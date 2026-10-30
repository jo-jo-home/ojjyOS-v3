#!/usr/bin/env python3
"""Exact VZ clock-contract regression fixtures. Created by Jonas Lee."""
import unittest
from vz_boot import check_clock


class ClockContract(unittest.TestCase):
    epoch=1793300000
    def test_corrections(self):
        for offset in [0,999,-999,1493,1418,-1493,2500,-2500]:
            with self.subTest(offset=offset):
                truncated=(abs(offset)//1000)*(1 if offset>=0 else -1)
                check_clock(self.epoch,5000,5001,4921,self.epoch+5+truncated,offset,12)
    def test_read_crosses_second_boundary(self):
        check_clock(self.epoch,5999,6000,4900,self.epoch+6+1,1493,12)
    def test_wrong_calendar_rejected(self):
        for offset in [0,1493,-1493]:
            correction=(abs(offset)//1000)*(1 if offset>=0 else -1)
            for drift in [-2,-1,1,2,3600]:
                with self.subTest(offset=offset,drift=drift),self.assertRaises(RuntimeError):
                    check_clock(self.epoch,5000,5001,4921,self.epoch+5+correction+drift,offset,12)
    def test_timer_gates(self):
        for ms,end,irqs in [(4999,5000,20),(14000,14001,20),(5000,5001,0),
                            (5000,4999,20),(5000,7000,20)]:
            with self.subTest(ms=ms,end=end,irqs=irqs),self.assertRaises(RuntimeError):
                check_clock(self.epoch,ms,end,irqs,self.epoch+ms//1000,0,12)


if __name__=='__main__':unittest.main()
