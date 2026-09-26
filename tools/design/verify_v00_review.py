"""核对 V00 主机帧、字体和对照图的归档身份。"""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "docs/assets/v00-design-handoff/screens"
IDS = (
    "System.grid", "Faces.modular", "Timers.home", "Alarms.edit",
    "Settings.display", "System.control", "Timers.home.scroll-end",
    "Settings.display.scroll-end",
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def png_size(path: Path) -> tuple[int, int]:
    header = path.read_bytes()[:24]
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"PNG 头无效: {path}")
    return struct.unpack(">II", header[16:24])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-dir", type=Path, required=True)
    parser.add_argument("--compare-dir", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    args = parser.parse_args()
    host = {item["id"]: item for item in json.loads(
        (args.host_dir / "manifest.json").read_text(encoding="utf-8"))}
    compared = {item["id"]: item for item in json.loads(
        (args.compare_dir / "manifest.json").read_text(encoding="utf-8"))["samples"]}
    if set(host) != set(IDS) or set(compared) != set(IDS):
        raise ValueError("八张页面帧不完整或存在重复 ID")
    font_hash = digest(args.font)
    for page_id in IDS:
        source = TARGET / f"{page_id}.png"
        rendered = args.host_dir / f"{page_id}.png"
        packed = args.host_dir / "raw" / f"{page_id}.rgb565.zlib"
        overlay = args.compare_dir / f"{page_id}.png"
        if png_size(source) != (390, 450) or png_size(rendered) != (390, 450):
            raise ValueError(f"页面尺寸错误: {page_id}")
        if host[page_id]["rgb565_bytes"] != 390 * 450 * 2:
            raise ValueError(f"RGB565 帧长度错误: {page_id}")
        raw = zlib.decompress(packed.read_bytes())
        if len(raw) != 390 * 450 * 2 or \
                hashlib.sha256(raw).hexdigest() != host[page_id]["rgb565_sha256"] or \
                digest(packed) != host[page_id]["rgb565_zlib_sha256"]:
            raise ValueError(f"原始 RGB565 帧哈希不一致: {page_id}")
        if host[page_id]["png_sha256"] != digest(rendered) or \
                host[page_id]["font_sha256"] != font_hash or \
                compared[page_id]["target_sha256"] != digest(source) or \
                compared[page_id]["host_sha256"] != digest(rendered) or \
                compared[page_id]["comparison_sha256"] != digest(overlay):
            raise ValueError(f"页面或字体哈希不一致: {page_id}")
    print(f"V00 REVIEW EVIDENCE OK: {len(IDS)} frames, font {font_hash[:12]}")


if __name__ == "__main__":
    main()
