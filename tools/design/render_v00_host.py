"""将 V00 真实 RGB565 主机帧转换为可审阅 PNG。"""
import argparse
import hashlib
import importlib.util
import json
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "firmware/tests/render_product_gallery.py"
RAW = ROOT / "firmware/tests/build/d07-a0"
OUTPUT = ROOT / "docs/ui/assets/v00/host"
PAGES = ("System.grid", "Faces.modular", "Timers.home", "Alarms.edit",
         "Settings.display", "System.control")
SCROLL_ENDS = (("Timers.home.scroll-end", 702),
               ("Settings.display.scroll-end", 704))
RUNTIME_PAGES = (("System.grid.runtime", 607),
                 ("Faces.modular.runtime", 608),
                 ("Timers.home.runtime", 609),
                 ("Alarms.edit.runtime", 610),
                 ("Settings.display.runtime", 611))
RUNTIME_SCROLL_ENDS = (("Timers.home.scroll-end.runtime", 709),
                       ("Alarms.edit.scroll-end.runtime", 710),
                       ("Settings.display.scroll-end.runtime", 711))
RUNTIME_STATES = (("System.grid.zoom-in.runtime", 807),
                  ("System.grid.zoom-out.runtime", 817),
                  ("Alarms.edit.minute-46.runtime", 810))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=OUTPUT)
    parser.add_argument("--font-path", type=Path)
    parser.add_argument("--runtime", action="store_true")
    args = parser.parse_args()
    font_hash = hashlib.sha256(args.font_path.read_bytes()).hexdigest() if args.font_path else None
    specification = importlib.util.spec_from_file_location("product_gallery", RENDERER)
    renderer = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(renderer)
    frames = ([(name, (RAW / f"gallery-{number}-0.rgb565").read_bytes())
               for name, number in (*RUNTIME_PAGES, *RUNTIME_SCROLL_ENDS,
                                    *RUNTIME_STATES)]
              if args.runtime else
              [(name, (RAW / f"gallery-{600 + index}-0.rgb565").read_bytes())
               for index, name in enumerate(PAGES)] +
              [(name, (RAW / f"gallery-{number}-0.rgb565").read_bytes())
               for name, number in SCROLL_ENDS])
    args.output_dir.mkdir(parents=True, exist_ok=True)
    raw_output = args.output_dir / "raw"
    raw_output.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, data in frames:
        rendered = renderer.png(data)
        packed = zlib.compress(data, 9)
        (args.output_dir / f"{name}.png").write_bytes(rendered)
        (raw_output / f"{name}.rgb565.zlib").write_bytes(packed)
        manifest.append({"id": name,
                         "rgb565_sha256": hashlib.sha256(data).hexdigest(),
                         "rgb565_zlib_sha256": hashlib.sha256(packed).hexdigest(),
                         "png_sha256": hashlib.sha256(rendered).hexdigest(),
                         "font_sha256": font_hash,
                         "rgb565_bytes": len(data), "png_bytes": len(rendered)})
    (args.output_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"V00 HOST FRAMES OK: {len(manifest)} RGB565 frames")


if __name__ == "__main__":
    main()
