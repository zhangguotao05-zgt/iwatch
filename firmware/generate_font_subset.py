"""按版本化字符清单生成并校验确定性的中文字体子集。"""
import argparse
import ast
import hashlib
import importlib.metadata
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


FIRMWARE = Path(__file__).resolve().parent
ROOT = FIRMWARE.parent
DEFAULT_CONFIG = FIRMWARE / "iwatch/src/resource/fonts/font_charset.json"


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    return sha256_bytes(path.read_bytes())


def resolve_inside_root(value):
    path = (ROOT / value).resolve()
    try:
        path.relative_to(ROOT.resolve())
    except ValueError as error:
        raise ValueError("path escapes repository: {}".format(value)) from error
    return path


def collect_json_strings(value, output):
    if isinstance(value, str):
        output.append(value)
    elif isinstance(value, dict):
        for child in value.values():
            collect_json_strings(child, output)
    elif isinstance(value, list):
        for child in value:
            collect_json_strings(child, output)


def parse_c_string_array(path, array_name):
    text = path.read_text(encoding="utf-8")
    pattern = (r"static\s+const\s+[^;=]+\b" + re.escape(array_name) +
               r"\s*\[\s*\]\s*=\s*\{(.*?)\n\s*\};")
    match = re.search(pattern, text, re.S)
    if match is None:
        raise ValueError("C string array not found: {}".format(array_name))
    literals = re.findall(r'"(?:\\.|[^"\\])*"', match.group(1))
    try:
        return [ast.literal_eval(item) for item in literals]
    except (SyntaxError, ValueError) as error:
        raise ValueError("unsupported C string escape in {}".format(array_name)) from error


def collect_charset(config):
    strings = []
    for first, last in config["ascii_ranges"]:
        if first < 0 or last > 0x10FFFF or first > last:
            raise ValueError("invalid ASCII/codepoint range")
        strings.append("".join(chr(codepoint) for codepoint in range(first, last + 1)))

    input_files = []
    for value in config["json_value_sources"]:
        path = resolve_inside_root(value)
        input_files.append(path)
        collect_json_strings(json.loads(path.read_text(encoding="utf-8")), strings)

    notification = config["notification_source"]
    notification_path = resolve_inside_root(notification["path"])
    input_files.append(notification_path)
    actual_notifications = parse_c_string_array(notification_path, notification["array"])
    if actual_notifications != notification["expected_strings"]:
        raise ValueError("notification strings changed; update and review font_charset.json")
    strings.extend(actual_notifications)
    strings.extend(config["planned_static_strings"])

    charset = "".join(sorted(set("".join(strings))))
    expected = config["expected"]
    if len(charset) != expected["character_count"]:
        raise ValueError("character count changed: {} != {}".format(
            len(charset), expected["character_count"]))
    digest = sha256_bytes(charset.encode("utf-8"))
    if digest != expected["charset_sha256"]:
        raise ValueError("character set changed: {}".format(digest))
    return charset, input_files


def require_fonttools(version):
    try:
        actual = importlib.metadata.version("fonttools")
    except importlib.metadata.PackageNotFoundError as error:
        raise ValueError("FontTools is not installed") from error
    if actual != version:
        raise ValueError("FontTools version {} != {}".format(actual, version))


def font_metrics(path, charset):
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.ttLib import TTFont

    font = TTFont(str(path), recalcBBoxes=False, recalcTimestamp=False, lazy=False)
    try:
        cmap = font.getBestCmap() or {}
        glyph_set = font.getGlyphSet()
        metrics = {}
        missing = []
        for character in charset:
            codepoint = ord(character)
            glyph_name = cmap.get(codepoint)
            if glyph_name is None:
                missing.append("U+{:04X}".format(codepoint))
                continue
            pen = BoundsPen(glyph_set)
            glyph_set[glyph_name].draw(pen)
            metrics[codepoint] = {
                "advance_lsb": tuple(font["hmtx"].metrics[glyph_name]),
                "bounds": pen.bounds,
            }
        if missing:
            raise ValueError("font is missing characters: {}".format(", ".join(missing)))
        hhea = font["hhea"]
        os2 = font["OS/2"]
        vertical = (
            hhea.ascent, hhea.descent, hhea.lineGap,
            os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap,
            os2.usWinAscent, os2.usWinDescent,
        )
        return metrics, vertical
    finally:
        font.close()


def validate_metrics(source, subset, charset):
    source_metrics, source_vertical = font_metrics(source, charset)
    subset_metrics, subset_vertical = font_metrics(subset, charset)
    if source_vertical != subset_vertical:
        raise ValueError("vertical font metrics changed")
    for codepoint in sorted(source_metrics):
        if source_metrics[codepoint] != subset_metrics.get(codepoint):
            raise ValueError("glyph metrics changed at U+{:04X}".format(codepoint))


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(str(temporary), str(path))


def make_manifest(config_path, config, source, input_files, charset, subset):
    return {
        "schema": 1,
        "fonttools_version": config["fonttools_version"],
        "subset_options": config["subset_options"],
        "inputs": {
            config_path.relative_to(ROOT).as_posix(): sha256_file(config_path),
            source.relative_to(ROOT).as_posix(): sha256_file(source),
            **{path.relative_to(ROOT).as_posix(): sha256_file(path) for path in input_files},
        },
        "character_count": len(charset),
        "charset_sha256": sha256_bytes(charset.encode("utf-8")),
        "codepoints": ["U+{:04X}".format(ord(character)) for character in charset],
        "output": {
            "bytes": len(subset),
            "sha256": sha256_bytes(subset),
        },
    }


def generate(config_path, output_path, manifest_path, check):
    config_path = config_path.resolve()
    config = json.loads(config_path.read_text(encoding="utf-8"))
    require_fonttools(config["fonttools_version"])
    charset, input_files = collect_charset(config)
    source = resolve_inside_root(config["source_font"])
    expected = config["expected"]
    if source.stat().st_size != expected["source_font_bytes"] or sha256_file(source) != expected["source_font_sha256"]:
        raise ValueError("source font differs from approved input")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="iwatch-font-", dir=str(output_path.parent)) as directory:
        directory_path = Path(directory)
        charset_path = directory_path / "charset.txt"
        candidate_path = directory_path / "subset.ttf"
        charset_path.write_bytes(charset.encode("utf-8"))
        command = [
            sys.executable, "-m", "fontTools.subset", str(source),
            "--output-file={}".format(candidate_path),
            "--text-file={}".format(charset_path),
        ] + list(config["subset_options"])
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8")
        if result.returncode:
            raise ValueError("FontTools subset failed: {}".format(result.stderr.strip()))
        candidate = candidate_path.read_bytes()
        if len(candidate) != expected["subset_font_bytes"]:
            raise ValueError("subset size changed: {}".format(len(candidate)))
        if sha256_bytes(candidate) != expected["subset_font_sha256"]:
            raise ValueError("subset hash changed: {}".format(sha256_bytes(candidate)))
        validate_metrics(source, candidate_path, charset)

    manifest = make_manifest(config_path, config, source, input_files, charset, candidate)
    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    if check:
        if not output_path.is_file() or output_path.read_bytes() != candidate:
            raise ValueError("existing subset does not match deterministic output")
        if manifest_path and (not manifest_path.is_file() or manifest_path.read_bytes() != manifest_bytes):
            raise ValueError("existing subset manifest does not match deterministic output")
    else:
        atomic_write(output_path, candidate)
        if manifest_path:
            atomic_write(manifest_path, manifest_bytes)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        manifest = generate(args.config, args.output, args.manifest, args.check)
    except (OSError, ValueError, KeyError, json.JSONDecodeError, subprocess.SubprocessError) as error:
        print("FONT SUBSET ERROR: {}".format(error))
        return 1
    print("FONT SUBSET OK: {} chars, {} B, {}".format(
        manifest["character_count"], manifest["output"]["bytes"], manifest["output"]["sha256"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
