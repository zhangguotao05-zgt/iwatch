"""复核 V00 字体样张、设计规格、资源文件及归档哈希。"""

import hashlib
import json
import math
import re
from pathlib import Path
from fontTools.ttLib import TTFont


ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "docs/assets/v00-typography-v1/typography.json"
ASSETS = ROOT / "docs/ui/assets/v00/font-specimens"
LIMIT = 131072


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check():
    spec = json.loads(SPEC.read_text(encoding="utf-8"))
    roles = {item["id"]: item for item in spec["roles"]}
    records = {item["id"]: item for item in spec["records"]}
    boards = []
    for directory, mode in ((ASSETS, "design"), (ASSETS / "candidate", "candidate"),
                            (ASSETS / "color", "color"), (ASSETS / "battery32", "battery32")):
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
        assert manifest["mode"] == mode
        assert manifest["status"] == "candidate_only_not_approved"
        assert manifest["typography_package"] == spec["package_id"]
        assert len(manifest["boards"]) == (1 if mode == "battery32" else 11)
        assert sum(len(item["samples"]) for item in manifest["boards"]) == (
            6 if mode == "battery32" else 35)
        for item in manifest["boards"]:
            role = roles[item["role"]]
            assert not role["firmware_approved"]
            assert item["weight"] == role["design_weight"]
            assert item["design_size_px"] == role["design_size_px"]
            assert item["design_tracking_px"] == role["tracking_px"]
            picture = directory / item["filename"]
            assert picture.exists() and digest(picture) == item["sha256"]
            if mode in ("color", "battery32"):
                design = records[item["reference"]]["design"]
                style = item["browser_reference_style"]
                assert manifest["reference_renderer"] == (
                    "DOM/CSS font-variant-numeric:tabular-nums, RGB565 quantized")
                assert style["numericVariant"] == design["numeric_variant"] == "tabular-nums"
                assert style["fontFamily"] == design["css_font_family"]
                assert style["fontWeight"] == str(design["weight"])
                assert style["fontSize"] == f'{design["size_px"]}px'
                assert style["color"] == design["color"]
                assert style["background"] == "rgb(23, 24, 28)"
                assert abs(style["baseline"] - 82) < 0.01
                expected_fonts = {(font["familyName"], font["postScriptName"])
                                  for font in design["reported_browser_fonts"]}
                actual_fonts = {(font["familyName"], font["postScriptName"])
                                for font in item["browser_platform_fonts"]}
                assert actual_fonts == expected_fonts
            boards.append(item)
    samples = {sample["text"] for board in boards for sample in board["samples"]}
    assert set(spec["required_test_strings"]).issubset(samples)
    budget = json.loads((ASSETS / "subsets/budget.json").read_text(encoding="utf-8"))
    total = budget["legacy_font_bytes"]
    postscript_names = set()
    for item in budget["subsets"]:
        file = ROOT / item["file"]
        assert file.stat().st_size == item["bytes"]
        assert digest(file) == item["sha256"]
        with TTFont(file) as font:
            assert "fvar" not in font
            assert font["OS/2"].usWeightClass == item["weight"]
            assert font["name"].getDebugName(6) == item["postscript_name"]
            postscript_names.add(item["postscript_name"])
        charset = set(item["characters"])
        if item["weight"] in (400, 500, 600):
            assert set("0123456789未知不可用加载中错误失败未接入-:%").issubset(charset)
        if item["weight"] == 600:
            assert set("周一二三四五六日天星期").issubset(charset)
        total += item["bytes"]
    assert len(postscript_names) == 4
    assert total == budget["total_with_legacy_bytes"]
    assert LIMIT - total == budget["headroom_bytes"]
    assert budget["fits_limit"] and total <= LIMIT
    parity = (ASSETS / "subset-parity.log").read_text(encoding="utf-8").splitlines()
    assert len(parity) == 35 and all(line.endswith(" MATCH") for line in parity)
    page_log = (ASSETS / "page-baselines.log").read_text(encoding="utf-8").splitlines()
    assert len(page_log) == 17
    expected_indices = {0, 1, 2, 3, 4, 5, 11, 13, 16, 19, 20, 21, 22, 23, 30, 31, 34}
    seen_indices = set()
    for line in page_log:
        assert "RGB565_COMPARE " in line and "missing=0 extra=0 result=NO_CLIP" in line
        assert line.endswith(" NO_CLIP")
        record_id = line.split(" ", 1)[0]
        record = records[record_id]
        index = int(re.search(r"\bindex=(\d+)", line).group(1))
        assert index in expected_indices and index not in seen_indices
        seen_indices.add(index)
        baseline = int(re.search(r"\bbaseline=(\d+)", line).group(1))
        assert baseline == round(record["design"]["baseline_y"])
        x, y, width, height = map(int, re.search(r"\bcontainer=([\d,]+)", line).group(1).split(","))
        bounds = record["design"]["text_bounds"]
        expected_x = {"Faces.modular:e004": 185, "System.control:e012": 37}.get(
            record_id, round(bounds["x"]))
        expected_width = {"Faces.modular:e004": 190, "System.control:e012": 140,
                          "Timers.home:e007": 126, "Settings.display:e020": 105,
                          "Timers.home:e006": 80}.get(record_id, math.ceil(bounds["width"]))
        assert x == expected_x and width == expected_width
        assert height == math.ceil(bounds["height"])
        assert 0 <= x < 390 and x + width <= 390 and 0 <= y < 450 and y + height <= 450
    assert seen_indices == expected_indices
    geometry = json.loads((ASSETS / "page-geometry/manifest.json").read_text(encoding="utf-8"))
    assert len(geometry["cases"]) == 7
    for item in geometry["cases"]:
        assert item["no_clip"]
        assert digest(ASSETS / "page-geometry" / item["filename"]) == item["sha256"]
    print(f"V00 FONT ARCHIVE OK: 34 boards, 111 samples, 35 subset parity, "
          f"17 page placements, {total} B TTF")


if __name__ == "__main__":
    check()
