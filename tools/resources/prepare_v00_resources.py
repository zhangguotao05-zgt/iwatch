"""从受控本地包核验/接入冻结图片；不下载、不授予再分发许可、不覆盖不同内容。"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
CONTRACT = Path(__file__).with_name('v00-inputs.json')
PACK_PREFIX = 'V00-GCC-resources-20260925/payload/'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def relative_path(value):
    """拒绝绝对路径、上溯和跨平台盘符，保持清单写入范围明确。"""
    path = PurePosixPath(value.replace('\\', '/'))
    if path.is_absolute() or '..' in path.parts or ':' in str(path) or not path.parts:
        raise ValueError('Unsafe relative path: ' + value)
    return path


def target_path(root, relative):
    root = root.resolve()
    target = (root / relative_path(relative)).resolve()
    if not target.is_relative_to(root):
        raise ValueError('Output escapes checkout: ' + relative)
    return target


def checked(data, row):
    if len(data) != row['bytes'] or digest(data) != row['sha256']:
        raise ValueError('Resource identity mismatch: ' + row['path'])
    return data


def prepare(root, contract, build_pack=None, raw_root=None, apply=False):
    """先验证全部输入/旧文件，再写缺失文件；不导入旧源码、旧字体清单或历史日志。"""
    if contract.get('schema') != 1 or contract.get('release_allowed') is not False:
        raise ValueError('Expected frozen non-release resource contract')
    planned, seen = [], set()
    pack = zipfile.ZipFile(build_pack) if build_pack else None
    try:
        if pack:
            names = pack.namelist()
            if len(names) != len(set(names)):
                raise ValueError('Duplicate ZIP entries')
            for name in names:
                relative_path(name)
        for item in contract['resources']:
            for kind in ['target_png', 'host_raw']:
                row = item.get(kind)
                if row is None:
                    continue
                if kind == 'target_png':
                    rel = row['path']
                    if not rel.startswith('firmware/iwatch/src/resource/images/common/ezip/v00_') or not rel.endswith('.png'):
                        raise ValueError('Unexpected target image path')
                else:
                    raw_rel = relative_path(row['path'])
                    if raw_rel.parts[:2] != ('assets', 'raw') or len(raw_rel.parts) != 3:
                        raise ValueError('Unexpected raw resource path')
                    rel = 'work/v00-inputs/assets/raw/' + raw_rel.name
                target = target_path(root, rel)
                if target in seen:
                    raise ValueError('Duplicate destination: ' + rel)
                seen.add(target)
                data = None
                if kind == 'target_png' and pack:
                    member = PACK_PREFIX + row['path']
                    info = pack.getinfo(member)
                    if info.file_size != row['bytes']:
                        raise ValueError('Unexpected ZIP payload size: ' + member)
                    data = checked(pack.read(info), row)
                if kind == 'host_raw' and raw_root:
                    data = checked((Path(raw_root) / PurePosixPath(row['path']).name).read_bytes(), row)
                if target.exists():
                    checked(target.read_bytes(), row)
                elif data is not None:
                    planned.append((target, data, rel))
                else:
                    planned.append((target, None, rel))
        missing = [rel for _, data, rel in planned if data is None]
        if apply and missing:
            raise ValueError('Missing local resource inputs: ' + ', '.join(missing))
        if apply:
            for target, data, _ in planned:
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as stream:
                    stream.write(data)
        return {'status': 'passed' if apply or not planned else 'missing',
                'checked_files': len(seen), 'missing_files': [rel for _, _, rel in planned] if not apply else [],
                'written_files': len(planned) if apply else 0, 'release_allowed': False,
                'network_used': False, 'source_modified': False,
                'host_asset_root': 'work/v00-inputs/assets/raw'}
    finally:
        if pack:
            pack.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--build-pack', type=Path, help='受控 V00-GCC-resources-20260925.zip，本地路径')
    parser.add_argument('--raw-root', type=Path, help='已核验 51eaec5 原包解包后的 assets/raw 目录')
    parser.add_argument('--apply', action='store_true', help='验证全部输入后仅创建缺失文件')
    args = parser.parse_args()
    try:
        contract = json.loads(CONTRACT.read_text(encoding='utf-8'))
        report = prepare(args.root, contract, args.build_pack, args.raw_root, args.apply)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print('V00 RESOURCE ERROR: ' + str(error), file=sys.stderr)
        return 1
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report['status'] == 'passed' else 2


if __name__ == '__main__':
    raise SystemExit(main())
