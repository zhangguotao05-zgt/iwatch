from pathlib import Path
import hashlib
import json
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch


FIRMWARE = Path(__file__).resolve().parents[1]
ROOT = FIRMWARE.parent
sys.path.insert(0, str(FIRMWARE))
import resource_budget as budget


def make_elf(path, font_size=40, image_size=60, duplicate_font=False, font_address=0x1000):
    names_list = ["", ".shstrtab", ".font_data", ".ROM3_IMG.test"]
    if duplicate_font:
        names_list.append(".font_data")
    names = bytearray()
    offsets = []
    for name in names_list:
        offsets.append(len(names))
        names.extend(name.encode("ascii") + b"\0")
    count = len(names_list)
    header = bytearray(52)
    header[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<I", header, 32, 52)
    struct.pack_into("<HHH", header, 46, 40, count, 1)
    data_offset = 52 + count * 40 + len(names)
    sections = [b"\0" * 40]
    sections.append(struct.pack("<10I", offsets[1], 3, 0, 0,
                                52 + count * 40, len(names), 0, 0, 1, 0))
    sections.append(struct.pack("<10I", offsets[2], 1, 0, font_address,
                                data_offset, font_size, 0, 0, 1, 0))
    sections.append(struct.pack("<10I", offsets[3], 1, 0, 0x2000,
                                data_offset + font_size, image_size, 0, 0, 1, 0))
    payload = b"f" * font_size + b"i" * image_size
    if duplicate_font:
        sections.append(struct.pack("<10I", offsets[4], 1, 0, 0x3000,
                                    data_offset + len(payload), 1, 0, 0, 1, 0))
        payload += b"x"
    path.write_bytes(header + b"".join(sections) + names + payload)


def gcc_map(font_size=40, image_size=60):
    return """Linker script and memory map
 .font_data 0x00001000 0x{font:x} build\\DroidSansFallback.o
 .ROM3_IMG.test 0x00002000 0x{image:x} build\\test.tmp.o
 .rodata.glyph_bitmap 0x00003000 0x10 build\\lv_font_montserrat_20.o
""".format(font=font_size, image=image_size)


def keil_map(font_size=40, image_size=60, v00_fonts=False):
    extra = ""
    if v00_fonts:
        for weight in (300, 400, 500, 600):
            name = "IWV00Noto{}".format(weight)
            extra += "    {name}  0x00004000   Data       10  {name}.o(.font_data)\n".format(name=name)
            extra += "    0x00004000   0x00004000   0x0000000a   Data   RO        4    .font_data          {name}.o\n".format(name=name)
    components = ""
    if v00_fonts:
        for weight in (300, 400, 500, 600):
            components += "         0          0         14          0          0          0   IWV00Noto{}.o\n".format(weight)
    return """Component: ARM Compiler 6.16 Tool: armlink [test]
    DroidSansFallback  0x00001000   Data       {font}  DroidSansFallback.o(.font_data)
    0x00001000   0x00001000   0x{font:x}   Data   RO        1    .font_data          DroidSansFallback.o
{extra}
    0x00002000   0x00002000   0x{image:x}   Data   RO        2    .ROM3_IMG.test     test.tmp.o
    0x00003000   0x00003000   0x00000010   Data   RO        3    .rodata.glyph_bitmap lv_font_montserrat_20.o
Image component sizes
      Code (inc. data)   RO Data    RW Data    ZI Data      Debug   Object Name
         0          0         {font_plus}          0          0          0   DroidSansFallback.o
{components}
         0          0         {image}          0          0          0   test.tmp.o
         0          0         16          0          0          0   lv_font_montserrat_20.o
""".format(font=font_size, font_plus=font_size + 4, image=image_size,
           extra=extra, components=components)


class ResourceBudgetTests(unittest.TestCase):
    def test_threshold_is_inclusive_and_one_byte_over_fails(self):
        exact = dict(budget.LIMITS)
        self.assertTrue(all(item["passed"] for item in budget.budget_checks(exact).values()))
        for name, limit in budget.LIMITS.items():
            values = dict(exact)
            values[name] = limit + 1
            self.assertFalse(budget.budget_checks(values)[name]["passed"])

    def test_gcc_elf_and_map_are_cross_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            elf = directory / "main.elf"
            map_path = directory / "main.map"
            make_elf(elf)
            map_path.write_text(gcc_map(), encoding="utf-8")
            result = budget.gcc_resources(elf, map_path)
            self.assertEqual(40, result["subset_ttf_bytes"])
            self.assertEqual(60, result["image_bytes"])
            self.assertEqual(16, result["bitmap_font_bytes"])
            map_path.write_text(gcc_map(image_size=59), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "totals differ"):
                budget.gcc_resources(elf, map_path)

    def test_gcc_rejects_duplicate_unallocated_and_truncated_sections(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            map_path = directory / "main.map"
            map_path.write_text(gcc_map(), encoding="utf-8")
            duplicate = directory / "duplicate.elf"
            make_elf(duplicate, duplicate_font=True)
            with self.assertRaisesRegex(ValueError, "one allocated"):
                budget.gcc_resources(duplicate, map_path)
            unallocated = directory / "unallocated.elf"
            make_elf(unallocated, font_address=0)
            with self.assertRaisesRegex(ValueError, "one allocated"):
                budget.gcc_resources(unallocated, map_path)
            truncated = directory / "truncated.elf"
            make_elf(truncated)
            truncated.write_bytes(truncated.read_bytes()[:-30])
            with self.assertRaisesRegex(ValueError, "exceeds file"):
                budget.gcc_resources(truncated, map_path)

    def test_wrong_map_toolchain_and_missing_keil_table_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            elf = directory / "main.elf"
            map_path = directory / "main.map"
            make_elf(elf)
            map_path.write_text(keil_map(), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "armlink"):
                budget.gcc_resources(elf, map_path)
            map_path.write_text("Component: ARM Compiler 6.16 Tool: armlink [test]\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "component sizes"):
                budget.keil_resources(map_path)

    def test_keil_merged_image_region_is_measured_from_map_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "main.map"
            path.write_text(keil_map(), encoding="utf-8")
            result = budget.keil_resources(path)
            self.assertEqual(40, result["subset_ttf_bytes"])
            self.assertEqual(60, result["image_bytes"])
            self.assertEqual(16, result["bitmap_font_bytes"])
            path.write_text(keil_map(v00_fonts=True), encoding="utf-8")
            self.assertEqual(80, budget.keil_resources(path)["subset_ttf_bytes"])
            path.write_text(keil_map(v00_fonts=True).replace(
                "    IWV00Noto400  0x00004000   Data       10  IWV00Noto400.o(.font_data)\n", ""),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "symbol and input"):
                budget.keil_resources(path)
            path.write_text(keil_map(v00_fonts=True).replace(
                "Image component sizes",
                "    0x00005000   0x00005000   0x0000000a   Data   RO        5    .font_data          UnknownFont.o\nImage component sizes"),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unknown sections"):
                budget.keil_resources(path)

    def test_stale_artifact_identity_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            for name in ("main.bin", "main.map", "main.elf"):
                (build / name).write_bytes(name.encode("ascii"))
            artifacts = {name: hashlib.sha256((build / name).read_bytes()).hexdigest()
                         for name in ("main.bin", "main.map", "main.elf")}
            artifacts["main.map"] = "0" * 64
            (build / "build_identity.json").write_text(json.dumps({
                "compiler": {"name": "gcc"}, "actual_map_toolchain": "gcc",
                "artifacts": artifacts,
            }), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "differs from build identity"):
                budget.verify_identity(build, "gcc")

    def test_report_evidence_does_not_hash_the_mutable_identity_file(self):
        expected = {"main.map", "main.bin", "main.elf"}
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            make_elf(build / "main.elf")
            (build / "main.map").write_text(gcc_map(), encoding="utf-8")
            (build / "main.bin").write_bytes(b"main")
            artifacts = {name: hashlib.sha256((build / name).read_bytes()).hexdigest()
                         for name in expected}
            (build / "build_identity.json").write_text(json.dumps({
                "compiler": {"name": "gcc"}, "actual_map_toolchain": "gcc",
                "profile": "DEV_A128_NAND", "git_head": "abc",
                "inputs": {"sdk_commit": "sdk", "sdk_patch": {"sha256": "a" * 64}},
                "artifacts": artifacts,
            }), encoding="utf-8")
            report = budget.build_report(build, "gcc")
            self.assertEqual(expected, set(report["evidence"]))

    def test_current_archives_have_expected_pre_a1_baseline(self):
        base = FIRMWARE / "iwatch/project/artifacts/DEV_A128_NAND"
        for toolchain, main_size in (("gcc", 5745568), ("keil", 5671520)):
            with self.subTest(toolchain=toolchain):
                archive = base / toolchain
                # 只跳过未生成的工具链目录，已有但不完整的归档仍须失败。
                if not archive.exists():
                    self.skipTest("local {} archive is not present".format(toolchain))
                report = budget.build_report(archive, toolchain)
                if report["git_head"] == "0828250b856f5502c04335a6930bbcf4fc6fdbd7":
                    self.assertFalse(report["passed"])
                    self.assertEqual(3939852, report["resources"]["subset_ttf_bytes"])
                    self.assertEqual(51335, report["resources"]["bitmap_font_bytes"])
                    self.assertEqual(866577, report["resources"]["image_bytes"])
                    self.assertEqual(main_size, report["resources"]["main_bin_bytes"])
                else:
                    self.assertGreater(report["resources"]["subset_ttf_bytes"], 0)

    def test_local_archive_checks_each_present_toolchain(self):
        # 单工具链、双工具链与无归档均独立统计，不让 GCC 决定 Keil 是否可测。
        for present in ((), ("gcc",), ("keil",), ("gcc", "keil")):
            with self.subTest(present=present), tempfile.TemporaryDirectory() as directory:
                firmware = Path(directory)
                base = firmware / "iwatch/project/artifacts/DEV_A128_NAND"
                for toolchain in present:
                    (base / toolchain).mkdir(parents=True)
                report = {"git_head": "fixture", "resources": {"subset_ttf_bytes": 1}}
                case = ResourceBudgetTests("test_current_archives_have_expected_pre_a1_baseline")
                result = unittest.TestResult()
                with patch(__name__ + ".FIRMWARE", firmware), \
                        patch.object(budget, "build_report", return_value=report) as build_report:
                    case.run(result)
                self.assertTrue(result.wasSuccessful(), result.errors + result.failures)
                self.assertEqual(2 - len(present), len(result.skipped))
                self.assertEqual(list(present), [call.args[1] for call in build_report.call_args_list])

    def test_local_incomplete_archive_is_not_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            firmware = Path(directory)
            (firmware / "iwatch/project/artifacts/DEV_A128_NAND/gcc").mkdir(parents=True)
            case = ResourceBudgetTests("test_current_archives_have_expected_pre_a1_baseline")
            result = unittest.TestResult()
            with patch(__name__ + ".FIRMWARE", firmware):
                case.run(result)
            self.assertEqual(1, len(result.errors))
            self.assertIn("FileNotFoundError", result.errors[0][1])
            self.assertEqual(1, len(result.skipped))


if __name__ == "__main__":
    unittest.main()
