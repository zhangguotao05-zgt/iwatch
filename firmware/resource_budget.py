"""从已绑定构建身份的 GCC/Keil 产物统计固件资源预算。"""
import argparse
import hashlib
import json
import os
import re
import struct
from collections import Counter
from pathlib import Path, PureWindowsPath


LIMITS = {
    "subset_ttf_bytes": 131072,
    "bitmap_font_bytes": 65536,
    "image_bytes": 1048576,
    "main_bin_bytes": 4194304,
}
BITMAP_OBJECT = re.compile(r"lv_font_montserrat_\d+\.o$")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def object_name(value):
    value = value.strip().replace("/", "\\")
    return PureWindowsPath(value).name


def read_elf32_sections(path):
    data = path.read_bytes()
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("expected little-endian ELF32")
    section_offset = struct.unpack_from("<I", data, 32)[0]
    entry_size, count, names_index = struct.unpack_from("<HHH", data, 46)
    if entry_size != 40 or count == 0 or names_index >= count:
        raise ValueError("invalid ELF section table")
    table_end = section_offset + entry_size * count
    if section_offset < 52 or table_end > len(data):
        raise ValueError("truncated ELF section table")
    raw_sections = [struct.unpack_from("<10I", data, section_offset + index * entry_size)
                    for index in range(count)]
    names_header = raw_sections[names_index]
    names_end = names_header[4] + names_header[5]
    if names_end > len(data):
        raise ValueError("truncated ELF section names")
    names = data[names_header[4]:names_end]
    sections = []
    for header in raw_sections:
        name_offset, section_type, _, address, offset, size, _, _, _, _ = header
        if name_offset >= len(names):
            raise ValueError("invalid ELF section name offset")
        name = names[name_offset:].split(b"\0", 1)[0].decode("ascii", errors="strict")
        if section_type != 8 and (offset > len(data) or size > len(data) - offset):
            raise ValueError("ELF section exceeds file: {}".format(name))
        sections.append({"name": name, "type": section_type, "address": address,
                         "offset": offset, "size": size})
    return sections


def parse_gcc_input_rows(text):
    rows = []
    pending = None
    full = re.compile(r"^\s+(\.\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+\.o)\s*$")
    section_only = re.compile(r"^\s+(\.\S+)\s*$")
    continuation = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+\.o)\s*$")
    for line in text.splitlines():
        match = full.match(line)
        if match:
            rows.append((match.group(1), int(match.group(2), 16), int(match.group(3), 16),
                         object_name(match.group(4))))
            pending = None
            continue
        match = section_only.match(line)
        if match:
            pending = match.group(1)
            continue
        if pending:
            match = continuation.match(line)
            if match:
                rows.append((pending, int(match.group(1), 16), int(match.group(2), 16),
                             object_name(match.group(3))))
            pending = None
    return rows


def require_gcc_map(text):
    if "Component: ARM Compiler" in text:
        raise ValueError("GCC report received an armlink map")
    if ("Archive member included to satisfy reference by file (symbol)" not in text and
            "Linker script and memory map" not in text):
        raise ValueError("map is not a GCC linker map")


def gcc_resources(elf_path, map_path):
    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    require_gcc_map(map_text)
    sections = read_elf32_sections(elf_path)
    fonts = [section for section in sections if section["name"] == ".font_data" and section["size"]]
    if len(fonts) != 1 or fonts[0]["address"] == 0:
        raise ValueError("GCC ELF must contain one allocated .font_data section")
    images = [section for section in sections
              if section["name"].startswith(".ROM3_IMG") and section["size"]]
    if not images or any(section["address"] == 0 for section in images):
        raise ValueError("GCC ELF image sections are missing or unallocated")

    rows = [row for row in parse_gcc_input_rows(map_text) if row[1] != 0 and row[2] != 0]
    map_font = sum(size for section, _, size, _ in rows if section == ".font_data")
    map_images = sum(size for section, _, size, _ in rows if section.startswith(".ROM3_IMG"))
    if map_font != fonts[0]["size"] or map_images != sum(item["size"] for item in images):
        raise ValueError("GCC ELF and map resource totals differ")

    bitmap_objects = Counter()
    for section, _, size, obj in rows:
        if section.startswith(".rodata") and BITMAP_OBJECT.fullmatch(obj):
            bitmap_objects[obj] += size
    if not bitmap_objects:
        raise ValueError("GCC bitmap font inputs are missing")
    return {
        "subset_ttf_bytes": fonts[0]["size"],
        "bitmap_font_bytes": sum(bitmap_objects.values()),
        "image_bytes": sum(item["size"] for item in images),
        "bitmap_objects": dict(sorted(bitmap_objects.items())),
        "image_section_count": len(images),
    }


def parse_keil_input_rows(text):
    pattern = re.compile(
        r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)"
        r"\s+Data\s+RO\s+\d+\s+(\.\S+)\s+(\S+)\s*$", re.M)
    return [(int(match.group(1), 16), int(match.group(3), 16), match.group(4),
             object_name(match.group(5))) for match in pattern.finditer(text)]


def parse_keil_component_sizes(text):
    marker = text.find("Image component sizes")
    if marker < 0:
        raise ValueError("Keil Image component sizes table is missing")
    pattern = re.compile(
        r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)\s*$", re.M)
    return {object_name(match.group(7)): {
        "code": int(match.group(1)), "inline_data": int(match.group(2)),
        "ro_data": int(match.group(3)), "rw_data": int(match.group(4)),
        "zi_data": int(match.group(5)), "debug": int(match.group(6)),
    } for match in pattern.finditer(text[marker:])}


def require_keil_map(text):
    header = text[:131072]
    if "Component: ARM Compiler" not in header or "Tool: armlink" not in header:
        raise ValueError("map is not an armlink map")


def keil_resources(map_path):
    text = map_path.read_text(encoding="utf-8", errors="replace")
    require_keil_map(text)
    rows = [row for row in parse_keil_input_rows(text) if row[0] and row[1]]
    components = parse_keil_component_sizes(text)

    font_names = ("DroidSansFallback", "IWV00Noto300", "IWV00Noto400",
                  "IWV00Noto500", "IWV00Noto600")
    all_font_rows = [row for row in rows if row[2] == ".font_data"]
    font_rows = {row[3][:-2]: row[1] for row in all_font_rows
                 if row[3].endswith(".o") and row[3][:-2] in font_names}
    if len(font_rows) != len(all_font_rows):
        raise ValueError("Keil font inputs include duplicate or unknown sections")
    if "DroidSansFallback" not in font_rows or len(font_rows) not in (1, len(font_names)):
        raise ValueError("Keil font inputs are incomplete")
    for name, size in font_rows.items():
        symbol = re.findall(
            r"^\s*" + re.escape(name) +
            r"\s+0x[0-9a-fA-F]+\s+Data\s+(\d+)\s+" + re.escape(name) +
            r"\.o\(\.font_data\)\s*$", text, re.M)
        if len(symbol) != 1 or int(symbol[0]) != size:
            raise ValueError("Keil TTF symbol and input section differ: " + name)
        component = components.get(name + ".o")
        if not component or component["ro_data"] < size:
            raise ValueError("Keil TTF component differs: " + name)

    image_rows = [row for row in rows if row[2].startswith(".ROM3_IMG")]
    if not image_rows:
        raise ValueError("Keil image input sections are missing")
    image_by_object = Counter()
    for _, size, _, obj in image_rows:
        image_by_object[obj] += size
    for obj, size in image_by_object.items():
        if obj not in components or components[obj]["ro_data"] < size:
            raise ValueError("Keil component table disagrees with image inputs: {}".format(obj))

    bitmap_input = Counter()
    for _, size, section, obj in rows:
        if section.startswith(".rodata") and BITMAP_OBJECT.fullmatch(obj):
            bitmap_input[obj] += size
    bitmap_component = {obj: values["ro_data"] for obj, values in components.items()
                        if BITMAP_OBJECT.fullmatch(obj) and values["ro_data"]}
    if not bitmap_input or dict(bitmap_input) != bitmap_component:
        raise ValueError("Keil bitmap map inputs and component table differ")
    return {
        "subset_ttf_bytes": sum(font_rows.values()),
        "bitmap_font_bytes": sum(bitmap_input.values()),
        "image_bytes": sum(row[1] for row in image_rows),
        "bitmap_objects": dict(sorted(bitmap_input.items())),
        "image_section_count": len(image_rows),
    }


def verify_identity(build_dir, toolchain):
    identity_path = build_dir / "build_identity.json"
    identity = json.loads(identity_path.read_text(encoding="utf-8"))
    if identity.get("compiler", {}).get("name") != toolchain:
        raise ValueError("build identity toolchain differs from report")
    expected_map = identity.get("actual_map_toolchain")
    if expected_map and expected_map != toolchain:
        raise ValueError("build identity map provenance differs from report")
    extension = "elf" if toolchain == "gcc" else "axf"
    required = ["main.bin", "main.map", "main.{}".format(extension)]
    artifacts = identity.get("artifacts", {})
    for relative in required:
        path = (build_dir / relative).resolve()
        try:
            path.relative_to(build_dir.resolve())
        except ValueError as error:
            raise ValueError("artifact escapes build directory") from error
        if relative not in artifacts or not path.is_file() or sha256(path) != artifacts[relative]:
            raise ValueError("artifact is missing or differs from build identity: {}".format(relative))
    return identity, identity_path, build_dir / "main.{}".format(extension)


def atomic_json(path, value):
    data = (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(str(temporary), str(path))


def budget_checks(resources):
    return {name: {"actual": resources[name], "limit": limit,
                   "passed": resources[name] <= limit}
            for name, limit in LIMITS.items()}


def build_report(build_dir, toolchain):
    identity, identity_path, executable = verify_identity(build_dir, toolchain)
    map_path = build_dir / "main.map"
    resources = (gcc_resources(executable, map_path) if toolchain == "gcc"
                 else keil_resources(map_path))
    resources["main_bin_bytes"] = (build_dir / "main.bin").stat().st_size
    checks = budget_checks(resources)
    return {
        "schema": 1,
        "toolchain": toolchain,
        "profile": identity.get("profile"),
        "git_head": identity.get("git_head"),
        "sdk_commit": identity.get("inputs", {}).get("sdk_commit"),
        "sdk_patch": identity.get("inputs", {}).get("sdk_patch"),
        "resources": resources,
        "checks": checks,
        "passed": all(item["passed"] for item in checks.values()),
        "evidence": {
            "main.map": sha256(map_path),
            "main.bin": sha256(build_dir / "main.bin"),
            executable.name: sha256(executable),
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--toolchain", choices=("gcc", "keil"), required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    try:
        report = build_report(args.build_dir.resolve(), args.toolchain)
        if args.output:
            atomic_json(args.output, report)
    except (OSError, ValueError, KeyError, json.JSONDecodeError, struct.error, UnicodeError) as error:
        print("RESOURCE BUDGET ERROR: {}".format(error))
        return 1
    print("RESOURCE BUDGET {}: {}".format("OK" if report["passed"] else "OVER",
                                                json.dumps(report["resources"], sort_keys=True)))
    return 0 if report["passed"] or args.report_only else 1


if __name__ == "__main__":
    raise SystemExit(main())
