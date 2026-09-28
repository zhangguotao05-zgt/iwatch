"""在隔离副本核对触摸采样/SDK 抽取依赖，不修改候选或真实 SDK。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

from test_configure_dependencies import SDK_CASES, SDK_SUPPORT_FILES


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    repo, sdk, build = out / 'repo', out / 'sdk', out / 'build'
    commands, protected, results = [], {}, []

    def run(argv):
        result = subprocess.run([str(x) for x in argv], cwd=root, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=120)
        commands.append({'command': [str(x) for x in argv], 'exit_code': result.returncode,
                         'stdout': result.stdout, 'stderr': result.stderr})
        (out / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        result.check_returncode()
        return result.stdout

    paths = run(['git', '-c', 'core.quotepath=false', 'ls-files', 'firmware']).splitlines()
    paths += ['firmware/tests/d07_tiny_ttf_oom/touch_invalid_cases.inc']
    for relative in set(paths):
        source, target = root / relative, repo / relative
        if not source.is_file():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        protected[str(source)] = sha(source)
    shutil.copytree(args.sdk / 'external/lvgl_v9/src', sdk / 'external/lvgl_v9/src')
    for relative in {p for p, _, _ in SDK_CASES} | set(SDK_SUPPORT_FILES):
        source, target = args.sdk / relative, sdk / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        protected[str(source)] = sha(source)
    override = out / 'override_touch.c'
    shutil.copy2(repo / 'firmware/iwatch/src/platform/iw_drv_touch.c', override)
    run(['cmake', '-S', repo / 'firmware/tests/d07_tiny_ttf_oom', '-B', build, '-G', 'Ninja',
         f'-DSDK_ROOT={sdk.as_posix()}', f'-DIW_TOUCH_DRIVER_SOURCE={override.as_posix()}'])
    cases = [
        (override, 'static void touch_invalid_sample(void)\n{', 'touch_driver_under_test.inc'),
        (override, 'msg = (struct touch_message){0, 0, TOUCH_EVENT_NONE};', 'touch_sampler_under_test.inc'),
        (sdk / SDK_SUPPORT_FILES[0], 'static rt_err_t read_regs(rt_uint8_t reg, rt_uint8_t len, rt_uint8_t *buf)\n{', 'ft6146_regs_under_test.inc'),
        (sdk / SDK_SUPPORT_FILES[0], 'static rt_err_t read_point(touch_msg_t p_msg)\n{', 'ft6146_point_under_test.inc'),
    ]
    for index, (source, anchor, generated) in enumerate(cases):
        before = sha(build / generated)
        token = f'TOUCH_DEPENDENCY_PROBE_{index}'
        original = source.read_text(encoding='utf-8')
        assert anchor in original
        time.sleep(0.05)
        source.write_text(original.replace(anchor, anchor + f'\n/* {token} */', 1), encoding='utf-8')
        run(['cmake', '--build', build, '--target', 'build.ninja'])
        refreshed = sha(build / generated) != before and token in (build / generated).read_text(encoding='utf-8')
        results.append({'source': str(source), 'generated': generated, 'refreshed': refreshed})
        assert refreshed
    unchanged = all(sha(Path(path)) == digest for path, digest in protected.items())
    assert unchanged
    (out / 'report.json').write_text(json.dumps({'status': 'passed', 'results': results,
        'original_inputs_unchanged': unchanged, 'original_sha256': protected}, indent=2), encoding='utf-8')
    print(json.dumps({'status': 'passed', 'cases': len(results), 'protected_files': len(protected)}))


if __name__ == '__main__':
    main()
