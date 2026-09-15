import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_layout as layout


class LayoutTests(unittest.TestCase):
    def setUp(self):
        self.table = json.loads(layout.TABLE.read_text(encoding="utf-8"))
        self.capacities = layout.board_capacities(layout.BOARD)

    def bank(self, name):
        return next(bank for bank in self.table if bank["mem"] == name)

    def test_checked_in_layout(self):
        self.assertEqual([], layout.validate_layout_sources())
        self.assertEqual([], layout.validate_layout(self.table, self.capacities))

    def test_original_psram_overflow_is_rejected(self):
        data = next(r for r in self.bank("psram1")["regions"] if "PSRAM_DATA" in r["tags"])
        data["max_size"] = "0x00C00000"
        self.assertTrue(any("capacity" in e for e in layout.validate_layout(self.table, self.capacities)))

    def test_overlapping_nand_data_is_rejected(self):
        main = next(r for r in self.bank("flash4")["regions"] if r.get("img") == "main")
        main["offset"] = "0x00000000"
        self.assertTrue(any("overlaps" in e for e in layout.validate_layout(self.table, self.capacities)))

    def test_run_window_cannot_outgrow_load_slot(self):
        main = next(r for r in self.bank("flash4")["regions"] if r.get("img") == "main")
        main["max_size"] = "0x00400000"
        self.assertTrue(any("link window" in e for e in layout.validate_layout(self.table, self.capacities)))

    def test_alias_budget_mismatch_is_rejected(self):
        self.bank("psram1_cbus")["regions"][0]["max_size"] = "0x00400000"
        self.assertTrue(any("mismatch" in e for e in layout.validate_layout(self.table, self.capacities)))

    def test_smaller_board_cannot_silently_build(self):
        self.capacities["psram2"] = 8 * layout.MIB
        self.assertTrue(layout.validate_layout(self.table, self.capacities))

    def test_missing_bank_is_rejected(self):
        self.table.remove(self.bank("flash4"))
        self.assertTrue(layout.validate_layout(self.table, self.capacities))

    def test_images_sizes_addresses_and_missing_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            manifest = {"write_flash": {"files": []}}
            for bank in self.table:
                for r in bank["regions"]:
                    name = r.get("img")
                    if not bank["mem"].startswith("flash") or name not in ("main", "bootloader", "ftab"):
                        continue
                    (build / f"{name}.bin").write_bytes(b"test")
                    manifest["write_flash"]["files"].append({
                        "path": f"{name}.bin", "address": hex(layout.number(bank["base"]) + layout.number(r["offset"]))})
            config = build / "sftool_param.json"
            config.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual([], layout.validate_images(self.table, build))

            with (build / "main.bin").open("wb") as image:
                image.truncate(6 * layout.MIB)
            self.assertEqual([], layout.validate_images(self.table, build))
            with (build / "main.bin").open("ab") as image:
                image.write(b"x")
            self.assertTrue(any("exceeds" in e for e in layout.validate_images(self.table, build)))
            (build / "main.bin").write_bytes(b"test")

            stale = copy.deepcopy(manifest)
            next(i for i in stale["write_flash"]["files"] if i["path"] == "main.bin")["address"] = "0x68000000"
            config.write_text(json.dumps(stale), encoding="utf-8")
            self.assertTrue(any("address" in e for e in layout.validate_images(self.table, build)))
            config.write_text(json.dumps(manifest), encoding="utf-8")
            (build / "ftab.bin").unlink()
            self.assertTrue(any("missing image" in e for e in layout.validate_images(self.table, build)))


if __name__ == "__main__":
    unittest.main()
