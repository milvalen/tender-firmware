#!/usr/bin/env python3
"""Read the TND-01 shift log after a shift and summarise it.

The device prints its log as CSV on the serial command "L" (see
ui-unit/lib/tableboard/src/shiftlog.h). This script reads it from the
device or from a saved dump, writes a CSV file with a wall-clock column
for the last boot, and prints a summary: key presses, alerts, how long
tables stayed in each stage against the thresholds, restarts and battery.

  python3 tools/shift_log.py --port /dev/cu.usbmodem1101 --out shift.csv
  python3 tools/shift_log.py --file dump.txt --at 2026-11-13T23:40

--port needs pyserial (pip install pyserial).

The log holds table numbers, stages and key actions only. A shift log is
still a record of a working day: keep the output outside the public
repositories (for example in tender-vault/_private/).
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import statistics
import sys
import time
from dataclasses import dataclass, field

# Thresholds in minutes, as in tableboard.h Config (approved 2026-11-13).
THRESHOLD_MIN = {"SEAT": 3, "ORD": 10, "MAIN": 15, "BILL": 3}
STAGE_ORDER = ["SEAT", "ORD", "MAIN", "BILL", "PAY"]
BAD_RESETS = {"PANIC", "WATCHDOG", "BROWNOUT"}
COLUMNS = ["boot", "ms", "event", "table", "stage", "detail"]


@dataclass
class Row:
    boot: int
    ms: int
    event: str
    table: int | None
    stage: str
    detail: str
    time: dt.datetime | None = None


@dataclass
class Dump:
    now_ms: int | None = None
    firmware_now: str = ""  # the firmware that printed the dump
    rows: list[Row] = field(default_factory=list)
    end_seen: bool = False
    skipped: int = 0


def parse_dump(lines) -> Dump:
    """Parses the text the device prints for "L". Other lines are ignored."""
    dump = Dump()
    in_log = False
    for raw in lines:
        line = raw.strip()
        if line.startswith("# TND-01 shift log"):
            in_log = True
            dump = Dump()
            for part in line.split():
                if part.startswith("now_ms="):
                    dump.now_ms = int(part.split("=", 1)[1])
                elif part.startswith("fw="):
                    dump.firmware_now = part.split("=", 1)[1]
            continue
        if not in_log:
            continue
        if line.startswith("# end"):
            dump.end_seen = True
            for part in line.split():
                if part.startswith("skipped="):
                    dump.skipped = int(part.split("=", 1)[1])
            break
        if not line or line == ",".join(COLUMNS):
            continue
        cells = line.split(",", 5)
        if len(cells) != 6 or not cells[0].isdigit() or not cells[1].isdigit():
            continue
        dump.rows.append(
            Row(
                boot=int(cells[0]),
                ms=int(cells[1]),
                event=cells[2],
                table=int(cells[3]) if cells[3] else None,
                stage=cells[4],
                detail=cells[5],
            )
        )
    return dump


def add_wall_clock(dump: Dump, read_at: dt.datetime | None) -> None:
    """Gives rows of the last boot a wall-clock time. Older boots have no clock."""
    if read_at is None or dump.now_ms is None or not dump.rows:
        return
    last = dump.rows[-1].boot
    for row in dump.rows:
        if row.boot == last:
            row.time = read_at - dt.timedelta(milliseconds=dump.now_ms - row.ms)


def write_csv(dump: Dump, path: str) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["time"] + COLUMNS)
        for r in dump.rows:
            w.writerow(
                [
                    r.time.isoformat(timespec="seconds") if r.time else "",
                    r.boot,
                    r.ms,
                    r.event,
                    "" if r.table is None else r.table,
                    r.stage,
                    r.detail,
                ]
            )


@dataclass
class Summary:
    boots: int = 0
    firmware: dict[int, str] = field(default_factory=dict)  # boot -> commit
    bad_resets: list[str] = field(default_factory=list)
    presses: dict[str, int] = field(default_factory=dict)
    alerts: dict[str, int] = field(default_factory=dict)
    stage_minutes: dict[str, list[float]] = field(default_factory=dict)
    battery: list[tuple[int, int, int]] = field(default_factory=list)  # boot, ms, level


def summarise(dump: Dump) -> Summary:
    s = Summary()
    entered: dict[tuple[int, int], tuple[str, int]] = {}  # (boot, table) -> (stage, ms)
    last_duration: dict[tuple[int, int], str] = {}  # stage of the last recorded duration
    for r in dump.rows:
        if r.event == "BOOT":
            s.boots += 1
            reason = r.detail.removeprefix("reset=")
            if reason in BAD_RESETS:
                s.bad_resets.append(f"boot {r.boot}: {reason}")
            continue
        if r.event == "FIRMWARE":
            s.firmware[r.boot] = r.detail.removeprefix("commit=")
            continue
        if r.event == "BATTERY":
            if r.detail.isdigit():
                s.battery.append((r.boot, r.ms, int(r.detail)))
            continue
        if r.event == "ALERT":
            s.alerts[r.detail] = s.alerts.get(r.detail, 0) + 1
            continue
        s.presses[r.event] = s.presses.get(r.event, 0) + 1
        if r.table is None:
            continue
        key = (r.boot, r.table)
        if r.event == "STEP":
            before = entered.get(key)
            if before and before[0] != r.stage:  # a STEP that closes a request keeps the stage
                minutes = (r.ms - before[1]) / 60000
                s.stage_minutes.setdefault(before[0], []).append(minutes)
                last_duration[key] = before[0]
            if not before or before[0] != r.stage:
                entered[key] = (r.stage, r.ms)
        elif r.event == "UNDO":
            # The stage goes back; the last duration of this table was a mistake.
            stage = last_duration.pop(key, None)
            if stage and s.stage_minutes.get(stage):
                s.stage_minutes[stage].pop()
            entered.pop(key, None)
    return s


def format_summary(s: Summary, dump: Dump) -> str:
    out = []
    out.append(f"records {len(dump.rows)}, boots {s.boots}, skipped {dump.skipped}")
    if not dump.end_seen:
        out.append("WARNING: the dump has no end line; the log may be cut")
    if s.firmware:
        commits = sorted(set(s.firmware.values()))
        if len(commits) == 1:
            out.append(f"firmware {commits[0]}")
        else:
            out.append("firmware " + ", ".join(f"boot {b}: {c}" for b, c in sorted(s.firmware.items())))
    for bad in s.bad_resets:
        out.append(f"restart: {bad}")
    if s.presses:
        out.append("presses  " + ", ".join(f"{k} {v}" for k, v in sorted(s.presses.items())))
    if s.alerts:
        total = sum(s.alerts.values())
        out.append(f"alerts   {total}: " + ", ".join(f"{k} {v}" for k, v in sorted(s.alerts.items())))
    else:
        out.append("alerts   0")
    out.append("stage    tables  median  max   threshold  over")
    for stage in STAGE_ORDER:
        times = s.stage_minutes.get(stage)
        if not times:
            continue
        th = THRESHOLD_MIN.get(stage)
        over = sum(1 for t in times if th is not None and t >= th)
        out.append(
            f"{stage:<8} {len(times):>6}  {statistics.median(times):5.1f}  {max(times):4.1f}  "
            f"{(str(th) + ' min') if th else '-':>9}  {over if th else '-':>4}"
        )
    for boot in sorted({b for b, _, _ in s.battery}):
        readings = [(ms, level) for b, ms, level in s.battery if b == boot]
        if len(readings) < 2:
            continue
        (ms0, l0), (ms1, l1) = readings[0], readings[-1]
        hours = (ms1 - ms0) / 3_600_000
        rate = f", {(l0 - l1) / hours:.1f} %/h" if hours > 0 else ""
        out.append(f"battery  boot {boot}: {l0}% -> {l1}% in {hours:.1f} h{rate}")
    return "\n".join(out)


def read_port(port: str, timeout_s: float) -> tuple[list[str], dt.datetime]:
    try:
        import serial  # pyserial
    except ImportError:
        sys.exit("--port needs pyserial: pip install pyserial")
    lines: list[str] = []
    with serial.Serial(port, 115200, timeout=1) as ser:
        ser.reset_input_buffer()
        ser.write(b"L")
        read_at = dt.datetime.now().astimezone()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").rstrip("\r\n")
            lines.append(line)
            if line.startswith("# end"):
                break
    return lines, read_at


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port of the device")
    src.add_argument("--file", help="a saved dump (the text the device prints for L)")
    ap.add_argument("--out", help="CSV file to write")
    ap.add_argument("--at", help="for --file: when the dump was read, ISO time (gives the wall clock)")
    ap.add_argument("--timeout", type=float, default=60, help="seconds to wait for the dump (--port)")
    args = ap.parse_args(argv)

    if args.port:
        lines, read_at = read_port(args.port, args.timeout)
    else:
        with open(args.file, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
        read_at = dt.datetime.fromisoformat(args.at) if args.at else None

    dump = parse_dump(lines)
    if dump.now_ms is None:
        print("no shift log found in the input", file=sys.stderr)
        return 1
    add_wall_clock(dump, read_at)
    if args.out:
        write_csv(dump, args.out)
    print(format_summary(summarise(dump), dump))
    return 0


if __name__ == "__main__":
    sys.exit(main())
