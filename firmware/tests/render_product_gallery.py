"""把生产 View 的 RGB565 主机帧转成 PNG，保留样本参数与原始帧摘要。"""
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
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 390, 450, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pages = ['face', 'apps', 'settings', 'display', 'brightness', 'time', 'about']
    samples = []
    for variant in range(56):
        parameters = {'page': pages[variant % 7], 'large_text': bool(variant // 7 % 2),
                      'quality': variant // 14 % 2, 'reduced_motion': variant >= 28}
        samples.append((100 + variant, dict(parameters, position='top')))
        if variant % 7 == 6:
            samples.append((300 + variant, dict(parameters, position='bottom')))
        if variant % 7 == 5:
            samples.append((400 + variant, dict(parameters, position='bottom')))
            for field in range(6):
                samples.append((200 + variant // 7 * 6 + field, dict(parameters, picker=field)))
    # 先确认整套帧都存在，再写入预览，避免半套画面被误认成完整验收。
    frames = [(number, parameters, (args.input / f'gallery-{number}-0.rgb565').read_bytes())
              for number, parameters in samples]
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {'fixture': 'VIS-FIXTURE-01', 'evidence': '主机软件渲染；不是实屏照片或 RTC 证据',
                'samples': []}
    for number, parameters, data in frames:
        filename = f'product-{number}.png'
        image = png(data)
        (args.output / filename).write_bytes(image)
        manifest['samples'].append(dict(parameters, file=filename,
            rgb565_sha256=hashlib.sha256(data).hexdigest(), png_sha256=hashlib.sha256(image).hexdigest()))
    (args.output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'PRODUCT GALLERY OK: {len(samples)} independent samples')


if __name__ == '__main__':
    main()
