"""V00 资源探针的损坏、尺寸和预算边界回归。"""

import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from tools.design import probe_v00_resources as probe


class ResourceProbeTests(unittest.TestCase):
    def test_integrity_rejects_corruption_and_truncation(self):
        payload = b"123456789"
        digest = probe.sha256(payload)
        self.assertTrue(probe.verified_blob(payload, 9, digest))
        self.assertFalse(probe.verified_blob(payload[:-1], 9, digest))
        self.assertFalse(probe.verified_blob(b"123456780", 9, digest))
        for _ in range(1000):
            self.assertTrue(probe.verified_blob(payload, 9, digest))

    def test_pixel_check_rejects_lost_alpha_and_wrong_size(self):
        with tempfile.TemporaryDirectory() as folder:
            source, decoded = Path(folder) / "source.png", Path(folder) / "decoded.png"
            Image.new("RGBA", (2, 1), (20, 30, 40, 0)).save(source)
            Image.new("RGBA", (2, 1), (20, 30, 40, 255)).save(decoded)
            with self.assertRaisesRegex(ValueError, "透明度"):
                probe.pixel_difference(source, decoded)
            Image.new("RGBA", (3, 1), (20, 30, 40, 0)).save(decoded)
            with self.assertRaisesRegex(ValueError, "尺寸"):
                probe.pixel_difference(source, decoded)

    def test_report_keeps_full_package_over_budget(self):
        report = json.loads(probe.DEFAULT_REPORT.read_text(encoding="utf-8"))
        self.assertEqual(len(report["resources"]), 38)
        self.assertGreater(report["cases"]["foreground"]["gcc_headroom_after_bytes"], 0)
        self.assertLess(report["cases"]["all"]["gcc_headroom_after_bytes"], 0)
        self.assertFalse(report["release_allowed"])
        self.assertTrue(all(not item["release_allowed"] for item in report["resources"]))


if __name__ == "__main__":
    unittest.main()
