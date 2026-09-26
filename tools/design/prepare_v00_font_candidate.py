"""将已获许可的外部字体缩成 V00 评审候选；产物仅放入本地 work。"""
import argparse
import hashlib
import json
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "firmware/iwatch/src/resource/fonts/font_charset.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--weights", type=int, nargs="+", default=(300, 400, 600))
    args = parser.parse_args()
    import sys
    sys.path.insert(0, str(ROOT / "firmware"))
    import generate_font_subset

    configuration = json.loads(CONFIG.read_text(encoding="utf-8"))
    charset, _ = generate_font_subset.collect_charset(configuration)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    results = []
    for weight in args.weights:
        if weight < 100 or weight > 900:
            raise ValueError("字重超出源字体支持的范围")
        font = TTFont(args.source, recalcTimestamp=False)
        options = subset.Options()
        options.name_IDs = [1, 2, 4, 6, 16, 17]
        options.recalc_timestamp = False
        selection = subset.Subsetter(options=options)
        selection.populate(unicodes=[ord(character) for character in charset])
        selection.subset(font)
        if "fvar" in font:
            font = instantiateVariableFont(font, {"wght": weight}, inplace=True)
        destination = args.output_dir / f"NotoSansSC-review-{weight}.ttf"
        font.save(destination, reorderTables=False)
        payload = destination.read_bytes()
        results.append({"weight": weight, "bytes": len(payload),
                        "sha256": hashlib.sha256(payload).hexdigest(),
                        "glyphs": len(font.getBestCmap())})
        font.close()
    print(json.dumps({"source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
                      "source": str(args.source), "candidates": results}, ensure_ascii=False))


if __name__ == "__main__":
    main()
