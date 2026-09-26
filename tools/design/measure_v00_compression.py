"""离线测量 V00 RGB565 资源压缩率；不产生可刷入的资源包。"""
import hashlib
import json
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "docs/assets/v00-design-handoff"
MANIFEST = PACKAGE / "manifest.json"
OUTPUT = ROOT / "docs/ui/assets/v00/compression-study.json"
IMAGE_LIMIT = 1_048_576
EXISTING_GCC_IMAGES = 866_577


def main():
    source = json.loads(MANIFEST.read_text(encoding="utf-8"))
    entries = []
    for item in source["resources"]:
        encoding = "rgb565" if item["role"] == "optional_background" else "rgb565a8"
        specification = item["encodings"][encoding]
        payload = (PACKAGE / specification["path"]).read_bytes()
        if (len(payload) != specification["bytes"] or
                hashlib.sha256(payload).hexdigest() != specification["sha256"]):
            raise ValueError(f"资源清单与原始数据不一致：{item['id']}")
        packed = zlib.compress(payload, 9)
        if zlib.decompress(packed) != payload:
            raise ValueError(f"压缩往返失败：{item['id']}")
        entries.append({"id": item["id"], "kind": item["kind"],
                        "encoding": encoding, "raw_bytes": len(payload),
                        "zlib_9_bytes": len(packed),
                        "raw_sha256": specification["sha256"],
                        "packed_sha256": hashlib.sha256(packed).hexdigest()})
    foreground = [entry for entry in entries if entry["encoding"] == "rgb565a8"]
    packed_total = sum(entry["zlib_9_bytes"] for entry in entries)
    report = {
        "scope": "仅主机压缩与完整往返；不包含目标端解码器、索引、对齐和缓存",
        "release_allowed": False,
        "resource_count": len(entries),
        "foreground_raw_bytes": sum(entry["raw_bytes"] for entry in foreground),
        "foreground_zlib_9_bytes": sum(entry["zlib_9_bytes"] for entry in foreground),
        "all_raw_bytes": sum(entry["raw_bytes"] for entry in entries),
        "all_zlib_9_bytes": packed_total,
        "image_budget_bytes": IMAGE_LIMIT,
        "existing_gcc_image_bytes": EXISTING_GCC_IMAGES,
        "theoretical_remaining_bytes": IMAGE_LIMIT - EXISTING_GCC_IMAGES - packed_total,
        "max_single_decoded_bytes": max(entry["raw_bytes"] for entry in entries),
        "target_decoder_tested": False,
        "target_peak_ram_tested": False,
        "entries": entries,
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                      encoding="utf-8")
    print(f"V00 COMPRESSION STUDY OK: {len(entries)} entries, "
          f"{packed_total} B packed, {report['theoretical_remaining_bytes']} B theoretical headroom")


if __name__ == "__main__":
    main()
