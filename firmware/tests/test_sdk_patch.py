from pathlib import Path
import hashlib
import json
import os
import sys
import tempfile
import unittest


FIRMWARE = Path(__file__).resolve().parents[1]
ROOT = FIRMWARE.parent
sys.path.insert(0, str(FIRMWARE))
import sdk_patch


class SdkPatchTests(unittest.TestCase):
    def test_manifest_has_exact_whitelist_and_valid_patch_hash(self):
        manifest, patch = sdk_patch.load_manifest()
        self.assertEqual(12, len(manifest["files"]))
        self.assertTrue(patch.is_file())
        self.assertIn("external/lvgl_v9/src/libs/tiny_ttf/lv_tiny_ttf.c",
                      {item["path"] for item in manifest["files"]})
        self.assertIn("middleware/lvgl/lv_drivers_v9/sifli/lv_draw_epic_label.c",
                      {item["path"] for item in manifest["files"]})

    def test_line_endings_do_not_change_declared_file_hash(self):
        self.assertEqual(sdk_patch.normalized_sha256(b"a\nb\n"),
                         sdk_patch.normalized_sha256(b"a\r\nb\r\n"))

    def test_tampered_manifest_is_rejected(self):
        manifest = json.loads(sdk_patch.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
        manifest["patch_sha256"] = "0" * 64
        with tempfile.NamedTemporaryFile("w", suffix=".json", encoding="utf-8", delete=False) as stream:
            json.dump(manifest, stream)
            path = Path(stream.name)
        try:
            with self.assertRaisesRegex(ValueError, "patch hash"):
                sdk_patch.load_manifest(path)
        finally:
            path.unlink()

    def test_manifest_path_escape_is_rejected(self):
        manifest = json.loads(sdk_patch.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
        manifest["files"][0]["path"] = "../outside.c"
        patch = ROOT / manifest["patch_file"]
        manifest["patch_sha256"] = hashlib.sha256(patch.read_bytes()).hexdigest()
        with tempfile.NamedTemporaryFile("w", suffix=".json", encoding="utf-8", delete=False) as stream:
            json.dump(manifest, stream)
            path = Path(stream.name)
        try:
            with self.assertRaisesRegex(ValueError, "invalid SDK patch path"):
                sdk_patch.load_manifest(path)
        finally:
            path.unlink()

    def test_local_fixed_and_derived_sdk_if_present(self):
        base = Path(os.environ.get("IWATCH_BASE_SDK", r"C:\OpenSiFli\SiFli-SDK-v2.5.1-iwatch-locked"))
        derived = Path(os.environ.get("IWATCH_PATCHED_SDK", str(ROOT / "work/sdk-d07-a0")))
        if not (base / ".git").exists() and not (base / "HEAD").exists():
            self.skipTest("local fixed SDK is not present")
        self.assertEqual("base", sdk_patch.verify_base(base)["mode"])
        if derived.exists():
            self.assertEqual("patched", sdk_patch.verify_derived(derived)["mode"])


if __name__ == "__main__":
    unittest.main()
