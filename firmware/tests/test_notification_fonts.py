"""通知扩字验证：真实 cmap、受控哈希和历史并集，不以截图掩盖缺字。"""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import unittest

from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/design"))
from extend_notification_fonts import ADDITIONS, SOURCE_SHA


class NotificationFontTests(unittest.TestCase):
    def test_identity_cmap_and_budget(self):
        font_dir = ROOT / "firmware/iwatch/src/resource/fonts"
        report = json.loads((font_dir / "NotoSansSC-notifications.subset.json").read_text(encoding="utf-8"))
        self.assertEqual(SOURCE_SHA, report["source_sha256"])
        self.assertEqual([300, 400, 500, 600], [row["weight"] for row in report["subsets"]])
        total = (font_dir / "DroidSansFallback.ttf").stat().st_size
        for row in report["subsets"]:
            path = font_dir / row["file"]
            data = path.read_bytes()
            self.assertEqual(row["sha256"], hashlib.sha256(data).hexdigest())
            self.assertEqual(row["bytes"], len(data))
            with TTFont(io.BytesIO(data)) as font:
                codes = set(font.getBestCmap())
                self.assertEqual(codes, set(row["codepoints"]))
                self.assertTrue(set(map(ord, ADDITIONS.get(row["weight"], ""))) <= codes)
                self.assertEqual(row["weight"], font["OS/2"].usWeightClass)
            original = subprocess.check_output(["git", "show", "215c2dab:" + path.relative_to(ROOT).as_posix()], cwd=ROOT)
            with TTFont(io.BytesIO(original)) as old_font:
                self.assertTrue(set(old_font.getBestCmap()) <= codes)
            total += len(data)
        self.assertEqual(total, report["total_bytes"])
        self.assertLessEqual(total, 131072)
        self.assertEqual(131072, report["limit_bytes"])
        print(f"Notification font total={total} headroom={131072-total}; original cmap retained")


if __name__ == "__main__":
    unittest.main()
