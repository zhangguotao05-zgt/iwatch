from pathlib import Path
import importlib.metadata
import json
import sys
import tempfile
import unittest
from unittest import mock


FIRMWARE = Path(__file__).resolve().parents[1]
ROOT = FIRMWARE.parent
sys.path.insert(0, str(FIRMWARE))
import generate_font_subset as subset


class FontSubsetTests(unittest.TestCase):
    def setUp(self):
        self.config_path = subset.DEFAULT_CONFIG
        self.config = json.loads(self.config_path.read_text(encoding="utf-8"))

    def test_committed_font_manifest_and_inputs(self):
        subset.verify_committed(self.config_path)

    def test_committed_stale_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest_path = Path(directory) / "stale.json"
            current = subset.verify_committed(self.config_path)
            current["inputs"][self.config["notification_source"]["path"]] = "0" * 64
            manifest_path.write_text(json.dumps(current), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "manifest is stale"):
                subset.verify_committed(self.config_path, manifest_path=manifest_path)

    def test_committed_missing_or_damaged_font_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "font.ttf"
            with self.assertRaises(FileNotFoundError):
                subset.verify_committed(self.config_path, output_path=output)
            output.write_bytes(b"damaged")
            with self.assertRaisesRegex(ValueError, "differs from approved output"):
                subset.verify_committed(self.config_path, output_path=output)

    def test_new_text_missing_from_charset_is_rejected(self):
        config = json.loads(json.dumps(self.config))
        config["planned_static_strings"].append("龘")
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "font_charset.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "character count changed"):
                subset.verify_committed(config_path)

    def test_input_text_change_blocks_formal_validation(self):
        changed = self.config["notification_source"]["expected_strings"] + ["遗漏字符龘"]
        with mock.patch.object(subset, "parse_c_string_array", return_value=changed):
            with self.assertRaisesRegex(ValueError, "notification strings changed"):
                subset.verify_committed(self.config_path)

    def test_component_text_source_is_bound_to_manifest(self):
        manifest = subset.verify_committed(self.config_path)
        for source in self.config["c_string_sources"]:
            self.assertIn(source["path"], manifest["inputs"])
        original = subset.parse_c_string_array
        def changed(path, name):
            values = original(path, name)
            return values + ["龘"] if name == "demo_texts" else values
        with mock.patch.object(subset, "parse_c_string_array", side_effect=changed):
            with self.assertRaisesRegex(ValueError, "character count changed"):
                subset.verify_committed(self.config_path)

    def test_approved_character_set_and_legacy_messages(self):
        charset, _ = subset.collect_charset(self.config)
        self.assertGreaterEqual(len(charset), 406)
        self.assertEqual(self.config["expected"]["character_count"], len(charset))
        self.assertEqual(self.config["expected"]["charset_sha256"],
                         subset.sha256_bytes(charset.encode("utf-8")))
        for message in self.config["notification_source"]["expected_strings"]:
            self.assertTrue(set(message).issubset(charset))
        self.assertEqual([16, 18, 20, 22, 24, 26, 28], self.config["legacy_sizes_px"])

    def test_product_texts_are_bound_and_cover_visual_contract(self):
        import html
        import re
        source = {"path": "firmware/iwatch/src/gui_core/iw_product_text.h", "array": "iw_product_texts"}
        self.assertIn(source, self.config["c_string_sources"])
        texts = subset.parse_c_string_array(ROOT / source["path"], source["array"])
        contract = (ROOT / "docs/ui/Apple_Watch界面复刻定稿_v2.html").read_text(encoding="utf-8")
        section = re.search(r'<section id="assets">(.*?)</section>', contract, re.S).group(1)
        content = re.search(r'<pre[^>]*>(.*?)</pre>', section, re.S).group(1)
        content = html.unescape(re.sub(r'<[^>]+>', '', content))
        # 定稿中的中文逐条进入编译文案，不仅检查临时子集能够生成。
        for text in content.split():
            if re.search(r'[\u4e00-\u9fff]', text):
                self.assertIn(text, texts)
        original = subset.parse_c_string_array
        def changed(path, name):
            values = original(path, name)
            return values + ["龘"] if name == "iw_product_texts" else values
        with mock.patch.object(subset, "parse_c_string_array", side_effect=changed):
            with self.assertRaisesRegex(ValueError, "character count changed"):
                subset.verify_committed(self.config_path)

    def test_changed_notification_list_is_rejected(self):
        changed = json.loads(json.dumps(self.config, ensure_ascii=False))
        changed["notification_source"]["expected_strings"][0] += "变更"
        with self.assertRaisesRegex(ValueError, "notification strings changed"):
            subset.collect_charset(changed)

    def test_source_font_is_the_approved_input(self):
        source = subset.resolve_inside_root(self.config["source_font"])
        self.assertEqual(self.config["expected"]["source_font_bytes"], source.stat().st_size)
        self.assertEqual(self.config["expected"]["source_font_sha256"], subset.sha256_file(source))

    def test_notification_baseline_matches_character_contract(self):
        baseline = json.loads((FIRMWARE / "tests/baselines/d07_notification_baseline.json").read_text(
            encoding="utf-8"))
        flattened = []
        for item in baseline["notifications"]:
            flattened.extend((item["title"], item["content"]))
        self.assertEqual(self.config["notification_source"]["expected_strings"], flattened)
        self.assertEqual(self.config["legacy_sizes_px"], baseline["legacy_sizes_px"])
        self.assertEqual("1b3ae9ed61fc7415a9938589285041f96269604825f105bee09c96e09096f517",
                         baseline["source"]["sha256"])

    def test_firmware_links_the_versioned_subset(self):
        font_dir = FIRMWARE / "iwatch/src/resource/fonts"
        linked = font_dir / "DroidSansFallback.ttf"
        source = font_dir / "DroidSansFallback.source.ttf"
        manifest = json.loads((font_dir / "DroidSansFallback.subset.json").read_text(
            encoding="utf-8"))
        self.assertEqual(self.config["expected"]["source_font_bytes"], source.stat().st_size)
        self.assertEqual(self.config["expected"]["source_font_sha256"], subset.sha256_file(source))
        self.assertEqual(manifest["output"]["bytes"], linked.stat().st_size)
        self.assertEqual(manifest["output"]["sha256"], subset.sha256_file(linked))
        self.assertEqual(self.config["expected"]["subset_font_sha256"], subset.sha256_file(linked))

    def test_two_independent_generations_are_identical(self):
        try:
            version = importlib.metadata.version("fonttools")
        except importlib.metadata.PackageNotFoundError:
            self.skipTest("FontTools is not installed in this Python runtime")
        if version != self.config["fonttools_version"]:
            self.skipTest("FontTools version does not match the pinned tool")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            first = directory / "first.ttf"
            second = directory / "second.ttf"
            first_manifest = directory / "first.json"
            second_manifest = directory / "second.json"
            subset.generate(self.config_path, first, first_manifest, False)
            subset.generate(self.config_path, second, second_manifest, False)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(first_manifest.read_bytes(), second_manifest.read_bytes())
            subset.generate(self.config_path, first, first_manifest, True)


if __name__ == "__main__":
    unittest.main()
