# -*- coding: utf-8 -*-
"""将逐行 C 生成器与控制中心批准背景逐像素对照。"""

import argparse
import ctypes
import json
import statistics
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs/assets/v00-design-handoff/assets/png/control-background-only.png"
OUTPUT = ROOT / "work/v00/resource-bridge/background"
WIDTH, HEIGHT = 390, 450


def expand565(value):
    red = (value >> 11) & 31
    green = (value >> 5) & 63
    blue = value & 31
    return ((red << 3) | (red >> 2), (green << 2) | (green >> 4),
            (blue << 3) | (blue >> 2))


def pack565(color):
    return ((color[0] >> 3) << 11) | ((color[1] >> 2) << 5) | (color[2] >> 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    args = parser.parse_args()
    library = ctypes.CDLL(str(args.library.resolve()))
    draw = library.iw_v00_control_background_line
    draw.argtypes = (ctypes.c_uint16, ctypes.POINTER(ctypes.c_uint16), ctypes.c_size_t)
    draw.restype = ctypes.c_bool
    line = (ctypes.c_uint16 * WIDTH)()
    if draw(HEIGHT, line, WIDTH) or draw(0, line, WIDTH - 1) or draw(0, None, WIDTH):
        raise ValueError("逐行绘制器没有拒绝非法缓冲区或行号")

    output = Image.new("RGB", (WIDTH, HEIGHT))
    approved = Image.open(str(SOURCE)).convert("RGB")
    pixels = output.load()
    reference = approved.load()
    total_error = 0
    max_error = 0
    max_error_xy = (0, 0)
    quantized_error = 0
    for row in range(HEIGHT):
        if not draw(row, line, WIDTH):
            raise ValueError("背景行绘制失败：{}".format(row))
        for col in range(WIDTH):
            color = expand565(line[col])
            pixels[col, row] = color
            for channel in range(3):
                delta = abs(color[channel] - reference[col, row][channel])
                total_error += delta
                if delta > max_error:
                    max_error = delta
                    max_error_xy = (col, row)
                quantized_error += abs(color[channel] -
                                       expand565(pack565(reference[col, row]))[channel])
    samples = []
    for _ in range(5):
        start = time.perf_counter()
        for row in range(HEIGHT):
            if not draw(row, line, WIDTH):
                raise ValueError("重复绘制失败")
        samples.append((time.perf_counter() - start) * 1000)
    mean_error = round(total_error / (WIDTH * HEIGHT * 3), 4)
    mean_quantized_error = round(quantized_error / (WIDTH * HEIGHT * 3), 4)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    output.save(str(OUTPUT / "control-background-procedural.png"))
    report = {"schema": 1, "width": WIDTH, "height": HEIGHT,
              "output_format": "RGB565", "line_buffer_bytes": WIDTH * 2,
              "mean_absolute_rgb_error": mean_error, "max_channel_error": max_error,
              "max_error_xy": list(max_error_xy),
              "mean_error_after_reference_rgb565": mean_quantized_error,
              "host_c_frame_median_ms": round(statistics.median(samples), 3),
              "target_timing_verified": False, "release_allowed": False}
    (OUTPUT / "background-compare.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if mean_error > 3 or max_error > 34:
        raise ValueError("程序绘制与批准背景差异过大：mean={} max={} at {}".format(
            mean_error, max_error, max_error_xy))
    print("V00 背景逐行绘制：平均色差 {}，最大 {}，行缓冲 {} B".format(
        mean_error, max_error, WIDTH * 2))


if __name__ == "__main__":
    main()
