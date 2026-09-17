from pathlib import Path
import copy
import json
import os
import sys
import struct
import tempfile
import unittest
from unittest import mock
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_identity as identity


class BuildIdentityTests(unittest.TestCase):
    def test_embedded_identity_tracks_inputs_and_actual_image(self):
        record = {'git_head': 'a' * 40, 'inputs': {'file': 'one'}, 'compiler': {'name': 'gcc'}}
        record['embedded_tag'] = identity.embedded_tag(record)
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build/'main.bin').write_bytes(record['embedded_tag'].encode('ascii') + b'\0')
            identity.verify_embedded_tag(record, build)
            record['inputs']['file'] = 'two'
            with self.assertRaisesRegex(ValueError, 'does not match'):
                identity.verify_embedded_tag(record, build)
            record['embedded_tag'] = identity.embedded_tag(record)
            with self.assertRaisesRegex(ValueError, 'missing'):
                identity.verify_embedded_tag(record, build)

    def setUp(self):
        self.config, self.profile = identity.load_profile('DEV_A128_NAND')

    def test_font_gate_runs_before_sdk_and_build_identity(self):
        with mock.patch.object(identity.generate_font_subset, 'verify_committed',
                               side_effect=ValueError('stale font manifest')) as check:
            with mock.patch.object(identity.sdk_patch, 'identify_sdk') as sdk:
                with self.assertRaisesRegex(ValueError, 'stale font manifest'):
                    identity.source_snapshot(Path('unused'), self.profile, self.config)
                check.assert_called_once_with()
                sdk.assert_not_called()

    def test_product_cannot_build_with_dev_layout(self):
        with self.assertRaisesRegex(ValueError, 'design only'):
            identity.load_profile('PRODUCT_N16_NOR')

    def test_early_warning_and_hard_reserve(self):
        p = self.profile
        self.assertFalse(identity.image_budget(p['main_slot_bytes'] * 80 // 100, p)['warning'])
        self.assertTrue(identity.image_budget(p['main_slot_bytes'] * 90 // 100, p)['warning'])
        for size in [0, p['main_slot_bytes'], p['main_slot_bytes'] + 1]:
            with self.assertRaises(ValueError):
                identity.image_budget(size, p)

    def test_effective_config_rejects_wrong_board_and_unvalidated_pm(self):
        text = '\n'.join(f'#define {name} {value or ""}' for name, value in self.profile['required_defines'].items())
        self.assertEqual([], identity.validate_config(text, self.profile))
        self.assertTrue(identity.validate_config(text.replace('BSP_QSPI4_MEM_SIZE 128', 'BSP_QSPI4_MEM_SIZE 16'), self.profile))
        self.assertTrue(identity.validate_config(text+'\n#define BSP_USING_PM', self.profile))

    def test_changed_artifact_changes_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'main.bin'
            path.write_bytes(b'first')
            digest = identity.sha(path)
            path.write_bytes(b'other')
            self.assertNotEqual(digest, identity.sha(path))

    def test_actual_verifier_rejects_tampered_or_incomplete_package(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            artifacts = {}
            for rel in ['rtconfig.h', 'sftool_param.json', 'main.map', 'main.bin', 'bootloader/bootloader.bin', 'ftab/ftab.bin']:
                path = build/rel
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(b'original')
                artifacts[rel] = identity.sha(path)
            record = {'artifacts': artifacts}
            identity.verify_artifacts(record, build)
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                identity.verify_artifacts({'artifacts': {}}, build)
            (build/'main.bin').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'artifact changed'):
                identity.verify_artifacts(record, build)

    def test_patched_package_requires_bound_resource_budget(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            artifacts = {}
            required = ['rtconfig.h', 'sftool_param.json', 'main.map', 'main.bin',
                        'bootloader/bootloader.bin', 'ftab/ftab.bin']
            for rel in required:
                path = build/rel
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(b'original')
                artifacts[rel] = identity.sha(path)
            record = {'inputs': {'sdk_mode': 'patched'}, 'artifacts': artifacts}
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                identity.verify_artifacts(record, build)
            report = build/'resource_budget.json'
            report.write_text('{}', encoding='utf-8')
            artifacts['resource_budget.json'] = identity.sha(report)
            identity.verify_artifacts(record, build)

    def test_resource_budget_binding_rejects_other_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            for name in ('main.map', 'main.bin', 'main.elf'):
                (build/name).write_bytes(name.encode('ascii'))
            patch = {'sha256': 'a' * 64, 'files': {}}
            record = {
                'profile': 'DEV_A128_NAND', 'git_head': 'abc',
                'inputs': {'sdk_commit': 'sdk', 'sdk_patch': patch},
            }
            report = {
                'schema': 1, 'passed': True, 'toolchain': 'gcc',
                'profile': 'DEV_A128_NAND', 'git_head': 'abc',
                'sdk_commit': 'sdk', 'sdk_patch': patch,
                'resources': {'main_bin_bytes': len(b'main.bin')},
                'evidence': {
                    'main.map': identity.sha(build/'main.map'),
                    'main.bin': identity.sha(build/'main.bin'),
                    'main.elf': identity.sha(build/'main.elf'),
                },
            }
            identity.validate_resource_budget(report, record, build, 'gcc')
            report['evidence']['main.bin'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'evidence differs'):
                identity.validate_resource_budget(report, record, build, 'gcc')

    def test_gcc_object_cannot_be_labelled_keil(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'source.o'
            names = b'\0.shstrtab\0.comment\0'
            comment = b'\0GCC: 14.2.1 20241119\0'
            header = bytearray(52)
            header[:6] = b'\x7fELF\x01\x01'
            struct.pack_into('<I', header, 32, 52)
            struct.pack_into('<HHH', header, 46, 40, 3, 1)
            sections = b'\0'*40 + struct.pack('<10I', 1, 3, 0, 0, 172, len(names), 0, 0, 1, 0)
            sections += struct.pack('<10I', 11, 1, 0, 0, 172+len(names), len(comment), 0, 0, 1, 0)
            path.write_bytes(header+sections+names+comment)
            identity.require_object_compiler(path, self.config['toolchains']['gcc'])
            with self.assertRaisesRegex(ValueError, 'compiler differs'):
                identity.require_object_compiler(path, self.config['toolchains']['keil'])

    def test_linker_map_must_match_requested_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'main.map'
            path.write_text(
                'Archive member included to satisfy reference by file (symbol)\n',
                encoding='utf-8')
            self.assertEqual('gcc', identity.require_map_toolchain(path, 'gcc'))
            with self.assertRaisesRegex(ValueError, 'belongs to gcc'):
                identity.require_map_toolchain(path, 'keil')

            path.write_text(
                'Component: ARM Compiler 6.16 Tool: armlink [5dfeaa00]\n',
                encoding='utf-8')
            self.assertEqual('keil', identity.require_map_toolchain(path, 'keil'))
            with self.assertRaisesRegex(ValueError, 'belongs to keil'):
                identity.require_map_toolchain(path, 'gcc')

            path.write_text('map without a linker signature\n', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'unrecognized'):
                identity.require_map_toolchain(path, 'gcc')

    def test_source_snapshot_accepts_only_declared_sdk_states(self):
        root = Path(__file__).resolve().parents[2]
        base = Path(os.environ.get('IWATCH_BASE_SDK',
                                   r'C:\OpenSiFli\SiFli-SDK-v2.5.1-iwatch-locked'))
        derived = Path(os.environ.get('IWATCH_PATCHED_SDK', str(root / 'work/sdk-d07-a0')))
        if not base.exists() or not derived.exists():
            self.skipTest('local fixed and derived SDK worktrees are not present')

        base_snapshot = identity.source_snapshot(base, self.profile, self.config)
        self.assertEqual('base', base_snapshot['sdk_mode'])
        self.assertTrue(base_snapshot['sdk_clean'])
        self.assertIsNone(base_snapshot['sdk_patch'])

        derived_snapshot = identity.source_snapshot(derived, self.profile, self.config)
        self.assertEqual('patched', derived_snapshot['sdk_mode'])
        self.assertFalse(derived_snapshot['sdk_clean'])
        self.assertEqual(identity.sdk_patch.load_manifest()[0]['patch_sha256'],
                         derived_snapshot['sdk_patch']['sha256'])


if __name__ == '__main__':
    unittest.main()
