"""校验构建输入与镜像身份，仅处理本地文件，不访问烧录器。"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
from datetime import datetime, timezone
from pathlib import Path

import check_layout
import sdk_patch

FIRMWARE = Path(__file__).resolve().parent
ROOT = FIRMWARE.parent
PROFILES = FIRMWARE / 'build_profiles.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True, encoding='utf-8').strip()


def load_profile(name):
    config = json.loads(PROFILES.read_text(encoding='utf-8'))
    profile = config['profiles'][name]
    if not profile['build_enabled']:
        raise ValueError(f'{name}: design only; no validated BSP or partition map')
    return config, profile


def validate_config(text, profile):
    defines = dict(re.findall(r'^#define\s+(\w+)[ \t]*(.*?)\s*$', text, re.M))
    errors = []
    for name, value in profile['required_defines'].items():
        if name not in defines or (value is not None and defines[name] != value):
            errors.append(f'effective config mismatch: {name}={defines.get(name)} expected {value}')
    for name in profile['forbidden_defines']:
        if name in defines:
            errors.append(f'unvalidated feature enabled: {name}')
    return errors


def image_budget(size, profile):
    limit = profile['main_slot_bytes']
    free = limit - size
    if size <= 0 or free < profile['image_min_headroom_bytes']:
        raise ValueError(f'main image headroom {free} B is below {profile["image_min_headroom_bytes"]} B')
    return {'bytes': size, 'slot_bytes': limit, 'free_bytes': free,
            'used_percent': round(100 * size / limit, 3),
            'warning': size * 100 >= limit * profile['image_warning_percent']}


def source_snapshot(sdk, profile, config):
    sdk_state = sdk_patch.identify_sdk(sdk)
    commit = sdk_state['sdk_commit']
    if commit != config['sdk_commit']:
        raise ValueError(f'wrong SDK commit: {commit}')
    modules = git(sdk, 'submodule', 'status', '--recursive').splitlines()
    if len(modules) < 2 or any(line.startswith(('-', '+', 'U')) for line in modules):
        raise ValueError('SDK submodules are missing or differ from the pinned revision')
    sources = {}
    for base in [FIRMWARE/'iwatch/src', FIRMWARE/'boards', FIRMWARE/'iwatch/project']:
        for path in sorted(base.rglob('*')):
            if not path.is_file():
                continue
            parts = path.relative_to(base).parts
            if any(part.startswith(('build', '__pycache__', '.')) for part in parts):
                continue
            if base.name == 'project' and (parts[0] in ('src', 'resource', 'artifacts') or path.name.startswith('project.uv')):
                continue
            if path.suffix.lower() not in ('.o', '.pyc', '.log', '.bak'):
                sources[path.relative_to(ROOT).as_posix()] = sha(path)
    for path in sorted(FIRMWARE.glob('*')):
        if path.suffix in ('.py', '.ps1', '.json'):
            sources[path.relative_to(ROOT).as_posix()] = sha(path)
    bsp = sdk / profile['sdk_bsp']
    bsp_files = {path.relative_to(sdk).as_posix(): sha(path) for path in sorted(bsp.rglob('*')) if path.is_file()}
    if not (bsp/'script/SConscript').is_file() or len(list(bsp.glob('*.c'))) != 4:
        raise ValueError('unexpected actual SDK BSP source set')
    patch_identity = None
    if sdk_state['mode'] == 'patched':
        patch_identity = {
            'sha256': sdk_state['patch_sha256'],
            'files': sdk_state['patched_file_sha256'],
        }
    return {'sdk_commit': commit, 'sdk_mode': sdk_state['mode'],
            'sdk_clean': sdk_state['mode'] == 'base', 'sdk_patch': patch_identity,
            'sdk_submodules': [line.strip().split()[:2] for line in modules],
            'sdk_bsp': bsp_files, 'project_inputs': sources}


def compiler_identity(name, compiler, config):
    version = subprocess.check_output([str(compiler), '--version'], text=True, encoding='utf-8')
    if config['toolchains'][name] not in version:
        raise ValueError(f'unsupported {name} compiler version: {version}')
    return {'name': name, 'path': str(compiler.resolve()), 'sha256': sha(compiler), 'version': version.strip()}


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def verify_artifacts(record, build):
    required = {'rtconfig.h', 'sftool_param.json', 'main.map', 'main.bin',
                'bootloader/bootloader.bin', 'ftab/ftab.bin'}
    if record.get('inputs', {}).get('sdk_mode') == 'patched':
        required.add('resource_budget.json')
    if not required.issubset(record['artifacts']):
        raise ValueError('incomplete artifact identity')
    for rel, digest in record['artifacts'].items():
        path = (build/rel).resolve()
        if not path.is_relative_to(build.resolve()) or sha(path) != digest:
            raise ValueError(f'artifact changed or escaped build directory: {rel}')


def validate_resource_budget(report, record, build, toolchain):
    """确认预算报告绑定当前核心产物，随后才能写入最终构建身份。"""
    extension = 'elf' if toolchain == 'gcc' else 'axf'
    executable = f'main.{extension}'
    expected_patch = record.get('inputs', {}).get('sdk_patch')
    if report.get('schema') != 1 or not report.get('passed'):
        raise ValueError('resource budget is missing or exceeds a hard limit')
    if (report.get('toolchain') != toolchain or
            report.get('profile') != record.get('profile') or
            report.get('git_head') != record.get('git_head') or
            report.get('sdk_commit') != record.get('inputs', {}).get('sdk_commit') or
            report.get('sdk_patch') != expected_patch):
        raise ValueError('resource budget belongs to different build inputs')
    expected_evidence = {
        'main.map': sha(build/'main.map'),
        'main.bin': sha(build/'main.bin'),
        executable: sha(build/executable),
    }
    if report.get('evidence') != expected_evidence:
        raise ValueError('resource budget evidence differs from current artifacts')
    if report.get('resources', {}).get('main_bin_bytes') != (build/'main.bin').stat().st_size:
        raise ValueError('resource budget main size differs from current image')


def object_compiler_comment(path):
    """读取项目自身 ELF32 对象的编译器标记，排除第三方库混入的标记。"""
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('expected little-endian ELF32 object')
    offset = struct.unpack_from('<I', data, 32)[0]
    size, count, names_index = struct.unpack_from('<HHH', data, 46)
    if size != 40 or names_index >= count:
        raise ValueError('invalid ELF section header')
    sections = [struct.unpack_from('<10I', data, offset + i * size) for i in range(count)]
    names_section = sections[names_index]
    names = data[names_section[4]:names_section[4]+names_section[5]]
    for section in sections:
        if names[section[0]:].split(b'\0', 1)[0] == b'.comment':
            return data[section[4]:section[4]+section[5]].decode('utf-8', errors='replace')
    raise ValueError('compiler marker missing from project object')


def require_object_compiler(path, expected):
    comment = object_compiler_comment(path)
    if expected not in comment:
        raise ValueError(f'actual object compiler differs from requested toolchain: {comment!r}')
    return comment


def require_map_toolchain(path, expected):
    """识别链接 map 的生成工具，防止共享输出中的旧文件混入归档。"""
    with path.open('r', encoding='utf-8', errors='replace') as stream:
        header = stream.read(131072)
    if 'Component: ARM Compiler' in header and 'Tool: armlink' in header:
        actual = 'keil'
    elif ('Archive member included to satisfy reference by file (symbol)' in header or
          'Linker script and memory map' in header):
        actual = 'gcc'
    else:
        raise ValueError('unrecognized linker map provenance')
    if actual != expected:
        raise ValueError(f'linker map belongs to {actual}, requested {expected}')
    return actual


def run(args):
    config, profile = load_profile(args.profile)
    snapshot = source_snapshot(args.sdk, profile, config)
    compiler = compiler_identity(args.toolchain, args.compiler, config)
    build = args.build_dir
    if args.phase == 'begin':
        table = json.loads(check_layout.TABLE.read_text(encoding='utf-8'))
        errors = check_layout.validate_layout(table, check_layout.board_capacities(check_layout.BOARD))
        if errors:
            raise ValueError('; '.join(errors))
        regions = [r for b in table if b['mem'] == 'flash4' for r in b['regions'] if r.get('img') == 'main']
        if len(regions) != 1 or check_layout.number(regions[0]['max_size']) != profile['main_slot_bytes']:
            raise ValueError('profile main budget differs from actual partition table')
        save(args.state, {'profile': args.profile, 'inputs': snapshot, 'compiler': compiler,
                          'started_utc': datetime.now(timezone.utc).isoformat(),
                          'git_head': git(ROOT, 'rev-parse', 'HEAD')})
        # 不允许失败的构建留下可误认成当前结果的身份文件。
        identity = build/'build_identity.json'
        if identity.exists():
            identity.unlink()
        archive = build.parent/'artifacts'/args.profile/args.toolchain
        archive_parent = (build.parent/'artifacts'/args.profile).resolve()
        if archive.exists():
            if archive.resolve().parent != archive_parent:
                raise ValueError('archive cleanup path escaped the selected profile')
            shutil.rmtree(archive)
        print(f'BUILD INPUTS OK: {args.profile}, {args.toolchain}, SDK {snapshot["sdk_commit"]}')
        return
    before = json.loads(args.state.read_text(encoding='utf-8'))
    if before['profile'] != args.profile or before['inputs'] != snapshot or before['compiler'] != compiler:
        raise ValueError('source/config/toolchain changed during build')
    if args.phase == 'attach-budget':
        identity_path = build/'build_identity.json'
        report_path = build/'resource_budget.json'
        record = json.loads(identity_path.read_text(encoding='utf-8'))
        if (record['profile'] != args.profile or record['inputs'] != snapshot or
                record['compiler'] != compiler):
            raise ValueError('core identity belongs to another source or toolchain')
        if snapshot.get('sdk_mode') != 'patched':
            raise ValueError('D07 resource budget requires the verified patched SDK')
        report = json.loads(report_path.read_text(encoding='utf-8'))
        validate_resource_budget(report, record, build, args.toolchain)
        record['artifacts']['resource_budget.json'] = sha(report_path)
        save(identity_path, record)
        archive = build.parent/'artifacts'/args.profile/args.toolchain
        for rel in [*record['artifacts'], 'build_identity.json']:
            target = archive/rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(build/rel, target)
        print(f'BUILD RESOURCE BUDGET ATTACHED: {report_path}; archive {archive}')
        return
    if args.phase == 'verify':
        record = json.loads((build/'build_identity.json').read_text(encoding='utf-8'))
        if record['profile'] != args.profile or record['inputs'] != snapshot or record['compiler'] != compiler:
            raise ValueError('artifact belongs to another source or toolchain')
        verify_artifacts(record, build)
        require_object_compiler(build/'src/gui_apps/watch_demo.o', config['toolchains'][args.toolchain])
        require_map_toolchain(build/'main.map', args.toolchain)
        print('BUILD IDENTITY OK')
        return
    table = json.loads(check_layout.TABLE.read_text(encoding='utf-8'))
    errors = check_layout.validate_images(table, build)
    errors += validate_config((build/'rtconfig.h').read_text(encoding='utf-8'), profile)
    if errors:
        raise ValueError('; '.join(errors))
    manifest = json.loads((build/'sftool_param.json').read_text(encoding='utf-8'))
    artifacts = {}
    for item in manifest['write_flash']['files']:
        rel = item['path'].replace('\\', '/')
        path = (build/rel).resolve()
        if not path.is_relative_to(build.resolve()):
            raise ValueError(f'image outside build directory: {rel}')
        artifacts[rel] = sha(path)
    for rel in ['rtconfig.h', 'sftool_param.json', 'main.map']:
        artifacts[rel] = sha(build/rel)
    budget = image_budget((build/'main.bin').stat().st_size, profile)
    object_rel = 'src/gui_apps/watch_demo.o'
    comment = require_object_compiler(build/object_rel, config['toolchains'][args.toolchain])
    map_toolchain = require_map_toolchain(build/'main.map', args.toolchain)
    artifacts[object_rel] = sha(build/object_rel)
    extension = 'elf' if args.toolchain == 'gcc' else 'axf'
    for name in ['main', 'bootloader/bootloader', 'ftab/ftab']:
        rel = f'{name}.{extension}'
        artifacts[rel] = sha(build/rel)
    before.update({'completed_utc': datetime.now(timezone.utc).isoformat(), 'budget': budget,
                   'actual_compiler_comment': comment.strip('\x00'),
                   'actual_map_toolchain': map_toolchain,
                   'artifacts': artifacts, 'board_status': '未刷入；板上验证待执行',
                   'actual_sdk_bsp_path': str(args.sdk/profile['sdk_bsp'])})
    save(build/'build_identity.json', before)
    # 各工具链独立归档；共享 SCons 输出受到外层文件锁保护。
    archive = build.parent/'artifacts'/args.profile/args.toolchain
    if snapshot.get('sdk_mode') == 'base':
        for rel in [*artifacts, 'build_identity.json']:
            target = archive/rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(build/rel, target)
    destination = archive if snapshot.get('sdk_mode') == 'base' else '等待资源预算绑定'
    print(f'BUILD {"WARNING" if budget["warning"] else "OK"}: main {budget["used_percent"]}% '
          f'({budget["bytes"]} B), free {budget["free_bytes"]} B; {destination}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('begin', 'finish', 'attach-budget', 'verify'))
    parser.add_argument('--profile', default='DEV_A128_NAND')
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--toolchain', choices=('gcc', 'keil'), required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--state', type=Path, required=True)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, ValueError, KeyError, struct.error, subprocess.CalledProcessError) as error:
        print(f'BUILD IDENTITY ERROR: {error}')
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
