"""独立浮点方向基准，核验 target_cache 导出的材质像素；不是实机验收。"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def check(directory):
    specs = [(147, 147, 73.5, 150, 155, 155, 4, 0x303033, 0x222225, 255),
             (350, 96, 24, 120, 350, 96, 0, 0x2b2b2e, 0x242426, 255),
             (350, 98, 24, 120, 350, 98, 0, 0x29292c, 0x202022, 255),
             (167, 103, 51.5, 150, 167, 103, 0, 0x767776, 0x454448, 117),
             (167, 103, 51.5, 130, 167, 103, 0, 0x00bde9, 0x0989fb, 255),
             (167, 103, 51.5, 140, 167, 103, 0, 0x7860ff, 0x5d40fa, 255)]
    rows = []
    for key, (w, h, radius, angle, gw, gh, inset, start, end, opacity) in enumerate(specs, 1):
        raw = (directory / f'paint-{key}.rgb565a8').read_bytes()
        assert len(raw) == w * h * 3
        dx, dy = math.sin(math.radians(angle)), -math.cos(math.radians(angle))
        extent = gw * abs(dx) + gh * abs(dy)
        maximum, checked = 0.0, 0
        for y in range(h):
            for x in range(w):
                cx = max(radius - (x + .5), 0, x + .5 - (w - radius))
                cy = max(radius - (y + .5), 0, y + .5 - (h - radius))
                alpha = raw[w * h * 2 + y * w + x]
                distance = math.hypot(cx, cy)
                if distance > radius + 1:
                    assert alpha == 0, (key, x, y, 'outside')
                # 胶囊内阴影/2px 边框另由完整截图检查；方向测试只取距边缘至少6px的填充区。
                if distance >= radius - 6 or min(x, y, w - 1 - x, h - 1 - y) < 6:
                    continue
                assert alpha == opacity, (key, x, y, alpha)
                offset = 2 * (y * w + x)
                packed = raw[offset] | raw[offset + 1] << 8
                actual = [(packed >> 11) * 255 / 31, ((packed >> 5) & 63) * 255 / 63, (packed & 31) * 255 / 31]
                t = .5 + ((x + inset + .5 - gw / 2) * dx + (y + inset + .5 - gh / 2) * dy) / extent
                expected = [((start >> s) & 255) * (1 - t) + ((end >> s) & 255) * t for s in [16, 8, 0]]
                for value, ideal, levels in zip(actual, expected, [31, 63, 31]):
                    error = abs(value - ideal)
                    assert error <= 255 / levels + .51, (key, x, y, error)
                    maximum = max(maximum, error)
                checked += 1
        assert checked > 1000
        rows.append({'key': key, 'angle': angle, 'interior_pixels_checked': checked,
                     'max_channel_error': maximum, 'sha256': hashlib.sha256(raw).hexdigest()})
    return {'status': 'passed', 'method': 'independent CSS-angle float reference; one RGB565 step + 0.51', 'results': rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paint_directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.paint_directory), indent=2))
