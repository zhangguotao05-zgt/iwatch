"""转换通知真实 TARGET View 捕获帧；只变换编码，不重画像素。"""
import argparse
import hashlib
import json
from pathlib import Path
from render_product_gallery import png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    states = {110: "empty", 111: "full32-top", 112: "confirm", 113: "full32-bottom",
              114: "detail-top", 115: "detail-bottom", 116: "cleared-empty",
              117: "critical-only", 118: "critical-full32", 119: "critical-after-clear",
              120: "long-detail-top", 121: "long-detail-bottom"}
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for number, state in states.items():
        source = args.input / f"gallery-{number}-0.rgb565"
        raw = source.read_bytes()
        encoded = png(raw)
        target = args.output / f"{state}.png"
        target.write_bytes(encoded)
        rows.append({"state": state, "file": target.name,
                     "rgb565_sha256": hashlib.sha256(raw).hexdigest(),
                     "png_sha256": hashlib.sha256(encoded).hexdigest()})
    (args.output / "manifest.json").write_text(json.dumps({"evidence": "生产 TARGET View 主机软件帧，不是硬件验证", "frames": rows},
                                                       ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
