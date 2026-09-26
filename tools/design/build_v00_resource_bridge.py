# -*- coding: utf-8 -*-
"""用固件图片链路的参数生成 V00 资源，并核对 C 描述符与二进制载荷。"""

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.design import probe_v00_resources as previous


MANIFEST = previous.MANIFEST
SCONSCRIPT = ROOT / "firmware/iwatch/src/resource/images/SConscript"
LOCKED_RTCONFIG = ROOT / "firmware/iwatch/project/artifacts/DEV_A128_NAND/gcc/rtconfig.h"
DEFAULT_WORK = ROOT / "work/v00/resource-bridge/generated"
DEFAULT_REPORT = ROOT / "docs/ui/assets/v00/resource-bridge.json"
ENCODER_FLAGS = ("-rgb565", "-section", "ROM3_IMG", "-lvgl_version", "9", "-dpt", "1")
LVGL_HEADER_BYTES = 12
DESCRIPTOR_BYTES = 28
ALIGNMENT_PADDING_MAX = 3
EZIP_HEADERS = {"rgb565": {(0x18, 0x10)}, "rgb565a8": {(0x1c, 0x18)}}
LVGL_FORMAT = {"rgb565": ("LV_COLOR_FORMAT_RAW", 1),
               "rgb565a8": ("LV_COLOR_FORMAT_RAW_ALPHA", 2)}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def symbol_for(resource_id):
    symbol = "v00_" + re.sub(r"[^a-zA-Z0-9_]", "_", resource_id)
    if not re.fullmatch(r"[a-zA-Z_][a-zA-Z0-9_]*", symbol):
        raise ValueError("资源 ID 无法转换为 C 标识符：{}".format(resource_id))
    return symbol


def assert_firmware_flags():
    script = SCONSCRIPT.read_text(encoding="utf-8")
    required = ("v00_ezip = Glob('common/ezip/v00_*.png')",
                "objs_ezip = [resource for resource in objs_ezip if resource not in v00_ezip]",
                "v00_img_flags = '-rgb565  -section ROM3_IMG  -lvgl_version 9 '",
                "Env.ImgResource(v00_ezip, v00_img_flags+' -cfile 2 -dpt 1')")
    if any(flag not in script for flag in required):
        raise ValueError("正式图片构建参数已变化，需重新核对转换链路")
    config = LOCKED_RTCONFIG.read_text(encoding="utf-8")
    if "#define LV_COLOR_DEPTH 16" not in config or \
            "#define EZIP_PAL_SUPPORT 1" not in config or \
            "#define EZIP_PAL_SUPPORT_1" in config:
        raise ValueError("构建配置的 eZIP 调色板模式已变化")
    return digest(SCONSCRIPT.read_bytes()), digest(LOCKED_RTCONFIG.read_bytes())


def run_converter(tool, source, target_dir, mode):
    target_dir.mkdir(parents=True, exist_ok=True)
    suffix = ".c" if mode == "c" else ".bin" if mode == "bin" else ".png"
    target = target_dir / (source.stem + suffix)
    if target.exists():
        target.unlink()
    if mode == "decode":
        args = ("-spt", "1", "-dpt", "0", "-dec_off_no_header", "0")
    else:
        args = ENCODER_FLAGS + (("-cfile", "2") if mode == "c" else
                                ("-binfile", "2", "-binext", ".bin"))
    command = [str(tool), "-convert", str(source)] + list(args) + ["-outdir", str(target_dir)]
    result = subprocess.run(command, capture_output=True, text=True,
                            errors="replace", timeout=30)
    if result.returncode or not target.is_file() or not target.stat().st_size or \
            "convert fail" in result.stdout.lower() or "write fail" in result.stdout.lower():
        raise RuntimeError("eZIP 转换未产出有效文件：{} {}\n{}".format(
            mode, source.name, result.stdout[-700:]))
    return target


def field(source, name):
    match = re.search(r"\.{}\s*=\s*(0x[0-9a-fA-F]+|[0-9]+|[A-Z_0-9]+)".format(
        re.escape(name)), source)
    if not match:
        raise ValueError("C 描述符缺少字段：{}".format(name))
    return match.group(1)


def parse_c_image(path, symbol):
    source = path.read_text(encoding="utf-8")
    match = re.search(r"const\s+uint8_t\s+" + re.escape(symbol) +
                      r"_map\[\]\s+SECTION\(\"\.ROM3_IMG_EZIP\." +
                      re.escape(symbol) + r"\"\)\s*=\s*\{([^}]+)\}",
                      source, re.DOTALL)
    if not match:
        raise ValueError("C 图像数组或链接段不匹配：{}".format(symbol))
    payload = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))
    if not payload or "SECTION(\".ROM3_IMG_EZIP_HEADER.{}\")".format(symbol) not in source:
        raise ValueError("C 图像数据或描述符缺失：{}".format(symbol))
    return payload, {key: field(source, key) for key in
                     ("header.magic", "header.cf", "header.flags", "header.w",
                      "header.h", "header.stride", "header.reserved_2", "data_size")}


def validate_pair(binary, payload, descriptor, width, height, encoding):
    if len(binary) != LVGL_HEADER_BYTES + len(payload) or binary[LVGL_HEADER_BYTES:] != payload:
        raise ValueError("BIN 与固件 C 图像载荷不一致")
    if len(payload) < 16 or int.from_bytes(payload[:4], "big") != len(payload):
        raise ValueError("eZIP 内部载荷长度与描述符不一致")
    codec = (payload[4], payload[5])
    if codec not in EZIP_HEADERS[encoding] or \
            int.from_bytes(payload[8:10], "big") != width or \
            int.from_bytes(payload[10:12], "big") != height:
        raise ValueError("eZIP 内部尺寸或编码格式不一致")
    magic, color, flags, bin_width, bin_height, stride, reserved = struct.unpack(
        "<BBHHHHH", binary[:LVGL_HEADER_BYTES])
    expected_color = LVGL_FORMAT[encoding]
    bin_color = expected_color[1]
    expected = {"header.magic": 0x19, "header.cf": expected_color[0],
                "header.flags": "LV_IMAGE_FLAGS_USER1", "header.w": width,
                "header.h": height, "header.stride": 0,
                "header.reserved_2": 0, "data_size": len(payload)}
    for key, value in expected.items():
        actual = descriptor[key]
        if key == "header.cf" and actual == "LV_IMG_CF_RAW_ALPHA":
            actual = "LV_COLOR_FORMAT_RAW_ALPHA"
        if isinstance(value, str):
            matches = actual == value
        else:
            try:
                matches = int(actual, 0) == value
            except ValueError:
                matches = False
        if not matches:
            raise ValueError("C 描述符与资源规格不一致：{}".format(key))
    if (magic, color, flags, bin_width, bin_height, stride, reserved) != \
            (0x19, bin_color, 0x0100, width, height, 0, 0):
        raise ValueError("BIN 图像头与源图尺寸/编码不一致")
    return codec, bin_color


def write_catalog(work, entries):
    """固定前景资源与实际生成符号的关系；背景在本阶段保持可选。"""
    selected = [item for item in entries if item["role"] != "optional_background"]
    header = """/* 本文件由 build_v00_resource_bridge.py 生成，不要手改。 */
#ifndef IW_V00_RESOURCE_CATALOG_H
#define IW_V00_RESOURCE_CATALOG_H

#include "iw_v00_resource_guard.h"
#include <stdbool.h>
#include <stddef.h>

size_t iw_v00_resource_catalog_count(void);
iw_v00_resource_result_t iw_v00_resource_catalog_validate(size_t index, bool for_release);

#endif
"""
    source = ["/* 本文件由 build_v00_resource_bridge.py 生成，不要手改。 */",
              '#include "iw_v00_resource_catalog.h"', '#include "lvgl.h"', "",
              "typedef struct {",
              "    const char *id;",
              "    const lv_image_dsc_t *image;",
              "    iw_v00_resource_binding_t binding;",
              "} catalog_entry_t;", "",
              "_Static_assert(LV_COLOR_FORMAT_RAW == 1, \"LVGL RAW 格式已变化\");",
              "_Static_assert(LV_COLOR_FORMAT_RAW_ALPHA == 2, \"LVGL RAW_ALPHA 格式已变化\");",
              ""]
    for item in selected:
        source.extend(("extern const lv_image_dsc_t {};".format(item["symbol"]),
                       "extern const uint8_t {}_map[];".format(item["symbol"])))
    source.extend(("", "static const catalog_entry_t catalog[] = {"))
    for item in selected:
        fmt = "IW_V00_RESOURCE_EZIP_RGB565A8" if item["encoding"] == "rgb565a8" else \
              "IW_V00_RESOURCE_EZIP_RGB565"
        source.extend(("    {{ {}, &{}, {{".format(json.dumps(item["id"]), item["symbol"]),
                       "        {{ IW_V00_RESOURCE_ABI, {}, {}, {}, {}, {}, 0x{:02x}u, 0x{:02x}u, false, {}, 0x{}u }},".format(
                           item["width"], item["height"], item["rgb_logical_stride_bytes"],
                           item["alpha_logical_stride_bytes"], fmt,
                           item["ezip_codec"], item["ezip_flags"], item["payload_bytes"],
                           item["payload_crc32"]),
                       "        {}_map".format(item["symbol"]), "    } },"))
    source.extend(("};", "", "size_t iw_v00_resource_catalog_count(void)", "{",
                   "    return sizeof(catalog) / sizeof(catalog[0]);", "}", "",
                   "iw_v00_resource_result_t iw_v00_resource_catalog_validate(size_t index, bool for_release)",
                   "{", "    if (index >= iw_v00_resource_catalog_count())",
                   "        return IW_V00_RESOURCE_INVALID;", "",
                   "    const catalog_entry_t *entry = &catalog[index];",
                   "    const lv_image_dsc_t *image = entry->image;",
                   "    const iw_v00_resource_image_t view = {",
                   "        image->header.w, image->header.h, image->header.stride,",
                   "        image->header.cf, image->data_size, image->data",
                   "    };", "    return iw_v00_resource_validate_binding(&entry->binding, &view, for_release);",
                   "}", ""))
    out = work / "catalog"
    out.mkdir(parents=True, exist_ok=True)
    with (out / "iw_v00_resource_catalog.h").open("w", encoding="utf-8", newline="\n") as file:
        file.write(header)
    content = "\n".join(source)
    with (out / "iw_v00_resource_catalog.c").open("w", encoding="utf-8", newline="\n") as file:
        file.write(content)
    return digest(content.encode("utf-8"))


def build_report(tool, work):
    manifest_bytes = MANIFEST.read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest["approval_id"] != "IW-VISUAL-APPROVAL-20260923-V2" or \
            manifest["release_allowed"] or manifest["automatic_firmware_import"]:
        raise ValueError("资源包不属于已批准的本地验证范围")
    script_sha, config_sha = assert_firmware_flags()
    tool_sha = digest(tool.read_bytes())
    if tool_sha != "be5e8a60dce6d8f9470087df42d68ba8a3165d230c950f42880fd3c0719842e7":
        raise ValueError("eZIP 工具与已复核 SDK 不一致")
    sources = work / "source"
    entries = []
    symbols = set()
    for item in manifest["resources"]:
        symbol = symbol_for(item["id"])
        if symbol in symbols:
            raise ValueError("资源符号重复：{}".format(symbol))
        symbols.add(symbol)
        png, png_data = previous.package_file(item["encodings"]["png"]["path"],
                                              item["encodings"]["png"])
        sources.mkdir(parents=True, exist_ok=True)
        source = sources / (symbol + ".png")
        source.write_bytes(png_data)
        c_path = run_converter(tool, source, work / "c", "c")
        bin_path = run_converter(tool, source, work / "bin", "bin")
        c_payload, descriptor = parse_c_image(c_path, symbol)
        binary = bin_path.read_bytes()
        encoding = "rgb565" if item["role"] == "optional_background" else "rgb565a8"
        codec, bin_color = validate_pair(binary, c_payload, descriptor,
                                         item["width"], item["height"], encoding)
        payload = work / "payload" / (symbol + ".bin")
        payload.parent.mkdir(parents=True, exist_ok=True)
        payload.write_bytes(c_payload)
        decoded = run_converter(tool, payload, work / "decoded", "decode")
        delta = previous.pixel_difference(png, decoded)
        entries.append({"id": item["id"], "symbol": symbol, "kind": item["kind"],
                        "role": item["role"], "release_allowed": item["release_allowed"],
                        "width": item["width"], "height": item["height"],
                        "encoding": encoding, "lvgl_color_format": descriptor["header.cf"],
                        "bin_color_format": bin_color,
                        "ezip_codec": codec[0], "ezip_flags": codec[1],
                        "lvgl_descriptor_stride": 0,
                        "rgb_logical_stride_bytes": item["width"] * 2,
                        "alpha_logical_stride_bytes": item["width"] if encoding == "rgb565a8" else 0,
                        "source_png_sha256": digest(png_data),
                        "c_source_sha256": digest(c_path.read_bytes()),
                        "bin_sha256": digest(binary), "bin_bytes": len(binary),
                        "payload_sha256": digest(c_payload),
                        "payload_bytes": len(c_payload),
                        "payload_crc32": "{:08x}".format(zlib.crc32(c_payload)),
                        "estimated_linked_bytes": len(c_payload) + DESCRIPTOR_BYTES +
                                                  ALIGNMENT_PADDING_MAX,
                        "max_pixel_delta_rgba": delta})
    if len(entries) != 38:
        raise ValueError("资源数量已变化")
    catalog_sha = write_catalog(work, entries)
    baseline = previous.build_baseline()
    cases = {}
    for case, selected in (("foreground", [entry for entry in entries
                                            if entry["role"] != "optional_background"]),
                           ("all", entries)):
        size = sum(entry["estimated_linked_bytes"] for entry in selected)
        cases[case] = {"resources": len(selected), "estimated_linked_bytes": size,
                       "gcc_headroom_after_bytes": previous.IMAGE_LIMIT -
                           baseline["gcc"]["image_bytes"] - size,
                       "keil_headroom_after_bytes": previous.IMAGE_LIMIT -
                           baseline["keil"]["image_bytes"] - size}
    return {"schema": 2,
            "scope": "与正式 SConscript 同参数的离线资源桥；尚未完整链接或上板",
            "visual_approval_id": manifest["approval_id"],
            "manifest_sha256": digest(manifest_bytes), "sconscript_sha256": script_sha,
            "locked_rtconfig_sha256": config_sha,
            "ezip_tool_sha256": tool_sha, "chip_mode": "SDK default sf55x",
            "encoder_flags": list(ENCODER_FLAGS),
            "c_mode": ["-cfile", "2"], "bin_mode": ["-binfile", "2", "-binext", ".bin"],
            "binary_header_bytes": LVGL_HEADER_BYTES,
            "foreground_catalog_sha256": catalog_sha,
            "decode_flags": ["-spt", "1", "-dpt", "0", "-dec_off_no_header", "0"],
            "image_budget_limit_bytes": previous.IMAGE_LIMIT,
            "release_allowed": False, "target_decoder_tested": False,
            "target_full_link_tested": False,
            "baseline": baseline, "cases": cases, "resources": entries}


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
            raise ValueError("正式参数资源报告已过期")
    else:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_bytes(output)
    print("V00 RESOURCE BRIDGE OK: {} resources; foreground {} B; all {} B".format(
        len(report["resources"]), report["cases"]["foreground"]["estimated_linked_bytes"],
        report["cases"]["all"]["estimated_linked_bytes"]))


if __name__ == "__main__":
    main()
