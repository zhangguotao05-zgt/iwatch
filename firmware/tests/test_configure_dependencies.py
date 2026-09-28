"""用实际 CMake/Ninja 工程验证抽取依赖；只修改独立副本，不运行产品测试。"""
import argparse
import datetime
import hashlib
import json
import shutil
import subprocess
import time
from pathlib import Path

# 每个条目指定源文件、抽取区内的锚点和真实生成文件；None 表示整文件抽取。
SDK_CASES = [
    ("middleware/lvgl/lv_drivers_v9/sifli/lv_draw_epic_label.c", None, "epic_label_under_test.inc"),
    ("rtos/rtthread/bsp/sifli/drivers/drv_epic_single.c", "rt_err_t drv_epic_cont_blend(", "epic_driver_under_test.inc"),
    ("middleware/app_fwk/app_schedule_port.c", "static const lv_obj_class_t route_screen_class", "route_screen_under_test.inc"),
    ("middleware/app_fwk/gui_app_fwk.c", "static rt_err_t send_msg_to_gui_app_task(", "sdk_send_under_test.inc"),
    ("middleware/app_fwk/app_schedule.c", "static rt_err_t app_create_page(", "sdk_create_under_test.inc"),
    ("middleware/app_fwk/app_schedule.c", "void app_schedule_route_snapshot(", "sdk_snapshot_under_test.inc"),
    ("middleware/lvgl/lv_drivers_v9/sifli/lv_draw_epic_img.c", "void lv_draw_epic_set_error_cb(", "epic_image_set_error_under_test.inc"),
    ("middleware/lvgl/lv_drivers_v9/sifli/lv_draw_epic_img.c", "bool lv_draw_epic_report_error(", "epic_image_report_error_under_test.inc"),
    ("middleware/lvgl/lv_drivers_v9/sifli/lv_draw_epic_img.c", "static void img_draw_core(lv_draw_task_t *draw_task, const lv_draw_image_dsc_t *draw_dsc,\n                          const lv_image_decoder_dsc_t *decoder_dsc, lv_draw_image_sup_t *sup,\n                          const lv_area_t *img_coords, const lv_area_t *clipped_img_area)\n{", "epic_image_core_under_test.inc"),
    ("middleware/lvgl/lv_drivers_v9/lvsf_img_decoder.c", "static lv_result_t decoder_open(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc)\n{", "ezip_decoder_under_test.inc"),
]
SOURCE_CASES = [
    ("gui_apps/component_gallery/iw_components_demo.c", None, "component_demo_under_test.inc"),
    ("gui_apps/watch_demo.c", "static uint32_t gui_process_frame(void)", "gui_frame_under_test.inc"),
    ("gui_apps/watch_demo.c", "static void input_cancel_lvgl(void)", "input_cancel_under_test.inc"),
    ("platform/iw_drv_touch.c", "static rt_size_t touch_read(", "touch_driver_under_test.inc"),
    ("platform/iw_touch_input.c", None, "touch_input_under_test.inc"),
    ("platform/iw_router.c", None, "router_under_test.inc"),
]
CMAKE_FILES = [f"firmware/tests/{name}/CMakeLists.txt" for name in ("d07_tiny_ttf_oom", "control_router")]
# 新触摸夹具的配置输入；本脚本仍只重放原 A1 判据，不把新 SDK 函数套入旧基线。
SDK_SUPPORT_FILES = ["customer/peripherals/touch_panel/ft6146/ft6146.c"]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    cmake = shutil.which("cmake")
    ninja = shutil.which("ninja")
    if not cmake or not ninja or not shutil.which("cl"):
        raise RuntimeError("Run in an existing MSVC developer environment with CMake and Ninja")
    commands = []

    def run(argv, cwd=root):
        result = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=120)
        record = {"command": [str(x) for x in argv], "cwd": str(cwd),
                  "exit_code": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
        commands.append(record)
        (out / "commands.json").write_text(json.dumps(commands, ensure_ascii=False, indent=2), encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"Command failed; see commands.json: {argv!r}")
        return result.stdout

    head = run(["git", "rev-parse", "HEAD"]).strip()
    baseline = run(["git", "rev-parse", args.baseline]).strip()
    versions = {"cmake": run([cmake, "--version"]), "ninja": run([ninja, "--version"])}
    tracked = run(["git", "ls-files", "firmware"]).splitlines()
    protected = {root / path for path in CMAKE_FILES}
    protected.update(root / "firmware/iwatch/src" / path for path, _, _ in SOURCE_CASES)
    protected.update(args.sdk / path for path, _, _ in SDK_CASES)
    protected.update(args.sdk / path for path in SDK_SUPPORT_FILES)
    protected.add(args.sdk / "middleware/app_fwk/gui_app_fwk.c")
    before_hashes = {str(path): sha(path) for path in protected}
    results = []
    for variant in ("before", "after"):
        for label, project, use_override in (
                ("d07_tiny_ttf_oom", "d07_tiny_ttf_oom", False),
                ("control_default", "control_router", False),
                ("control_override", "control_router", True)):
            workspace = out / variant / label
            repo = workspace / "repo"
            sdk = workspace / "sdk"
            for relative in tracked:
                source = root / relative
                if source.is_file():
                    destination = repo / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, destination)
            # TARGET 生成输入只复制既有文件，绝不覆盖之前的主机输出。
            shutil.copytree(root / "firmware/tests/v00_target_view/generated",
                            repo / "firmware/tests/v00_target_view/generated", dirs_exist_ok=True)
            shutil.copytree(args.sdk / "external/lvgl_v9/src", sdk / "external/lvgl_v9/src")
            for relative in {path for path, _, _ in SDK_CASES} | set(SDK_SUPPORT_FILES):
                destination = sdk / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(args.sdk / relative, destination)
            if variant == "before":
                for relative in CMAKE_FILES:
                    (repo / relative).write_text(run(["git", "show", f"{baseline}:{relative}"]), encoding="utf-8")
            build = workspace / "build"
            configure = [cmake, "-S", str(repo / "firmware/tests" / project), "-B", str(build),
                         "-G", "Ninja", f"-DSDK_ROOT={sdk.as_posix()}", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"]
            if project == "control_router":
                cases = [(sdk / "middleware/app_fwk/gui_app_fwk.c", "void gui_app_remove_page(", "sdk_remove.inc"),
                         (repo / "firmware/iwatch/src/gui_apps/watch_demo.c", "static bool input_signal(", "input_signal.inc")]
                if use_override:
                    override = workspace / "override_router.c"
                    shutil.copy2(repo / "firmware/iwatch/src/platform/iw_router.c", override)
                    configure.append(f"-DIW_ROUTER_SOURCE={override.as_posix()}")
                    cases.append((override, None, "legacy/router_under_test.inc"))
                else:
                    cases += [(repo / "firmware/iwatch/src" / path, marker, "legacy/" + output)
                              for path, marker, output in SOURCE_CASES
                              if output in ("gui_frame_under_test.inc", "touch_driver_under_test.inc", "router_under_test.inc")]
            else:
                cases = [(sdk / path, marker, output) for path, marker, output in SDK_CASES]
                cases += [(repo / "firmware/iwatch/src" / path, marker, output) for path, marker, output in SOURCE_CASES]
            run(configure)
            for index, (source, anchor, generated) in enumerate(cases):
                token = f"IW_CONFIGURE_DEPENDENCY_PROBE_{index}"
                text = source.read_text(encoding="utf-8")
                if anchor is None:
                    position = len(text)
                else:
                    start = text.index(anchor)
                    position = text.index("{", start) + 1
                # 注释位于真实抽取区内；签名和业务行为不变，仅修改隔离副本。
                text = text[:position] + f"\n/* {token} */\n" + text[position:]
                previous = sha(build / generated)
                time.sleep(0.05)
                source.write_text(text, encoding="utf-8")
                run([cmake, "--build", str(build), "--target", "build.ninja"])
                extracted = (build / generated).read_text(encoding="utf-8")
                refreshed = token in extracted and previous != sha(build / generated)
                results.append({"variant": variant, "project": label, "source": str(source),
                                "generated": str(build / generated), "token": token,
                                "before_sha256": previous, "after_sha256": sha(build / generated),
                                "assertion_extracted_content_updated": refreshed})
                print(f"{variant}/{label}/{generated}: refreshed={refreshed}", flush=True)
    unchanged = all(sha(Path(path)) == value for path, value in before_hashes.items())
    passed = unchanged and all(item["assertion_extracted_content_updated"] == (item["variant"] == "after")
                               for item in results)
    report = {"status": "passed" if passed else "failed", "head": head, "baseline": baseline,
              "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "versions": versions, "test_sha256": sha(Path(__file__)),
              "cmake_files_sha256": {path: sha(root / path) for path in CMAKE_FILES},
              "original_inputs_unchanged": unchanged, "original_input_sha256": before_hashes,
              "results": results, "product_runtime_test": False, "target_build": False}
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
