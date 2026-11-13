#!/usr/bin/env python3
"""Tests for shift_log.py. Run: python3 -I tools/test_shift_log.py"""

import datetime as dt
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import shift_log  # noqa: E402

# The lines match the formats checked in ui-unit/test/test_shiftlog.cpp.
DUMP = """\
TND-01: a/s/d/f = A/B/red/blue, uppercase = hold, ? = state
# TND-01 shift log v1 now_ms=4000000 fw=ec29f0ba
boot,ms,event,table,stage,detail
1,100,BOOT,,,reset=POWERON
1,100,FIRMWARE,,,commit=d5a8b011
1,5000,BATTERY,,,90
1,60000,STEP,7,SEAT,request=0 muted=0
1,240000,ALERT,7,SEAT,TAKE ORDER
1,250000,STEP,7,ORD,request=0 muted=0
1,300000,NEXT,8,FREE,request=0 muted=0
1,310000,STEP,8,SEAT,request=0 muted=0
1,400000,STEP,8,ORD,request=0 muted=0
1,410000,UNDO,8,SEAT,request=0 muted=0
1,420000,STEP,8,ORD,request=0 muted=0
1,700000,STEP,7,MAIN,request=0 muted=0
1,1600000,STEP,7,BILL,request=0 muted=0
1,1690000,STEP,7,PAY,request=0 muted=0
1,2000000,STEP,7,FREE,request=0 muted=0
1,1805000,BATTERY,,,84
2,90,BOOT,,,reset=BROWNOUT
2,90,FIRMWARE,,,commit=ec29f0ba
2,3000,MUTE,8,ORD,request=0 muted=1
2,3600100,BATTERY,,,80
2,3600200,ALERT,8,ORD,GUEST REQUEST
# end records=21 skipped=1 boots=2 log=ok
garbage after the end
"""


class ShiftLogTest(unittest.TestCase):
    def setUp(self):
        self.dump = shift_log.parse_dump(DUMP.splitlines())

    def test_parse(self):
        d = self.dump
        self.assertEqual(d.now_ms, 4000000)
        self.assertTrue(d.end_seen)
        self.assertEqual(d.skipped, 1)
        self.assertEqual(len(d.rows), 21)
        self.assertEqual(d.firmware_now, "ec29f0ba")
        self.assertEqual(d.rows[0].event, "BOOT")
        self.assertIsNone(d.rows[0].table)
        self.assertEqual(d.rows[3].table, 7)
        self.assertEqual(d.rows[3].detail, "request=0 muted=0")

    def test_summary(self):
        s = shift_log.summarise(self.dump)
        self.assertEqual(s.boots, 2)
        self.assertEqual(s.bad_resets, ["boot 2: BROWNOUT"])
        self.assertEqual(s.presses["STEP"], 9)
        self.assertEqual(s.presses["UNDO"], 1)
        self.assertEqual(s.alerts, {"TAKE ORDER": 1, "GUEST REQUEST": 1})
        # TBL-07: SEAT 1:00 -> ORD 4:10 is 3.17 min; TBL-08: the UNDO drops
        # the 1.5 min SEAT time, and the time after the UNDO is unknown.
        self.assertEqual([round(m, 2) for m in s.stage_minutes["SEAT"]], [3.17])
        self.assertEqual([round(m, 2) for m in s.stage_minutes["MAIN"]], [15.0])
        self.assertEqual([round(m, 2) for m in s.stage_minutes["BILL"]], [1.5])
        self.assertEqual([round(m, 2) for m in s.stage_minutes["PAY"]], [5.17])
        text = shift_log.format_summary(s, self.dump)
        self.assertIn("restart: boot 2: BROWNOUT", text)
        self.assertIn("firmware boot 1: d5a8b011, boot 2: ec29f0ba", text)
        self.assertIn("alerts   2", text)
        self.assertIn("SEAT          1    3.2   3.2      3 min     1", text)
        self.assertIn("PAY           1    5.2   5.2          -     -", text)
        self.assertIn("battery  boot 1: 90% -> 84% in 0.5 h, 12.0 %/h", text)
        self.assertNotIn("boot 2: 80%", text)  # one reading only

    def test_wall_clock_only_for_the_last_boot(self):
        read_at = dt.datetime(2026, 11, 13, 23, 40, 0)
        shift_log.add_wall_clock(self.dump, read_at)
        first, last = self.dump.rows[0], self.dump.rows[-1]
        self.assertIsNone(first.time)  # boot 1: no clock
        self.assertEqual(last.time, read_at - dt.timedelta(milliseconds=4000000 - 3600200))

    def test_cut_dump_and_no_dump(self):
        cut = shift_log.parse_dump(DUMP.splitlines()[:6])
        self.assertFalse(cut.end_seen)
        self.assertIn("WARNING", shift_log.format_summary(shift_log.summarise(cut), cut))
        self.assertIsNone(shift_log.parse_dump(["hello", "1,2,STEP,3,SEAT,x"]).now_ms)

    def test_main_writes_csv(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "dump.txt")
            out = os.path.join(tmp, "shift.csv")
            with open(src, "w", encoding="utf-8") as f:
                f.write(DUMP)
            with open(os.devnull, "w") as devnull:
                old, sys.stdout = sys.stdout, devnull
                try:
                    rc = shift_log.main(["--file", src, "--out", out, "--at", "2026-11-13T23:40:00"])
                finally:
                    sys.stdout = old
            self.assertEqual(rc, 0)
            with open(out, encoding="utf-8") as f:
                lines = f.read().splitlines()
            self.assertEqual(lines[0], "time,boot,ms,event,table,stage,detail")
            self.assertEqual(lines[1], ",1,100,BOOT,,,reset=POWERON")
            self.assertEqual(lines[-1], "2026-11-13T23:33:20,2,3600200,ALERT,8,ORD,GUEST REQUEST")
            self.assertEqual(len(lines), 22)


if __name__ == "__main__":
    unittest.main()
