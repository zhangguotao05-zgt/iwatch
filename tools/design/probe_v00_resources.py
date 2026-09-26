"""核对 V00 设计资源，并用锁定 SDK 的 eZIP 工具做离线容量与像素往返。"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
import zlib
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "docs/assets/v00-design-handoff"
MANIFEST = PACKAGE / "manifest.json"
FONT_APPROVAL = ROOT / "docs/assets/v00-typography-v1/visual-approval.json"
FONT_BUDGET = ROOT / "docs/ui/assets/v00/font-specimens/subsets/budget.json"
DEFAULT_REPORT = ROOT / "docs/ui/assets/v00/resource-study.json"
DEFAULT_WORK = ROOT / "work/v00/resource-study/probe"
IMAGE_LIMIT = 1_048_576
DESCRIPTOR_BYTES = 28  # 当前 GCC/Keil map 中 lv_image_dsc_t 的占用。
MAX_SECTION_PADDING = 3


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def package_file(relative, expected):
    path = (PACKAGE / relative).resolve()
    path.relative_to(PACKAGE.resolve())
    data = path.read_bytes()
    if len(data) != expected["bytes"] or sha256(data) != expected["sha256"]:
        raise ValueError("资源清单与文件不符：{}".format(relative))
    return path, data


def verified_blob(blob, expected_bytes, expected_sha):
    """固定大小和完整性校验；目标端接入前不得直接把坏数据交给图像解码器。"""
    return len(blob) == expected_bytes and sha256(blob) == expected_sha


def convert(tool, source, output, decode=False):
    if decode:
        args = [str(tool), "-convert", str(source), "-spt", "1", "-dpt", "0",
                "-dec_off", "0x0", "-outdir", str(output)]
    else:
        # 锁定 SDK 的默认转换模式保留 Alpha；-chip sf58x 在本工具版本会丢失透明度。
        args = [str(tool), "-convert", str(source), "-rgb565", "-binfile", "2",
                "-binext", ".bin", "-outdir", str(output)]
    result = subprocess.run(args, capture_output=True, text=True, errors="replace")
    if result.returncode:
        raise RuntimeError("eZIP 转换失败 {}: {}".format(source.name, result.returncode))


def pixel_difference(source, decoded):
    with Image.open(source) as original, Image.open(decoded) as restored:
        a, b = original.convert("RGBA"), restored.convert("RGBA")
        if a.size != b.size:
            raise ValueError("eZIP 往返尺寸改变：{}".format(source.name))
        differences = [0, 0, 0, 0]
        for first, second in zip(a.getdata(), b.getdata()):
            for channel in range(4):
                differences[channel] = max(differences[channel],
                                           abs(first[channel] - second[channel]))
    # RGB565 量化按当前转换工具可达 15/7/15；Alpha 必须逐像素一致。
    if any(actual > limit for actual, limit in zip(differences, (15, 7, 15, 0))):
        raise ValueError("eZIP 像素或透明度往返失败：{} {}".format(source.name, differences))
    return differences


def rejected_chip_mode(tool, work, manifest):
    """对照试验：当前工具的 sf58x 快速路径会抹掉项目图符的透明度。"""
    item = manifest["resources"][17]
    source, _ = package_file(item["encodings"]["png"]["path"],
                             item["encodings"]["png"])
    output = work / "sf58x-alpha-check"
    output.mkdir(parents=True, exist_ok=True)
    args = [str(tool), "-convert", str(source), "-rgb565", "-binfile", "2",
            "-binext", ".bin", "-chip", "sf58x", "-outdir", str(output)]
    encoded = subprocess.run(args, capture_output=True, text=True, errors="replace")
    if encoded.returncode:
        raise RuntimeError("sf58x 对照转换失败")
    binary = output / (source.stem + ".bin")
    convert(tool, binary, output, decode=True)
    with Image.open(source) as first, Image.open(output / (source.stem + ".png")) as second:
        a, b = first.convert("RGBA"), second.convert("RGBA")
        lost = sum(x[3] != y[3] for x, y in zip(a.getdata(), b.getdata()))
    if not lost:
        raise ValueError("sf58x 对照行为改变，需重审资源转换模式")
    return {"asset_id": item["id"], "alpha_changed_pixels": lost,
            "candidate_rejected": True}


def page_usage(manifest):
    usage = {item["id"]: [] for item in manifest["resources"]}
    for page in manifest["screens"]:
        spec_path, _ = package_file(page["spec"]["path"], page["spec"])
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        names = {placement["asset_id"] for placement in spec["resource_placements"]}
        optional = spec.get("optional_background")
        if optional:
            names.add(optional)
        for name in names:
            if name not in usage:
                raise ValueError("页面引用不存在的资源：{} {}".format(page["id"], name))
            usage[name].append(page["id"])
    return usage


def font_inventory():
    approval = json.loads(FONT_APPROVAL.read_text(encoding="utf-8"))
    budget = json.loads(FONT_BUDGET.read_text(encoding="utf-8"))
    if approval["approval_id"] != "IW-V00-FONT-VISUAL-20260923-1":
        raise ValueError("字体批准标识变化")
    fonts = []
    for item in approval["fonts"]:
        path = (ROOT / item["path"]).resolve()
        path.relative_to(ROOT.resolve())
        data = path.read_bytes()
        if not verified_blob(data, item["bytes"], item["sha256"]):
            raise ValueError("字体文件与批准清单不符：{}".format(path.name))
        fonts.append({"weight": item["weight"], "path": item["path"],
                      "bytes": len(data), "sha256": item["sha256"],
                      "license": "OFL；发布时仍需附许可文本"})
    if budget["total_with_legacy_bytes"] != budget["legacy_font_bytes"] + sum(
            item["bytes"] for item in fonts):
        raise ValueError("字体预算数据不一致")
    return {"fonts": fonts, "legacy_bytes": budget["legacy_font_bytes"],
            "combined_bytes": budget["total_with_legacy_bytes"],
            "limit_bytes": budget["limit_bytes"],
            "headroom_bytes": budget["headroom_bytes"],
            "license_path": "docs/ui/assets/v00/font-specimens/subsets/OFL.txt"}


def build_baseline():
    sys.path.insert(0, str(ROOT / "firmware"))
    from resource_budget import build_report

    result = {}
    for toolchain in ("gcc", "keil"):
        build_dir = ROOT / "firmware/iwatch/project/artifacts/DEV_A128_NAND" / toolchain
        report = build_report(build_dir, toolchain)
        if not report["passed"]:
            raise ValueError("现有固件资源预算未通过：{}".format(toolchain))
        result[toolchain] = {"git_head": report["git_head"],
                             "image_bytes": report["resources"]["image_bytes"],
                             "main_bin_bytes": report["resources"]["main_bin_bytes"],
                             "main_bin_sha256": report["evidence"]["main.bin"],
                             "identity_verified": True,
                             "v00_pages_or_assets_linked": False}
    return result


def build_report(tool, work):
    manifest_data = MANIFEST.read_bytes()
    manifest = json.loads(manifest_data)
    if manifest["approval_id"] != "IW-VISUAL-APPROVAL-20260923-V2":
        raise ValueError("视觉批准标识变化")
    if manifest["release_allowed"] or manifest["automatic_firmware_import"]:
        raise ValueError("设计参考包不得自动发布或并入固件")
    usage = page_usage(manifest)
    tool_data = tool.read_bytes()
    version = subprocess.run([str(tool), "-help"], capture_output=True,
                             text=True, errors="replace").stdout.splitlines()[0]
    if not re.search(r"ezip version: 2\.6\.4_20260414", version):
        raise ValueError("eZIP 工具版本未锁定：{}".format(version))
    work.mkdir(parents=True, exist_ok=True)
    entries = []
    for item in manifest["resources"]:
        encoding = "rgb565" if item["role"] == "optional_background" else "rgb565a8"
        raw_path, raw = package_file(item["encodings"][encoding]["path"],
                                     item["encodings"][encoding])
        source, _ = package_file(item["encodings"]["png"]["path"],
                                 item["encodings"]["png"])
        width, height = item["width"], item["height"]
        if not (0 < width <= 390 and 0 < height <= 450):
            raise ValueError("资源尺寸超过页面：{}".format(item["id"]))
        if len(raw) != width * height * (2 if encoding == "rgb565" else 3):
            raise ValueError("RGB565/A8 平面长度不符：{}".format(item["id"]))
        convert(tool, source, work)
        packed_path = work / (source.stem + ".bin")
        packed = packed_path.read_bytes()
        if len(packed) < 5:
            raise ValueError("eZIP 数据过短：{}".format(item["id"]))
        convert(tool, packed_path, work, decode=True)
        delta = pixel_difference(source, work / (source.stem + ".png"))
        entries.append({"id": item["id"], "kind": item["kind"],
                        "role": item["role"], "pages": usage[item["id"]],
                        "source_name": item.get("source_name"),
                        "source_url": item.get("source_url"),
                        "license_status": item["license_status"],
                        "release_allowed": item["release_allowed"],
                        "encoding": encoding, "width": width, "height": height,
                        "rgb_stride_bytes": width * 2,
                        "alpha_stride_bytes": 0 if encoding == "rgb565" else width,
                        "byte_order": "RGB565 little-endian; A8 separate plane",
                        "raw_path": str(raw_path.relative_to(ROOT)).replace("\\", "/"),
                        "raw_bytes": len(raw), "raw_sha256": sha256(raw),
                        "ezip_bin_bytes": len(packed),
                        "ezip_payload_bytes": len(packed) - 4,
                        "ezip_bin_sha256": sha256(packed),
                        "ezip_payload_crc32": "{:08x}".format(zlib.crc32(packed[4:])),
                        "estimated_linked_bytes": len(packed) - 4 + DESCRIPTOR_BYTES +
                                                  MAX_SECTION_PADDING,
                        "max_pixel_delta_rgba": delta})
    if len(entries) != 38 or len({item["id"] for item in entries}) != 38:
        raise ValueError("设计包资源数量变化")
    rejected = rejected_chip_mode(tool, work, manifest)
    baseline = build_baseline()
    cases = {}
    for name, selected in (("foreground", [item for item in entries
                                              if item["role"] != "optional_background"]),
                           ("all", entries)):
        estimated = sum(item["estimated_linked_bytes"] for item in selected)
        cases[name] = {"resources": len(selected),
                       "raw_bytes": sum(item["raw_bytes"] for item in selected),
                       "ezip_bin_bytes": sum(item["ezip_bin_bytes"] for item in selected),
                       "estimated_linked_bytes": estimated,
                       "gcc_headroom_after_bytes": IMAGE_LIMIT -
                           baseline["gcc"]["image_bytes"] - estimated,
                       "keil_headroom_after_bytes": IMAGE_LIMIT -
                           baseline["keil"]["image_bytes"] - estimated}
    return {"schema": 1, "scope": "离线资源探针；未链接 V00 目标页面、未上板",
            "visual_approval_id": manifest["approval_id"],
            "font_approval_id": "IW-V00-FONT-VISUAL-20260923-1",
            "release_allowed": False, "automatic_firmware_import": False,
            "manifest_sha256": sha256(manifest_data),
            "ezip_tool_sha256": sha256(tool_data), "ezip_version": version,
            "ezip_mode": "锁定 SDK 默认芯片模式，RGB565，binfile 2；不使用会丢 Alpha 的 -chip sf58x",
            "rejected_chip_mode": rejected,
            "descriptor_estimate_bytes_per_image": DESCRIPTOR_BYTES,
            "section_padding_bound_bytes_per_image": MAX_SECTION_PADDING,
            "image_budget_limit_bytes": IMAGE_LIMIT,
            "target_ezip_decoder_tested": False, "target_peak_ram_tested": False,
            "target_draw_time_tested": False,
            "baseline": baseline, "cases": cases,
            "max_raw_image_bytes": max(item["raw_bytes"] for item in entries),
            "fonts": font_inventory(), "resources": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ezip", type=Path, required=True)
    parser.add_argument("--work", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    report = build_report(args.ezip.resolve(), args.work.resolve())
    output = (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if args.check:
        if args.report.read_bytes() != output:
            raise ValueError("V00 资源探针报告已过期")
    else:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_bytes(output)
    print("V00 RESOURCE PROBE OK: {} resources; foreground {} B; all {} B; full deficit {} B".format(
        len(report["resources"]), report["cases"]["foreground"]["estimated_linked_bytes"],
        report["cases"]["all"]["estimated_linked_bytes"],
        -report["cases"]["all"]["gcc_headroom_after_bytes"]))


if __name__ == "__main__":
    main()
