"""逐像素核对原坐标绘字与无遮挡样张，包含暗部抗锯齿。"""

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FRAMES = ROOT / "work/v00/type-specimens"
WIDTH = 390
HEIGHT = 450


def foreground_pixels(path):
    frame = path.read_bytes()
    if len(frame) != WIDTH * HEIGHT * 2:
        raise ValueError(f"RGB565 帧大小错误：{path}")
    background = frame[:2]
    pixels = {}
    for offset in range(0, len(frame), 2):
        color = frame[offset:offset + 2]
        if color != background:
            index = offset // 2
            pixels[(index % WIDTH, index // WIDTH)] = color
    if not pixels:
        raise ValueError(f"没有字形像素：{path}")
    return background, pixels


def compare_pixels(free, page):
    free_left = min(x for x, _ in free)
    free_top = min(y for _, y in free)
    page_left = min(x for x, _ in page)
    page_top = min(y for _, y in page)
    free_normalized = {(x - free_left, y - free_top): color
                       for (x, y), color in free.items()}
    page_normalized = {(x - page_left, y - page_top): color
                       for (x, y), color in page.items()}
    missing = sum(page_normalized.get(position) != color
                  for position, color in free_normalized.items())
    extra = sum(free_normalized.get(position) != color
                for position, color in page_normalized.items())
    return page_left - free_left, page_top - free_top, missing, extra


def check(index, free_directory, page_directory):
    name = f"{index:02d}.rgb565"
    free_bg, free = foreground_pixels(free_directory / name)
    page_bg, page = foreground_pixels(page_directory / name)
    if free_bg != page_bg:
        raise ValueError(f"样张背景不一致：{name}")
    shift_x, shift_y, missing, extra = compare_pixels(free, page)
    result = "NO_CLIP" if not missing and not extra else "CLIPPED"
    print(f"RGB565_COMPARE index={index} free={len(free)} page={len(page)} "
          f"shift={shift_x},{shift_y} "
          f"missing={missing} extra={extra} result={result}")
    return result == "NO_CLIP"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", type=int, nargs="?")
    parser.add_argument("--free-dir", type=Path, default=FRAMES / "raw-color")
    parser.add_argument("--page-dir", type=Path, default=FRAMES / "raw-page")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        # 两帧亮色像素数量相同，但第二帧丢失了暗色抗锯齿边缘。
        assert compare_pixels({(0, 0): b"\xff\xff", (1, 0): b"\x65\x21"},
                              {(0, 0): b"\xff\xff"})[2:] == (1, 0)
        print("V00 PAGE PIXEL NEGATIVE TEST OK: dark edge loss detected")
        raise SystemExit(0)
    if args.index is None:
        parser.error("需要样例序号")
    raise SystemExit(0 if check(args.index, args.free_dir, args.page_dir) else 1)
