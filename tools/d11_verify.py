"""从 D11 原始串口数据复核接收连续性和关键板上状态。"""

import argparse
import hashlib
import json
import re
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(folder):
    raw_path = folder / "serial-raw.bin"
    timeline_path = folder / "timeline.jsonl"
    text = raw_path.read_bytes().decode("utf-8", errors="replace")
    events = [json.loads(line) for line in timeline_path.read_text(encoding="utf-8").splitlines()]
    received = [event for event in events if event["event"] == "serial_rx"]
    offset = 0
    for event in received:
        if event["offset"] != offset:
            raise ValueError(f"串口接收偏移不连续：{event['offset']} != {offset}")
        offset += event["bytes"]
    if (not events or offset != raw_path.stat().st_size or
            events[0]["event"] != "capture_start" or events[-1]["event"] != "capture_stop"):
        raise ValueError("串口采集窗口不完整")

    laps = [int(code) for code in re.findall(
        r"chrono result request=\d+ state=3 code=(\d+) opcode=1027", text)]
    paused = re.findall(r"stopwatch revision=103 state=2 elapsed_ms=(\d+) laps=100", text)
    timer = re.findall(
        r"timer id=2 revision=(\d+) state=(\d+) duration_ms=60000 "
        r"remaining_ms=(\d+) occurrence=(\d+) alert=(\d+)", text)
    memory = [int(value) for value in re.findall(r"font main base=\d+ used=(\d+)", text)]
    checks = {
        "lap_capacity": laps.count(1) == 100 and laps.count(7) == 1,
        "lap_state": bool(re.search(r"stopwatch revision=102 state=1 elapsed_ms=\d+ laps=100", text)),
        "pause_stable": len(paused) >= 2 and paused[-1] == paused[-2],
        "reset": "stopwatch revision=104 state=0 elapsed_ms=0 laps=0" in text,
        "timer_expired": any(state == "3" and occurrence == "2" and alert == "1"
                             for _, state, _, occurrence, alert in timer),
        "timer_restarted": any(state == "1" and occurrence == "3"
                               for _, state, _, occurrence, _ in timer),
        "off_page_expired": "timer id=1 revision=2 state=3 duration_ms=3000 remaining_ms=0 occurrence=1 alert=1" in text,
        "memory_stable": len(memory) >= 11 and len(set(memory[-11:])) == 1,
        "service_acks": "submitted=111 duplicate=0 rejected=0 completed=111 ack=111" in text,
        "touch_overflow": "touch overflow=0" in text,
        "brightness": "desired=20/2 applied=20/2" in text and "desired=80/3 applied=80/3" in text,
        "no_fault": re.search(r"HardFault|assert failed|lcd timeout", text, re.IGNORECASE) is None,
    }
    failures = [name for name, passed in checks.items() if not passed]
    if failures:
        raise ValueError("板上复核未通过：" + ", ".join(failures))
    return {
        "planned_source": events[0]["planned_source"],
        "port": events[0]["port"],
        "raw_bytes": offset,
        "raw_sha256": digest(raw_path),
        "timeline_sha256": digest(timeline_path),
        "rx_events": len(received),
        "rx_offsets_contiguous": True,
        "checks": checks,
        "lap_success": laps.count(1),
        "lap_capacity": laps.count(7),
        "paused_elapsed_ms": int(paused[-1]),
        "desktop_main_heap_bytes": memory[-11:],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path, help="含 serial-raw.bin 与 timeline.jsonl 的本地证据目录")
    args = parser.parse_args()
    print(json.dumps(verify(args.folder), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
