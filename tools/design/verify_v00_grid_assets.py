"""核对蜂窝主机样片的素材顺序、哈希、许可和既有图片预算。"""
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "docs/assets/v00-design-handoff"
OUTPUT = ROOT / "docs/ui/assets/v00/grid-review/resource-ledger.json"
FULL_OUTPUT = ROOT / "docs/ui/assets/v00/full-review/resource-ledger.json"
LOADER = ROOT / "firmware/tests/d07_tiny_ttf_oom/test_v00_assets.c"
BASELINE = ROOT / "work/v00/gcc/resource_budget.json"
LIMIT = 1_048_576


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    manifest = json.loads((PACKAGE / "manifest.json").read_text(encoding="utf-8"))
    grid = json.loads((PACKAGE / "specs/System.grid.json").read_text(encoding="utf-8"))
    resources = {item["id"]: item for item in manifest["resources"]}
    placements = grid["resource_placements"]
    names = re.findall(r'\{"([^"]+)",\s*(\d+),\s*(\d+)\}',
                       LOADER.read_text(encoding="utf-8"))
    if len(placements) != 17 or len(names) != len(manifest["resources"]) != 38:
        raise ValueError("设计资源数量或主机加载表不一致")
    for index, (name, width, height) in enumerate(names):
        asset = manifest["resources"][index]
        if (asset["id"] != name or asset["width"] != int(width) or
                asset["height"] != int(height)):
            raise ValueError(f"加载表第 {index} 项与设计包不一致")
        encoding = "rgb565" if index == 37 else "rgb565a8"
        encoded = asset["encodings"][encoding]
        raw = (PACKAGE / encoded["path"]).read_bytes()
        if len(raw) != encoded["bytes"] or digest(raw) != encoded["sha256"]:
            raise ValueError(f"第 {index} 项像素载荷校验失败")
    selected = []
    for index, (placement, (name, width, height)) in enumerate(zip(placements, names)):
        asset = resources[placement["asset_id"]]
        if asset["id"] != name or asset["width"] != int(width) or width != height:
            raise ValueError(f"图标 {index + 1} 的名称或尺寸与设计包不一致")
        encoded = asset["encodings"]["rgb565a8"]
        raw = (PACKAGE / encoded["path"]).read_bytes()
        if len(raw) != encoded["bytes"] or digest(raw) != encoded["sha256"]:
            raise ValueError(f"图标 {index + 1} 的像素载荷校验失败")
        if asset["release_allowed"]:
            raise ValueError("设计包许可状态异常")
        selected.append({
            "index": index + 1, "asset_id": asset["id"], "source_url": asset["source_url"],
            "source_sha256": asset["source_sha256"], "raw_path": encoded["path"],
            "raw_sha256": encoded["sha256"], "bytes": encoded["bytes"],
            "x": placement["x"], "y": placement["y"], "size": int(width),
            "release_allowed": False,
        })
    image_baseline = json.loads(BASELINE.read_text(encoding="utf-8"))["resources"]["image_bytes"]
    raw_bytes = sum(item["bytes"] for item in selected)
    png_bytes = sum(resources[item["asset_id"]]["encodings"]["png"]["bytes"]
                    for item in selected)
    result = {
        "schema": 1, "design_package_sha256": digest((PACKAGE / "manifest.json").read_bytes()),
        "page": "System.grid", "selected_format": "RGB565+A8, raw without LVGL header",
        "source": "Apple 官方公开参考图；未获再分发许可，仅用于本地主机对照",
        "target_firmware_embedded": False,
        "image_budget_bytes": LIMIT, "prior_gcc_image_bytes": image_baseline,
        "prior_gcc_baseline_head": "cf38028ec5836192572ef135a742bdc22c73cef7",
        "remaining_before_grid_bytes": LIMIT - image_baseline,
        "grid_raw_bytes": raw_bytes, "raw_over_budget_bytes":
            max(0, image_baseline + raw_bytes - LIMIT),
        "grid_png_bytes": png_bytes,
        "png_decoder_enabled_for_target": False,
        "png_budget_fit_is_not_implementation_approval": True,
        "icons": selected,
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    foreground = manifest["resources"][:-1]
    foreground_bytes = sum(item["encodings"]["rgb565a8"]["bytes"]
                           for item in foreground)
    background_bytes = manifest["resources"][-1]["encodings"]["rgb565"]["bytes"]
    font_path = ROOT / "firmware/iwatch/src/resource/fonts/DroidSansFallback.ttf"
    full = {
        "schema": 1,
        "design_package_sha256": result["design_package_sha256"],
        "pages": ["System.grid", "Faces.modular", "Timers.home", "Alarms.edit",
                  "Settings.display", "System.control"],
        "host_only": True,
        "release_allowed": False,
        "resource_count": len(names),
        "unique_foreground_count": len(foreground),
        "reference_icon_count": sum(item["kind"] == "apple_reference"
                                    for item in foreground),
        "project_vector_count": sum(item["kind"] == "project_vector"
                                    for item in foreground),
        "foreground_rgb565a8_bytes": foreground_bytes,
        "control_background_rgb565_bytes": background_bytes,
        "image_budget_bytes": LIMIT,
        "prior_gcc_image_bytes": image_baseline,
        "remaining_before_v00_bytes": LIMIT - image_baseline,
        "foreground_over_budget_bytes":
            max(0, image_baseline + foreground_bytes - LIMIT),
        "with_background_over_budget_bytes":
            max(0, image_baseline + foreground_bytes + background_bytes - LIMIT),
        "target_firmware_embedded_bytes": 0,
        "font_candidate_path": str(font_path.relative_to(ROOT)).replace("\\", "/"),
        "font_candidate_sha256": digest(font_path.read_bytes()),
        "font_candidate_bytes": font_path.stat().st_size,
        "font_file_modified_for_five_pages": False,
        "new_runtime_font_size_px": 48,
        "font_cache_peak_pending_target_measurement": True,
        "assets": [{"index": i, "id": item["id"], "kind": item["kind"],
                    "width": item["width"], "height": item["height"],
                    "bytes": item["encodings"]["rgb565" if i == 37 else
                                                "rgb565a8"]["bytes"],
                    "release_allowed": item["release_allowed"]}
                   for i, item in enumerate(manifest["resources"])],
    }
    FULL_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    FULL_OUTPUT.write_text(json.dumps(full, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(f"V00 GRID ASSETS OK: {len(selected)} icons, {raw_bytes} raw bytes, "
          f"{result['raw_over_budget_bytes']} over existing image budget")
    print(f"V00 SIX PAGES ASSETS OK: {len(foreground)} foreground graphics, "
          f"{foreground_bytes} bytes, {full['foreground_over_budget_bytes']} "
          "over existing budget")


if __name__ == "__main__":
    main()
