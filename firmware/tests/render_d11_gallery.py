"""把 D11 生产 View 的 RGB565 主机帧转为 PNG，并保存参数与哈希。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def png(data):
    if len(data) != 390 * 450 * 2:
        raise ValueError('RGB565 帧尺寸不等于 390×450')
    rows = bytearray()
    for y in range(450):
        rows.append(0)
        for x in range(390):
            pixel = struct.unpack_from('<H', data, (y * 390 + x) * 2)[0]
            rows.extend((((pixel >> 11) & 31) * 255 // 31,
                         ((pixel >> 5) & 63) * 255 // 63, (pixel & 31) * 255 // 31))
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', 390, 450, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    profiles = [
        {'quality': 'Q0', 'large_text': False, 'reduced_motion': False},
        {'quality': 'Q1', 'large_text': False, 'reduced_motion': False},
        {'quality': 'Q1', 'large_text': True, 'reduced_motion': False},
        {'quality': 'Q1', 'large_text': False, 'reduced_motion': True},
    ]
    states = ['launcher', 'timer-empty', 'timer-mixed', 'timer-running',
              'timer-paused', 'timer-expired', 'stopwatch-running', 'stopwatch-full-paused']
    frames = []
    for number in range(32):
        source = args.input / f'gallery-{500 + number}-0.rgb565'
        frames.append((number, dict(profiles[number // 8], state=states[number % 8]),
                       source.read_bytes()))
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {'fixture': 'D11-VIS-FIXTURE-01',
                'evidence': '生产 View 主机软件渲染；不是实屏照片或计时精度证据',
                'samples': []}
    for number, parameters, data in frames:
        filename = f'd11-{number:02d}.png'
        image = png(data)
        (args.output / filename).write_bytes(image)
        manifest['samples'].append(dict(parameters, file=filename,
            rgb565_sha256=hashlib.sha256(data).hexdigest(),
            png_sha256=hashlib.sha256(image).hexdigest()))
    (args.output / 'manifest.json').write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'D11 GALLERY OK: {len(frames)} independent samples')


if __name__ == '__main__':
    main()
