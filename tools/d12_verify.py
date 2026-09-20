"""复核 D12 原始串口窗口、容量边界与实机回收结果。"""

import argparse
import hashlib
import json
import re
from pathlib import Path


def _digest(data):
    return hashlib.sha256(data).hexdigest()


def _result(text, request, opcode, code):
    pattern = (rf"d12 result request={request} state=3 code={code} "
               rf"opcode={opcode} revision=\d+")
    return re.search(pattern, text) is not None


def _snapshot(lines):
    header = "alerts revision=26 count=24 output=0"
    try:
        start = lines.index(header) + 1
    except ValueError as error:
        raise ValueError("缺少 24 条提醒的到期快照") from error
    records = lines[start:start + 24]
    found = set()
    for line in records:
        match = re.fullmatch(
            r"alert source=(\d+) id=(\d+) occurrence=(\d+) "
            r"state=[12] missed=0 first_mono_ms=\d+", line)
        if not match:
            raise ValueError("24 条提醒快照包含缺失或异常记录")
        found.add(tuple(map(int, match.groups())))
    expected = {(source, index, index)
                for source, count in ((1, 8), (2, 16))
                for index in range(1, count + 1)}
    if found != expected:
        raise ValueError("8 个计时器和 16 个闹钟未独立保留")


def _root_memory(text, sequence):
    marker = f"nav state=3 seq={sequence} current=0002:0"
    start = text.find(marker)
    end = text.find("nav request action", start + len(marker))
    section = text[start:end if end >= 0 else None] if start >= 0 else ""
    match = re.search(r"font main base=\d+ used=(\d+)[^\n]*"
                      r"\nfont ttf base=\d+ used=(\d+)", section)
    if not match:
        raise ValueError(f"缺少根页面 seq={sequence} 的双内存采样")
    return tuple(map(int, match.groups()))


def verify(folder, expected_sha256=None):
    raw = (folder / "serial-raw.bin").read_bytes()
    digest = _digest(raw)
    if expected_sha256 and digest.lower() != expected_sha256.lower():
        raise ValueError("原始串口哈希与归档记录不一致")
    text = raw.decode("utf-8", errors="replace").replace("\r", "")
    lines = text.splitlines()
    events = [json.loads(line) for line in
              (folder / "timeline.jsonl").read_text(encoding="utf-8").splitlines()]
    if (not events or events[0].get("event") != "capture_start" or
            events[-1].get("event") != "capture_stop" or
            events[0].get("port") != "COM10"):
        raise ValueError("COM10 采集窗口不完整")
    offset = received = 0
    for event in events:
        if event.get("event") in ("reader_failed", "serial_disconnected"):
            raise ValueError("串口采集中断")
        if event.get("event") != "serial_rx":
            continue
        if event.get("offset") != offset or event.get("bytes", 0) <= 0:
            raise ValueError("原始串口偏移不连续")
        offset += event["bytes"]
        received += 1
    if not received or offset != len(raw):
        raise ValueError("原始串口长度与接收时间线不一致")

    _snapshot(lines)
    if not (_result(text, 25, 768, 7) and
            re.search(r"chrono result request=26 state=3 code=7 opcode=256", text)):
        raise ValueError("满容量创建拒绝未得到终态结果")
    for request in range(27, 51):
        if not _result(text, request, 512, 1):
            raise ValueError(f"提醒确认 {request} 缺少成功终态")
    if ("alerts revision=73 count=0 output=0" not in text or
            "alarms revision=53 count=0 missed_late=0" not in text or
            "submitted=69 duplicate=0 rejected=0 completed=69 ack=69" not in text):
        raise ValueError("提醒、闹钟或命令账本未最终清空")
    touch = [int(value) for value in re.findall(r"\btouch overflow=(\d+)\b", text)]
    if not touch or any(touch):
        raise ValueError("采集窗口含触摸溢出或缺少采样")
    if re.search(r"HardFault|Assertion failed", text, re.IGNORECASE):
        raise ValueError("采集窗口出现崩溃或断言")
    timeouts = list(re.finditer(r"draw_core timeout", text, re.IGNORECASE))
    display_timeouts = len(timeouts)
    display = list(re.finditer(
        r"display reassert=\d+ recovery=(\d+) fault_pending=(\d+)", text))
    attempts = len(re.findall(r"LCD draw failed; recovery attempt \d+", text))
    if (not display or int(display[-1].group(2)) != 0 or
            int(display[-1].group(1)) != display_timeouts or
            attempts != display_timeouts):
        raise ValueError("显示恢复未闭环")
    for sample in display:
        preceding = sum(fault.start() < sample.start() for fault in timeouts)
        if int(sample.group(1)) < preceding:
            raise ValueError("显示故障后的采样缺少恢复计数")
    roots = [_root_memory(text, sequence) for sequence in (8, 10)]
    if roots[0] != roots[1]:
        raise ValueError("同状态重复进退的主堆或字形池持续增长")
    return {
        "raw_bytes": len(raw), "raw_sha256": digest,
        "timeline_sha256": _digest((folder / "timeline.jsonl").read_bytes()),
        "rx_events": received, "commands": sum(e.get("event") == "command_sent" for e in events),
        "alert_capacity": "8 timers + 16 alarms", "capacity_rejections": 2,
        "alert_acks": 24, "final_alarms": 0, "final_alerts": 0,
        "touch_samples": len(touch), "touch_overflow": 0,
        "stable_root_heap": {"main": roots[0][0], "glyph_pool": roots[0][1]},
        "display_draw_timeouts": display_timeouts,
        "display_recovered": bool(display_timeouts),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--raw-sha256")
    args = parser.parse_args()
    print(json.dumps(verify(args.folder, args.raw_sha256), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
