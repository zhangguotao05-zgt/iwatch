from pathlib import Path
import copy
import json
import os
import sys
import struct
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_identity as identity


class BuildIdentityTests(unittest.TestCase):
    def setUp(self):
        self.config, self.profile = identity.load_profile('DEV_A128_NAND')

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
