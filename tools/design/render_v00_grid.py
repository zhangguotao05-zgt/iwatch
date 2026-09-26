"""把真实 LVGL 主机 RGB565 帧转换为蜂窝单页复核图。"""
import hashlib
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "firmware/tests/build/d07-a0/gallery-600-0.rgb565"
OUTPUT = ROOT / "docs/ui/assets/v00/grid-review"
RENDERER = ROOT / "firmware/tests/render_product_gallery.py"


def main():
    specification = importlib.util.spec_from_file_location("product_gallery", RENDERER)
    renderer = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(renderer)
    raw = RAW.read_bytes()
    if len(raw) != 390 * 450 * 2:
        raise ValueError("蜂窝主机帧不是 390×450 RGB565")
    png = renderer.png(raw)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "host.png").write_bytes(png)
    manifest = {
        "page": "System.grid", "source": str(RAW.relative_to(ROOT)).replace("\\", "/"),
        "raw_bytes": len(raw), "raw_sha256": hashlib.sha256(raw).hexdigest(),
        "host_png_sha256": hashlib.sha256(png).hexdigest(),
        "renderer": "真实 iw_product_view + LVGL RGB565 主机绘制",
    }
    (OUTPUT / "host-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"V00 GRID HOST OK: {manifest['raw_sha256']}")


if __name__ == "__main__":
    main()
