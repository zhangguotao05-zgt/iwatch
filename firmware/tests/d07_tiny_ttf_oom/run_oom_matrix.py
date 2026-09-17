"""逐个字体分配点注入一次失败，并检查回滚和重复生命周期。"""
import argparse
import re
import subprocess
from pathlib import Path


STAGES = ("create", "metadata", "bitmap")


def run_case(executable, font, stage, number, timeout=10):
    result = subprocess.run([str(executable), str(font), stage, str(number)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", timeout=timeout)
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
    print("TINY_TTF OOM OK: {} failure points; {} lifecycle loops; {}; {}; {}".format(
        total, repeat_count, compatibility, repeat, eviction))


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
