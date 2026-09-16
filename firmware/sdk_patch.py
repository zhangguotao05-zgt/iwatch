"""管理固定 SiFli SDK 上的受控字体内存安全补丁。"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path, PurePosixPath


FIRMWARE = Path(__file__).resolve().parent
ROOT = FIRMWARE.parent
DEFAULT_MANIFEST = FIRMWARE / "patches/sdk_patch_manifest.json"


def run_git(sdk, *args, check=True):
    result = subprocess.run(["git", "-C", str(sdk), *args], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, encoding="utf-8")
    if check and result.returncode:
        raise ValueError("git {} failed: {}".format(" ".join(args), result.stderr.strip()))
    return result


def normalized_sha256(data):
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def file_sha256(path):
    return normalized_sha256(path.read_bytes())


def load_manifest(path=DEFAULT_MANIFEST):
    path = path.resolve()
    manifest = json.loads(path.read_text(encoding="utf-8"))
    patch_path = (ROOT / manifest["patch_file"]).resolve()
    try:
        patch_path.relative_to(ROOT.resolve())
    except ValueError as error:
        raise ValueError("patch path escapes repository") from error
    digest = hashlib.sha256(patch_path.read_bytes()).hexdigest()
    if digest != manifest["patch_sha256"]:
        raise ValueError("SDK patch hash differs from manifest")
    paths = [item["path"] for item in manifest["files"]]
    if len(paths) != len(set(paths)) or not paths:
        raise ValueError("SDK patch whitelist is empty or duplicated")
    for item in manifest["files"]:
        relative = PurePosixPath(item["path"])
        if relative.is_absolute() or ".." in relative.parts or relative.as_posix() != item["path"]:
            raise ValueError("invalid SDK patch path: {}".format(item["path"]))
        for field in ("base_sha256", "patched_sha256"):
            value = item[field]
            if len(value) != 64 or any(character not in "0123456789abcdef" for character in value):
                raise ValueError("invalid {} for {}".format(field, item["path"]))
    return manifest, patch_path


def require_commit(sdk, manifest):
    commit = run_git(sdk, "rev-parse", "HEAD").stdout.strip()
    if commit != manifest["sdk_commit"]:
        raise ValueError("wrong SDK commit: {}".format(commit))
    return commit


def changed_paths(sdk):
    staged_or_modified = set(filter(None, run_git(sdk, "diff", "--name-only", "HEAD", "--").stdout.splitlines()))
    untracked = set(filter(None, run_git(sdk, "ls-files", "--others", "--exclude-standard").stdout.splitlines()))
    return staged_or_modified, untracked


def verify_base(sdk, manifest_path=DEFAULT_MANIFEST):
    sdk = sdk.resolve()
    manifest, patch_path = load_manifest(manifest_path)
    commit = require_commit(sdk, manifest)
    changed, untracked = changed_paths(sdk)
    if changed or untracked:
        raise ValueError("base SDK must be clean")
    for item in manifest["files"]:
        data = run_git(sdk, "show", "HEAD:" + item["path"]).stdout.encode("utf-8")
        if normalized_sha256(data) != item["base_sha256"]:
            raise ValueError("base file hash differs: {}".format(item["path"]))
    check = run_git(sdk, "apply", "--check", str(patch_path), check=False)
    if check.returncode:
        raise ValueError("patch does not apply to pinned SDK: {}".format(check.stderr.strip()))
    return {
        "mode": "base",
        "sdk_commit": commit,
        "patch_sha256": manifest["patch_sha256"],
        "modified_files": [],
    }


def verify_derived(sdk, manifest_path=DEFAULT_MANIFEST):
    sdk = sdk.resolve()
    manifest, patch_path = load_manifest(manifest_path)
    commit = require_commit(sdk, manifest)
    allowed = {item["path"] for item in manifest["files"]}
    changed, untracked = changed_paths(sdk)
    if untracked:
        raise ValueError("derived SDK contains untracked files: {}".format(", ".join(sorted(untracked))))
    if changed != allowed:
        raise ValueError("derived SDK changes differ from whitelist: {}".format(", ".join(sorted(changed))))
    post_hashes = {}
    for item in manifest["files"]:
        path = sdk / item["path"]
        digest = file_sha256(path)
        if digest != item["patched_sha256"]:
            raise ValueError("patched file hash differs: {}".format(item["path"]))
        post_hashes[item["path"]] = digest
    reverse = run_git(sdk, "apply", "--check", "--reverse", str(patch_path), check=False)
    if reverse.returncode:
        raise ValueError("derived SDK is not the declared patch: {}".format(reverse.stderr.strip()))
    return {
        "mode": "patched",
        "sdk_commit": commit,
        "patch_sha256": manifest["patch_sha256"],
        "modified_files": sorted(allowed),
        "patched_file_sha256": post_hashes,
    }


def identify_sdk(sdk, manifest_path=DEFAULT_MANIFEST):
    changed, untracked = changed_paths(sdk.resolve())
    if not changed and not untracked:
        return verify_base(sdk, manifest_path)
    return verify_derived(sdk, manifest_path)


def prepare(base_sdk, output, manifest_path=DEFAULT_MANIFEST, init_submodules=False):
    base_sdk = base_sdk.resolve()
    output = output.resolve()
    manifest, patch_path = load_manifest(manifest_path)
    verify_base(base_sdk, manifest_path)
    if output.exists():
        raise ValueError("derived SDK output already exists: {}".format(output))
    output.parent.mkdir(parents=True, exist_ok=True)
    result = run_git(base_sdk, "worktree", "add", "--detach", str(output), manifest["sdk_commit"], check=False)
    if result.returncode:
        raise ValueError("unable to create SDK worktree: {}".format(result.stderr.strip()))
    try:
        apply_result = run_git(output, "apply", str(patch_path), check=False)
        if apply_result.returncode:
            raise ValueError("unable to apply SDK patch: {}".format(apply_result.stderr.strip()))
        if init_submodules:
            update = run_git(output, "submodule", "update", "--init", "--recursive", check=False)
            if update.returncode:
                raise ValueError("unable to initialize SDK submodules: {}".format(update.stderr.strip()))
        return verify_derived(output, manifest_path)
    except Exception:
        # 输出路径由本函数刚创建，只通过 Git worktree 命令回收，避免残留半应用 SDK。
        run_git(base_sdk, "worktree", "remove", "--force", str(output), check=False)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("verify-base", "verify-derived", "identify", "prepare"))
    parser.add_argument("--sdk", type=Path)
    parser.add_argument("--base-sdk", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--init-submodules", action="store_true")
    args = parser.parse_args()
    try:
        if args.action == "prepare":
            if args.base_sdk is None or args.output is None:
                raise ValueError("prepare requires --base-sdk and --output")
            result = prepare(args.base_sdk, args.output, args.manifest, args.init_submodules)
        else:
            if args.sdk is None:
                raise ValueError("{} requires --sdk".format(args.action))
            if args.action == "verify-base":
                result = verify_base(args.sdk, args.manifest)
            elif args.action == "verify-derived":
                result = verify_derived(args.sdk, args.manifest)
            else:
                result = identify_sdk(args.sdk, args.manifest)
    except (OSError, ValueError, KeyError, json.JSONDecodeError, subprocess.SubprocessError) as error:
        print("SDK PATCH ERROR: {}".format(error))
        return 1
    print("SDK PATCH OK: {}".format(json.dumps(result, ensure_ascii=False, sort_keys=True)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
