"""本地资源导入安全边界；使用合成字节，不依赖受限原图。"""
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile

import prepare_v00_resources as resources


class ResourceImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / 'repo'
        self.root.mkdir()
        self.raw = self.base / 'raw'
        self.raw.mkdir()
        self.png_path = 'firmware/iwatch/src/resource/images/common/ezip/v00_test.png'
        def row(path, data):
            return {'path': path, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        self.contract = {'schema': 1, 'release_allowed': False, 'resources': [{
            'target_png': row(self.png_path, b'png'),
            'host_raw': row('assets/raw/test.rgb565a8.bin', b'raw')} ]}
        (self.raw / 'test.rgb565a8.bin').write_bytes(b'raw')
        self.pack = self.base / 'input.zip'
        self.write_pack()

    def write_pack(self, data=b'png', extra=None):
        with zipfile.ZipFile(self.pack, 'w') as pack:
            pack.writestr(resources.PACK_PREFIX + self.png_path, data)
            if extra:
                pack.writestr(extra, b'x')

    def run_import(self, apply=False, contract=None):
        return resources.prepare(self.root, contract or self.contract, self.pack, self.raw, apply)

    def test_read_only_missing(self):
        self.assertEqual(self.run_import()['status'], 'missing')
        self.assertEqual(list(self.root.iterdir()), [])

    def test_apply_check_and_idempotent(self):
        self.assertEqual(self.run_import(True)['written_files'], 2)
        self.assertEqual(self.run_import(True)['written_files'], 0)
        self.assertEqual(resources.prepare(self.root, self.contract)['status'], 'passed')

    def test_corrupt_payload_no_writes(self):
        self.write_pack(b'bad')
        with self.assertRaises(ValueError): self.run_import(True)
        self.assertEqual(list(self.root.iterdir()), [])

    def test_existing_mismatch_preserved(self):
        path = self.root / self.png_path
        path.parent.mkdir(parents=True)
        path.write_bytes(b'keep')
        with self.assertRaises(ValueError): self.run_import(True)
        self.assertEqual(path.read_bytes(), b'keep')

    def test_zip_traversal_rejected(self):
        self.write_pack(extra='../escape')
        with self.assertRaises(ValueError): self.run_import(True)

    def test_duplicate_destinations_rejected(self):
        contract = copy.deepcopy(self.contract)
        contract['resources'].append(contract['resources'][0])
        with self.assertRaises(ValueError): self.run_import(True, contract)

    def test_manifest_traversal_rejected(self):
        contract = copy.deepcopy(self.contract)
        contract['resources'][0]['target_png']['path'] = '../escape'
        with self.assertRaises(ValueError): self.run_import(True, contract)

    def test_missing_raw_no_partial_write(self):
        (self.raw / 'test.rgb565a8.bin').unlink()
        with self.assertRaises(FileNotFoundError): self.run_import(True)
        self.assertFalse((self.root / self.png_path).exists())


if __name__ == '__main__':
    unittest.main()
