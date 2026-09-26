"""按设计字重生成最小 V00 文字子集，证明候选资源量，不改正式固件。"""

import argparse
import hashlib
import json
from pathlib import Path

from fontTools import subset
from fontTools import __version__ as fonttools_version
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "docs/assets/v00-typography-v1/typography.json"
LEGACY = ROOT / "firmware/iwatch/src/resource/fonts/DroidSansFallback.ttf"
LICENSE = ROOT / "docs/ui/assets/v00/font-candidate/OFL.txt"
LIMIT = 131072


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.source = args.source.resolve()
    args.output_dir = args.output_dir.resolve()
    spec = json.loads(SPEC.read_text(encoding="utf-8"))
    roles = {role["id"]: role for role in spec["roles"]}
    charsets = {300: set(), 400: set(), 500: set(), 600: set()}
    for record in spec["records"]:
        if record["rendering"] != "runtime_text":
            continue
        role = roles[record["role"]]
        charsets[role["design_weight"]].update(record["sample"])
    # 数字、星期与故障文案按运行值域闭包，不从少量截图样例反推字符集。
    digits = "0123456789"
    states = "未知不可用加载中错误失败未接入"
    for weight in (400, 500, 600):
        charsets[weight].update(digits + states + "-:%")
    charsets[400].update("文字大小显示与亮度全天候显示水锁输入设置")
    charsets[500].update("取消确认")
    charsets[600].update("周一二三四五六日天星期所有计时器")
    charsets[300].update(":")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    result = {"status": "host_budget_candidate_only", "limit_bytes": LIMIT,
              "generator": f"fontTools {fonttools_version}",
              "source_sha256": sha256(args.source), "legacy_font_sha256": sha256(LEGACY),
              "license_file": str(LICENSE.relative_to(ROOT)), "license_sha256": sha256(LICENSE),
              "legacy_font_bytes": LEGACY.stat().st_size, "subsets": []}
    total = LEGACY.stat().st_size
    for weight, charset in charsets.items():
        font = TTFont(args.source, recalcTimestamp=False)
        options = subset.Options()
        options.name_IDs = [1, 2, 4, 6, 16, 17]
        options.recalc_timestamp = False
        worker = subset.Subsetter(options=options)
        worker.populate(unicodes=sorted(ord(character) for character in charset))
        worker.subset(font)
        if "fvar" in font:
            font = instantiateVariableFont(font, {"wght": weight}, inplace=True)
        missing = charset - {chr(codepoint) for codepoint in font.getBestCmap()}
        if missing:
            raise ValueError(f"{weight} 字重缺字：{''.join(sorted(missing))}")
        # 派生资源使用独立名称，避免四个静态字重都继承源字体的 Thin 名称。
        style = {300: "Light", 400: "Regular", 500: "Medium", 600: "SemiBold"}[weight]
        family = "IW V00 Sans"
        names = font["name"]
        names.names = [entry for entry in names.names if entry.nameID not in (1, 2, 4, 6, 16, 17)]
        values = {1: family, 2: style, 4: f"{family} {style}",
                  6: f"IWV00Sans-{style}", 16: family, 17: style}
        for name_id, value in values.items():
            names.setName(value, name_id, 3, 1, 0x409)
            names.setName(value, name_id, 1, 0, 0)
        destination = args.output_dir / f"NotoSansSC-v00-only-{weight}.ttf"
        font.save(destination, reorderTables=False)
        entry = {"weight": weight, "characters": "".join(sorted(charset)),
                 "character_count": len(charset), "glyphs": len(font.getBestCmap()),
                 "os2_weight": font["OS/2"].usWeightClass,
                 "has_variable_axes": "fvar" in font,
                 "family_name": font["name"].getDebugName(1),
                 "postscript_name": font["name"].getDebugName(6),
                 "bytes": destination.stat().st_size, "sha256": sha256(destination),
                 "file": str(destination.relative_to(ROOT))}
        result["subsets"].append(entry)
        total += entry["bytes"]
        font.close()
    result["total_with_legacy_bytes"] = total
    result["headroom_bytes"] = LIMIT - total
    result["fits_limit"] = total <= LIMIT
    manifest = args.output_dir / "budget.json"
    manifest.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"total": total, "headroom": LIMIT-total,
                      "subsets": [(item["weight"], item["bytes"]) for item in result["subsets"]]}))


if __name__ == "__main__":
    main()
