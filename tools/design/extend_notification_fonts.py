"""以现行 cmap 并集扩字，固定源哈希和预算；不覆盖历史字体批准记录。"""
import argparse
import hashlib
import json
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "firmware/iwatch/src/resource/fonts"
SOURCE_SHA = "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"
ADDITIONS = {
    400: "普通通知本机测试待处理提醒闹钟计时器暂无没有有新的通知时会显示在这里。普通通知将移除，闹钟和计时器提醒不会因此停止。通知已变化请重试时间未校准操作失败/ ",
    500: "通知没有通知清除所有通知？返回取消回复通知已变化请重试操作失败",
    600: "普通通知本机测试",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if sha(args.source) != SOURCE_SHA:
        raise ValueError("Pinned font source hash mismatch")
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for weight in (300, 400, 500, 600):
        name = f"NotoSansSC-v00-only-{weight}.ttf"
        current = FONTS / name
        with TTFont(current) as previous:
            old_codes = set(previous.getBestCmap())
        codes = old_codes | set(map(ord, ADDITIONS.get(weight, "")))
        target = args.output / name
        if codes == old_codes:
            target.write_bytes(current.read_bytes())
        else:
            font = TTFont(args.source, recalcTimestamp=False)
            options = subset.Options()
            options.name_IDs = [1, 2, 4, 6, 16, 17]
            options.recalc_timestamp = False
            worker = subset.Subsetter(options=options)
            worker.populate(unicodes=sorted(codes))
            worker.subset(font)
            font = instantiateVariableFont(font, {"wght": weight}, inplace=True)
            style = {400: "Regular", 500: "Medium", 600: "SemiBold"}[weight]
            names = font["name"]
            names.names = [entry for entry in names.names if entry.nameID not in (1, 2, 4, 6, 16, 17)]
            values = {1: "IW V00 Sans", 2: style, 4: f"IW V00 Sans {style}",
                      6: f"IWV00Sans-{style}", 16: "IW V00 Sans", 17: style}
            for key, value in values.items():
                names.setName(value, key, 3, 1, 0x409)
                names.setName(value, key, 1, 0, 0)
            if codes - set(font.getBestCmap()):
                raise ValueError("Required glyph missing")
            font.save(target, reorderTables=False)
            font.close()
        rows.append({"weight": weight, "file": name, "previous_sha256": sha(current),
                     "sha256": sha(target), "bytes": target.stat().st_size,
                     "previous_cmap_retained": True, "codepoints": sorted(codes)})
    total = (FONTS / "DroidSansFallback.ttf").stat().st_size + sum(row["bytes"] for row in rows)
    report = {"schema": 1, "source_sha256": SOURCE_SHA, "subsets": rows, "total_bytes": total,
              "legacy_sha256": sha(FONTS / "DroidSansFallback.ttf"),
              "license_sha256": sha(ROOT / "docs/ui/assets/v00/font-candidate/OFL.txt"),
              "limit_bytes": 131072, "fits_limit": total <= 131072,
              "historical_approval_modified": False, "applied": False}
    if args.apply and total <= 131072:
        for row in rows:
            (FONTS / row["file"]).write_bytes((args.output / row["file"]).read_bytes())
        report["applied"] = True
    (args.output / "budget.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if report["applied"]:
        (FONTS / "NotoSansSC-notifications.subset.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"total_bytes": total, "fits_limit": total <= 131072, "applied": report["applied"]}))
    return 0 if total <= 131072 else 1


if __name__ == "__main__":
    raise SystemExit(main())
