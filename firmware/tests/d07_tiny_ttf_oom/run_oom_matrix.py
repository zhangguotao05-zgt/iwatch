"""逐个字体分配点注入一次失败，并检查回滚和重复生命周期。"""
import argparse
import re
import subprocess
from pathlib import Path


STAGES = ("create", "metadata", "bitmap", "epic", "registry_oom", "registry_epic")
ROOT = Path(__file__).resolve().parents[3]


def run_case(executable, font, stage, number, timeout=10):
    result = subprocess.run([str(executable), str(font), stage, str(number)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", timeout=timeout, cwd=ROOT)
    output = result.stdout.strip()
    if result.returncode:
        raise ValueError("{}/{} failed ({}): {}".format(stage, number, result.returncode, output))
    if "asserts=0" not in output or "result=ok" not in output:
        raise ValueError("{}/{} produced invalid evidence: {}".format(stage, number, output))
    return output


def run_matrix(executable, font, repeat_count):
    compatibility = run_case(executable, font, "compat", 0)
    total = 0
    for stage in STAGES:
        probe = run_case(executable, font, stage, 0)
        match = re.search(r"\ballocations=(\d+)\b", probe)
        if match is None or int(match.group(1)) <= 0:
            raise ValueError("{} probe did not report allocations: {}".format(stage, probe))
        allocation_count = int(match.group(1))
        for failure_index in range(1, allocation_count + 1):
            output = run_case(executable, font, stage, failure_index)
            if "oom=1" not in output:
                raise ValueError("{}/{} did not latch OOM: {}".format(stage, failure_index, output))
            total += 1
    repeat = run_case(executable, font, "repeat", repeat_count, timeout=60)
    eviction = run_case(executable, font, "evict", 10, timeout=30)
    pixels = run_case(executable, font, "pixels", 2, timeout=60)
    registry = run_case(executable, font, "registry", repeat_count, timeout=60)
    print("TINY_TTF OOM OK: {} failure points; {} lifecycle loops; {}; {}; {}; {}".format(
        total, repeat_count, compatibility, repeat, eviction, pixels))
    print("FONT REGISTRY AND THEME OK: " + registry)
    component_points = 0
    for kind in range(5):
        for operation in ("create", "draw"):
            stage = "component_{}_{}".format(operation, kind)
            probe = run_case(executable, font, stage, 0)
            count = int(re.search(r"\ballocations=(\d+)\b", probe).group(1))
            if count <= 0:
                raise ValueError("component probe has no allocation points: " + stage)
            for point in range(1, count + 1):
                run_case(executable, font, stage, point)
            component_points += count
            print("COMPONENT OOM OK: {} / {} points".format(stage, count))
    components = run_case(executable, font, "components", repeat_count, timeout=60)
    print("COMPONENTS OK: {} failure points; {}".format(component_points, components))
    fill_points = 0
    for stage in ("component_fill", "component_fill_busy"):
        probe = run_case(executable, font, stage, 0)
        count = int(re.search(r"\ballocations=(\d+)\b", probe).group(1))
        for point in range(1, count + 1):
            run_case(executable, font, stage, point)
        fill_points += count
    print("ROUNDED FILL OOM OK: {} real renderer failure points".format(fill_points))
    print(run_case(executable, font, "component_navigation", repeat_count, timeout=60))
    print(run_case(executable, font, "component_gallery", repeat_count, timeout=60))
    print(run_case(executable, font, "component_touch", repeat_count, timeout=60))
    for mode in range(5):
        print(run_case(executable, font, "component_render", mode))
    for variant in range(32):
        print(run_case(executable, font, "component_gallery_render", variant))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--repeat", type=int, default=1000)
    args = parser.parse_args()
    try:
        if args.repeat <= 0:
            raise ValueError("repeat must be positive")
        run_matrix(args.executable.resolve(), args.font.resolve(), args.repeat)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("TINY_TTF OOM ERROR: {}".format(error))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
