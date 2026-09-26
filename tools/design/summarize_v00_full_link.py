# -*- coding: utf-8 -*-
"""核对隔离目录中的 V00 完整固件链接、身份和资源预算。"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "firmware"))
import resource_budget

DEFAULT_OUTPUT = ROOT / "docs/ui/assets/v00/resource-full-link.json"
BRIDGE_REPORT = ROOT / "docs/ui/assets/v00/resource-bridge.json"
DESCRIPTOR_BYTES = 28


def digest(data):
    return hashlib.sha256(data).hexdigest()


def v00_sections(map_path, toolchain):
    source = map_path.read_text(encoding="utf-8", errors="replace")
    if toolchain == "gcc":
        resource_budget.require_gcc_map(source)
        rows = [(name, address, size) for name, address, size, _ in
                resource_budget.parse_gcc_input_rows(source)]
    else:
        resource_budget.require_keil_map(source)
        rows = [(name, address, size) for address, size, name, _ in
                resource_budget.parse_keil_input_rows(source)]
    return [(name, address, size) for name, address, size in rows
            if name.startswith((".ROM3_IMG_EZIP.v00_", ".ROM3_IMG_EZIP_HEADER.v00_"))
            and address and size]


def check_v00_sections(rows, resources):
    selected = [item for item in resources if item["role"] != "optional_background"]
    expected = {}
    for item in selected:
        expected[".ROM3_IMG_EZIP." + item["symbol"]] = item["payload_bytes"]
        expected[".ROM3_IMG_EZIP_HEADER." + item["symbol"]] = DESCRIPTOR_BYTES
    found = {}
    for name, address, size in rows:
        if name in found:
            raise ValueError("目标图像段重复：{}".format(name))
        found[name] = (address, size)
    if set(found) != set(expected):
        raise ValueError("完整固件链接遗漏或增加了 V00 图像段")
    for name, size in expected.items():
        if found[name][1] != size:
            raise ValueError("完整固件图像尺寸不符：{}".format(name))
    addresses = [address for address, _ in found.values()]
    first = min(addresses)
    last = max(address + size for address, size in found.values())
    return {"resources": len(selected), "sections": len(found),
            "payload_bytes": sum(item["payload_bytes"] for item in selected),
            "descriptor_bytes": len(selected) * DESCRIPTOR_BYTES,
            "section_bytes": sum(size for _, size in found.values()),
            "aligned_span_bytes": last - first}


def check_linked_payloads(image_path, rows, resources):
    image = image_path.read_bytes()
    sections = resource_budget.read_elf32_sections(image_path)
    by_name = {name: (address, size) for name, address, size in rows}
    verified = 0
    for item in resources:
        if item["role"] == "optional_background":
            continue
        address, size = by_name[".ROM3_IMG_EZIP." + item["symbol"]]
        containers = [section for section in sections if section["type"] != 8 and
                      section["address"] <= address and
                      address + size <= section["address"] + section["size"]]
        if len(containers) != 1:
            raise ValueError("镜像中找不到唯一图像载荷：{}".format(item["symbol"]))
        section = containers[0]
        offset = section["offset"] + address - section["address"]
        if digest(image[offset:offset + size]) != item["payload_sha256"]:
            raise ValueError("实际链接字节与离线转换不同：{}".format(item["symbol"]))
        verified += 1
    return verified


def verify_archive(folder, toolchain, bridge_report):
    identity, _, _ = resource_budget.verify_identity(folder, toolchain)
    budget = json.loads((folder / "resource_budget.json").read_text(encoding="utf-8"))
    if not budget.get("passed") or budget.get("toolchain") != toolchain:
        raise ValueError("归档资源预算未通过：{}".format(toolchain))
    for name in ("main.bin", "main.map", "main." + ("elf" if toolchain == "gcc" else "axf")):
        if budget["evidence"][name] != identity["artifacts"][name]:
            raise ValueError("预算与构建身份产物不一致：{} {}".format(toolchain, name))
    inputs = identity["inputs"]["project_inputs"]
    for item in bridge_report["resources"]:
        if item["role"] == "optional_background":
            continue
        source = "firmware/iwatch/src/resource/images/common/ezip/{}.png".format(item["symbol"])
        if inputs.get(source) != item["source_png_sha256"]:
            raise ValueError("构建输入与资源清单不一致：{}".format(source))
    rows = v00_sections(folder / "main.map", toolchain)
    sections = check_v00_sections(rows, bridge_report["resources"])
    image_path = folder / ("main.elf" if toolchain == "gcc" else "main.axf")
    payloads_verified = check_linked_payloads(image_path, rows, bridge_report["resources"])
    resources = budget["resources"]
    image_limit = budget["checks"]["image_bytes"]["limit"]
    if resources["image_bytes"] != budget["checks"]["image_bytes"]["actual"] or \
            resources["image_bytes"] > image_limit:
        raise ValueError("完整固件图片区预算不一致或超限")
    return {"git_head": identity["git_head"],
            "embedded_tag": identity["embedded_tag"],
            "source_snapshot_sha256": digest(json.dumps(identity["inputs"],
                sort_keys=True, ensure_ascii=True).encode("ascii")),
            "main_bin_bytes": resources["main_bin_bytes"],
            "main_bin_sha256": identity["artifacts"]["main.bin"],
            "main_map_sha256": identity["artifacts"]["main.map"],
            "image_bytes": resources["image_bytes"],
            "image_limit_bytes": image_limit,
            "image_headroom_bytes": image_limit - resources["image_bytes"],
            "font_bytes": resources["subset_ttf_bytes"],
            "bitmap_font_bytes": resources["bitmap_font_bytes"],
            "v00_sections": sections,
            "linked_payloads_sha256_verified": payloads_verified}


def build_report(isolated_root):
    bridge_bytes = BRIDGE_REPORT.read_bytes()
    bridge_report = json.loads(bridge_bytes)
    archive = isolated_root / "firmware/iwatch/project/artifacts/DEV_A128_NAND"
    toolchains = {name: verify_archive(archive / name, name, bridge_report)
                  for name in ("gcc", "keil")}
    if toolchains["gcc"]["git_head"] != toolchains["keil"]["git_head"] or \
            toolchains["gcc"]["source_snapshot_sha256"] != toolchains["keil"]["source_snapshot_sha256"]:
        raise ValueError("GCC/Keil 来源不一致")
    if toolchains["gcc"]["image_bytes"] != toolchains["keil"]["image_bytes"]:
        raise ValueError("GCC/Keil 图片占用不一致")
    return {"schema": 1, "scope": "隔离工作树完整固件链接；未烧录、未验证硬件解码",
            "release_allowed": False, "flash_allowed": False,
            "temporary_link_reference": True,
            "resource_bridge_sha256": digest(bridge_bytes),
            "toolchains": toolchains}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--isolated-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    report = build_report(args.isolated_root.resolve())
    data = (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if args.check:
        if args.output.read_bytes() != data:
            raise ValueError("完整链接报告已过期")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
    for toolchain, row in report["toolchains"].items():
        print("{} 完整链接：图片 {} B，余量 {} B，主镜像 {} B".format(
            toolchain, row["image_bytes"], row["image_headroom_bytes"],
            row["main_bin_bytes"]))


if __name__ == "__main__":
    main()
