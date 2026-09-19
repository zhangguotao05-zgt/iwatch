"""验证 D11 串口复核器能够拒绝计数溢出与实际固件故障格式。"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from d11_verify import verify


VALID = "\n".join([
    *[f"chrono result request={i} state=3 code=1 opcode=1027" for i in range(1, 101)],
    "chrono result request=101 state=3 code=7 opcode=1027",
    "stopwatch revision=102 state=1 elapsed_ms=123456 laps=100",
    "stopwatch revision=103 state=2 elapsed_ms=118011 laps=100",
    "stopwatch revision=103 state=2 elapsed_ms=118011 laps=100",
    "stopwatch revision=104 state=0 elapsed_ms=0 laps=0",
    "timer id=2 revision=3 state=3 duration_ms=60000 remaining_ms=0 occurrence=2 alert=1",
    "timer id=2 revision=4 state=1 duration_ms=60000 remaining_ms=60000 occurrence=3 alert=0",
    "timer id=1 revision=2 state=3 duration_ms=3000 remaining_ms=0 occurrence=1 alert=1",
    *["font main base=1 used=80020" for _ in range(11)],
    "submitted=111 duplicate=0 rejected=0 completed=111 ack=111",
    "touch overflow=0 discarded=0",
    "desired=20/2 applied=20/2",
    "desired=80/3 applied=80/3",
]) + "\n"


class D11VerifyTests(unittest.TestCase):
    def check_log(self, contents):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            raw = contents.encode("utf-8")
            (folder / "serial-raw.bin").write_bytes(raw)
            events = [
                {"event": "capture_start", "planned_source": "test", "port": "COM10"},
                {"event": "serial_rx", "offset": 0, "bytes": len(raw)},
                {"event": "capture_stop"},
            ]
            (folder / "timeline.jsonl").write_text(
                "\n".join(json.dumps(event) for event in events), encoding="utf-8")
            return verify(folder)

    def test_normal_log(self):
        self.assertEqual(self.check_log(VALID)["touch_samples"], 1)

    def test_rejects_every_nonzero_overflow_sample(self):
        for changed in ("touch overflow=1\n" + VALID,
                        VALID + "touch overflow=1\n",
                        VALID.replace("touch overflow=0", "touch overflow=1")):
            with self.subTest(changed=changed[:25]):
                with self.assertRaisesRegex(ValueError, "touch_overflow"):
                    self.check_log(changed)
        with self.assertRaisesRegex(ValueError, "touch_overflow"):
            self.check_log(VALID.replace("touch overflow=0 discarded=0\n", ""))

    def test_rejects_real_fault_formats(self):
        for fault in ("Assertion failed at function:draw_core, line number:1735 ,(0)",
                      "[21627] E/drv.lcd lcd_task: draw_core timeout",
                      "HardFault", "lcd timeout"):
            with self.subTest(fault=fault):
                with self.assertRaisesRegex(ValueError, "no_fault"):
                    self.check_log(VALID + fault + "\n")


if __name__ == "__main__":
    unittest.main()
