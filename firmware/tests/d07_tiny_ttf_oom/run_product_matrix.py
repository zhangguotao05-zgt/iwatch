"""D10 真实 View 的构造、绘制失败矩阵与同源画面。"""
from pathlib import Path
import argparse
import concurrent.futures
import re
from run_oom_matrix import run_case

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--font', type=Path, required=True)
    args = parser.parse_args()
    executable, font = args.executable.resolve(), args.font.resolve()
    for stage in ('component_product_oom', 'component_product_draw_oom',
                  'component_product_line_0', 'component_product_line_1', 'component_product_line_2'):
        probe = run_case(executable, font, stage, 0)
        count = int(re.search(r'allocations=(\d+)', probe).group(1))
        if count < 1:
            raise ValueError('分配探针为空')
        # 每个失败点使用独立进程，不共享 LVGL 全局对象与注入器。
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            jobs = [pool.submit(run_case, executable, font, stage, point) for point in range(1, count + 1)]
            for job in jobs:
                job.result()
        print(f'{stage}: {count} failure points passed', flush=True)
    print(run_case(executable, font, 'component_product', 1000, timeout=60), flush=True)
    print(run_case(executable, font, 'component_product_controller', 1000, timeout=60), flush=True)
    print(run_case(executable, font, 'component_product_router', 1000, timeout=60), flush=True)
    print(run_case(executable, font, 'component_product_input', 0), flush=True)
    print(run_case(executable, font, 'component_product_boundaries', 0), flush=True)
    for variant in range(56):
        print(run_case(executable, font, 'component_product_render', variant), flush=True)
    for variant in range(36):
        print(run_case(executable, font, 'component_d11_render', variant), flush=True)

if __name__ == '__main__':
    main()
