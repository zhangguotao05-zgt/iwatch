# -*- coding: utf-8 -*-
"""核对 GCC/Keil 最小目标链接中的真实 V00 图片段。"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.design import build_v00_resource_bridge as bridge


ROOT = bridge.ROOT
DEFAULT_REPORT = ROOT / "docs/ui/assets/v00/resource-target-link.json"
WORK = ROOT / "work/v00/resource-bridge"
GCC_OBJDUMP = Path("arm-none-eabi-objdump.exe")


def gcc_sections(image):
    result = subprocess.run([str(GCC_OBJDUMP), "-h", str(image)],
                            capture_output=True, text=True, check=True)
    rows = []
    pattern = re.compile(r"^\s*\d+\s+(\.ROM3_IMG_EZIP(?:_HEADER)?\.\S+)\s+"
                         r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", re.MULTILINE)
    for match in pattern.finditer(result.stdout):
        rows.append((match.group(1), int(match.group(2), 16), int(match.group(3), 16)))
    return rows


def keil_sections(map_path):
    source = map_path.read_text(encoding="utf-8")
    pattern = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+Data\s+RO\s+"
                         r"\d+\s+(\.ROM3_IMG_EZIP(?:_HEADER)?\.\S+)\s+", re.MULTILINE)
    return [(match.group(3), int(match.group(2), 16), int(match.group(1), 16))
            for match in pattern.finditer(source)]


def measure(rows, expected):
    selected = [item for item in expected if item["role"] != "optional_background"]
    by_name = {name: (size, address) for name, size, address in rows}
    if len(rows) != 2 * len(selected) or len(by_name) != len(rows):
        raise ValueError("目标链接中的 V00 图片段数量不符")
    for item in selected:
        symbol = item["symbol"]
        data = by_name.get(".ROM3_IMG_EZIP." + symbol)
        header = by_name.get(".ROM3_IMG_EZIP_HEADER." + symbol)
        if not data or data[0] != item["payload_bytes"] or \
                not header or header[0] != bridge.DESCRIPTOR_BYTES:
            raise ValueError("目标链接载荷或描述符不符：{}".format(symbol))
    addresses = [address for _, _, address in rows]
    first = min(addresses)
    last = max(address + size for _, size, address in rows)
    return {"resources": len(selected), "sections": len(rows),
            "payload_bytes": sum(item["payload_bytes"] for item in selected),
            "descriptor_bytes": len(selected) * bridge.DESCRIPTOR_BYTES,
            "section_bytes": sum(size for _, size, _ in rows),
            "aligned_span_bytes": last - first,
            "padding_bytes": last - first - sum(size for _, size, _ in rows)}


def build_report():
    source = bridge.DEFAULT_REPORT.read_bytes()
    bridge_report = json.loads(source)
    result = {"schema": 1, "scope": "ARM GCC/Keil 最小目标链接；不是完整固件链接或实机验收",
              "resource_bridge_sha256": bridge.digest(source),
              "release_allowed": False, "full_firmware_linked": False,
              "target_decoder_tested": False, "toolchains": {}}
    for toolchain, image_name in (("gcc", "v00-resource-probe.elf"),
                                  ("keil", "v00-resource-probe.axf")):
        folder = WORK / (toolchain + "-link")
        image = folder / image_name
        map_path = folder / "v00-resource-probe.map"
        if not image.is_file() or not map_path.is_file():
            raise ValueError("目标链接产物缺失：{}".format(toolchain))
        rows = gcc_sections(image) if toolchain == "gcc" else keil_sections(map_path)
        usage = measure(rows, bridge_report["resources"])
        if usage["aligned_span_bytes"] > \
                bridge_report["cases"]["foreground"]["estimated_linked_bytes"]:
            raise ValueError("目标图片段超过离线对齐上界：{}".format(toolchain))
        baseline = bridge_report["baseline"][toolchain]["image_bytes"]
        result["toolchains"][toolchain] = {
            "image_sha256": bridge.digest(image.read_bytes()),
            "map_sha256": bridge.digest(map_path.read_bytes()),
            "baseline_image_bytes": baseline,
            "provisional_sum_bytes": baseline + usage["aligned_span_bytes"],
            "provisional_headroom_bytes": bridge_report["image_budget_limit_bytes"] -
                baseline - usage["aligned_span_bytes"],
            "image_sections": usage}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    report = build_report()
    data = (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if args.check:
        if args.output.read_bytes() != data:
            raise ValueError("最小目标链接报告已过期")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
    for toolchain, row in report["toolchains"].items():
        print("{} V00 LINK: image span {} B; provisional headroom {} B".format(
            toolchain, row["image_sections"]["aligned_span_bytes"],
            row["provisional_headroom_bytes"]))


if __name__ == "__main__":
    main()
