# -*- coding: utf-8 -*-
"""正式参数资源桥的描述符、载荷和故障边界回归。"""

import json
import unittest

from tools.design import build_v00_resource_bridge as bridge


class ResourceBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = json.loads(bridge.DEFAULT_REPORT.read_text(encoding="utf-8"))
        cls.entries = cls.report["resources"]
        cls.sample = cls.entries[17]
        cls.work = bridge.DEFAULT_WORK

    def sample_parts(self):
        symbol = self.sample["symbol"]
        payload, descriptor = bridge.parse_c_image(self.work / "c" / (symbol + ".c"), symbol)
        binary = (self.work / "bin" / (symbol + ".bin")).read_bytes()
        return binary, payload, descriptor

    def test_parameters_and_catalog_are_bound(self):
        self.assertEqual(self.report["encoder_flags"], list(bridge.ENCODER_FLAGS))
        self.assertEqual(len(self.entries), 38)
        self.assertEqual(len({entry["symbol"] for entry in self.entries}), 38)
        self.assertEqual(sum(entry["role"] != "optional_background" for entry in self.entries), 37)
        self.assertFalse(self.report["release_allowed"])
        self.assertTrue(all(not entry["release_allowed"] for entry in self.entries))
        catalog = (self.work / "catalog" / "iw_v00_resource_catalog.c").read_bytes()
        self.assertEqual(bridge.digest(catalog), self.report["foreground_catalog_sha256"])
        self.assertEqual(self.report["sconscript_sha256"],
                         bridge.digest(bridge.SCONSCRIPT.read_bytes()))

    def test_actual_c_and_bin_payload_match(self):
        for entry in self.entries:
            symbol = entry["symbol"]
            payload, descriptor = bridge.parse_c_image(self.work / "c" / (symbol + ".c"), symbol)
            binary = (self.work / "bin" / (symbol + ".bin")).read_bytes()
            bridge.validate_pair(binary, payload, descriptor, entry["width"],
                                 entry["height"], entry["encoding"])
            self.assertEqual(bridge.digest(payload), entry["payload_sha256"])

    def test_rejects_header_payload_and_geometry_mismatch(self):
        binary, payload, descriptor = self.sample_parts()
        sample = self.sample
        bridge.validate_pair(binary, payload, descriptor, sample["width"],
                             sample["height"], sample["encoding"])
        with self.assertRaisesRegex(ValueError, "载荷不一致"):
            bridge.validate_pair(binary[:-1] + b"x", payload, descriptor,
                                 sample["width"], sample["height"], sample["encoding"])
        altered = bytearray(payload)
        altered[9] += 1
        with self.assertRaisesRegex(ValueError, "内部尺寸"):
            bridge.validate_pair(binary[:12] + altered, altered, descriptor,
                                 sample["width"], sample["height"], sample["encoding"])
        altered = bytearray(payload)
        altered[4] = 0x18
        with self.assertRaisesRegex(ValueError, "编码"):
            bridge.validate_pair(binary[:12] + altered, altered, descriptor,
                                 sample["width"], sample["height"], sample["encoding"])
        changed = dict(descriptor, **{"header.w": "1"})
        with self.assertRaisesRegex(ValueError, "C 描述符"):
            bridge.validate_pair(binary, payload, changed,
                                 sample["width"], sample["height"], sample["encoding"])


if __name__ == "__main__":
    unittest.main()
