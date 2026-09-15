"""Read-only guards for the iwatch A128R32N1 project, not a generic SoC map.

Run before build; add --build-dir after build to also check actual binaries and
generated download addresses. This does not format, migrate or flash a device.
"""

import argparse
import json
import re
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent
TABLE = FIRMWARE / "iwatch/project/iwatch_sf32lb58_a128_qspi_hcpu/ptab.json"
BOARD = FIRMWARE / "boards/iwatch_sf32lb58_a128_qspi/hcpu/board.conf"
STALE_BOARD_TABLE = FIRMWARE / "boards/iwatch_sf32lb58_a128_qspi/ptab.yaml"
LEGACY_BOARD_TABLE = FIRMWARE / "boards/iwatch_sf32lb58_a128_qspi/legacy/reference/ptab.yaml"
MIB = 1024 * 1024
BASES = {"psram1": 0x60000000, "psram2": 0x62000000,
         "flash4": 0x68000000, "flash5": 0x1C000000}


def validate_layout_sources():
    errors = []
    if STALE_BOARD_TABLE.exists():
        errors.append(f"stale board partition table must not be a build candidate: {STALE_BOARD_TABLE}")
    if not LEGACY_BOARD_TABLE.is_file():
        errors.append(f"missing archived legacy partition reference: {LEGACY_BOARD_TABLE}")
    return errors


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def board_capacities(path):
    config = dict(re.findall(r"^(CONFIG_BSP_QSPI[1245]_MEM_SIZE)=(\d+)$",
                             path.read_text(encoding="utf-8"), re.M))
    # MPI5's 4 MiB is the SDK default for this board; explicit values win.
    config.setdefault("CONFIG_BSP_QSPI5_MEM_SIZE", "4")
    return {name: int(config[f"CONFIG_BSP_QSPI{index}_MEM_SIZE"]) * MIB
            for name, index in (("psram1", 1), ("psram2", 2),
                                ("flash4", 4), ("flash5", 5))}


def validate_layout(table, capacities):
    errors = []
    banks = {}
    for bank in table:
        name = bank["mem"]
        if name in banks:
            errors.append(f"duplicate memory bank: {name}")
        banks[name] = bank
    for name, base in BASES.items():
        bank = banks.get(name)
        if bank is None:
            errors.append(f"missing memory bank: {name}")
            continue
        if number(bank["base"]) != base:
            errors.append(f"{name}: unexpected address for this board")
        occupied = []
        for index, region in enumerate(bank["regions"]):
            start, size = number(region["offset"]), number(region["max_size"])
            label = "/".join(region.get("tags", [])) or region.get("img", str(index))
            end = start + size
            if start < 0 or size <= 0 or end > capacities[name]:
                errors.append(f"{name}/{label}: offset 0x{start:X} + size 0x{size:X} "
                              f"exceeds/violates bank capacity 0x{capacities[name]:X}")
            for other_start, other_end, other_label in occupied:
                if start < other_end and other_start < end:
                    errors.append(f"{name}: {label} overlaps {other_label}")
            occupied.append((start, end, label))

    # The CBUS view aliases PSRAM1. It is NOT a second physical bank.
    run = [r for r in banks.get("psram1", {}).get("regions", [])
           if r.get("ftab", {}).get("name") == "main"]
    alias = [r for r in banks.get("psram1_cbus", {}).get("regions", [])
             if "HCPU_FLASH_CODE" in r.get("tags", [])]
    load = [r for r in banks.get("flash4", {}).get("regions", [])
            if r.get("img") == "main"]
    if len(run) != 1 or len(alias) != 1 or len(load) != 1:
        errors.append("main must have exactly one PSRAM run region, CBUS alias and NAND load slot")
    else:
        if banks["psram1_cbus"]["base"] not in ("0x10000000", 0x10000000):
            errors.append("unexpected main CBUS base for this board")
        for field in ("offset", "max_size"):
            if number(run[0][field]) != number(alias[0][field]):
                errors.append(f"main PSRAM/CBUS {field} mismatch")
        if number(run[0]["max_size"]) > number(load[0]["max_size"]):
            errors.append("main link window is larger than its NAND slot; align budgets before building")
    return errors


def validate_images(table, build_dir):
    errors = []
    slots = {}
    run_size = None
    for bank in table:
        for region in bank["regions"]:
            if bank["mem"] == "psram1" and region.get("ftab", {}).get("name") == "main":
                run_size = number(region["max_size"])
            if bank["mem"].startswith("flash") and "img" in region:
                slots[region["img"]] = (number(bank["base"]) + number(region["offset"]),
                                        number(region["max_size"]))
    config_file = build_dir / "sftool_param.json"
    if not config_file.is_file():
        return [f"missing generated download manifest: {config_file}"]
    manifest = json.loads(config_file.read_text(encoding="utf-8"))
    seen = set()
    for item in manifest["write_flash"]["files"]:
        image_path = build_dir / item["path"].replace("\\", "/")
        name = image_path.stem
        if name in seen:
            errors.append(f"duplicate download image: {name}")
        seen.add(name)
        if name not in slots:
            errors.append(f"download image {name} has no declared slot")
            continue
        address, size = slots[name]
        if number(item["address"]) != address:
            errors.append(f"{name}: stale/wrong download address; expected 0x{address:X}")
        if not image_path.is_file():
            errors.append(f"missing image: {image_path}")
            continue
        length = image_path.stat().st_size
        limit = min(size, run_size) if name == "main" and run_size else size
        if length <= 0 or length > limit:
            errors.append(f"{name}: image {length} B exceeds/violates slot {limit} B")
    for name in ("main", "bootloader", "ftab"):
        if name not in seen:
            errors.append(f"required image absent from download manifest: {name}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    errors = validate_layout_sources()
    try:
        table = json.loads(TABLE.read_text(encoding="utf-8"))
        errors.extend(validate_layout(table, board_capacities(BOARD)))
        if args.build_dir:
            errors.extend(validate_images(table, args.build_dir))
    except (OSError, ValueError, KeyError, TypeError) as exc:
        errors.append(f"invalid or missing layout/build metadata: {exc}")
    for error in errors:
        print(f"LAYOUT ERROR: {error}")
    if errors:
        return 1
    print("iwatch layout checks passed" + ("; images and download addresses checked" if args.build_dir else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
