#!/usr/bin/env python3
"""Private-disk VZ boot/PCI regression. Created by Jonas Lee.

This is a boot/device check, not the later USB-input/persistence smoke.
Compiles and signs only the probe owned by this test; never opens the
installed writable disk or changes its EFI variable store.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def check_clock(epoch, ms, sample_end_ms, irqs, calendar_epoch, offset_ms, seconds):
    """Clock contract isolated for deterministic correction/regression tests."""
    if not (5000 <= ms <= seconds * 1000 + 1000) or irqs == 0:
        raise RuntimeError(f"Virtual timer failed: uptime={ms}ms, delivered IRQs={irqs}")
    if not ms <= sample_end_ms <= ms + 100:
        raise RuntimeError("RTC sample interval invalid or unexpectedly slow")
    # clock_adjust_rtc adds whole offset seconds, truncating toward zero
    # (C integer division, including negative sub-second corrections).
    correction=(abs(offset_ms)//1000)*(1 if offset_ms>=0 else -1)
    if not epoch + ms // 1000 + correction <= calendar_epoch <= epoch + sample_end_ms // 1000 + correction:
        raise RuntimeError("RTC calendar did not advance by guest uptime")


def run(args):
    root = Path(__file__).resolve().parents[1]
    source = Path(args.image).resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    disk = output / "disk.img"
    if disk.exists():
        raise RuntimeError("Choose a new output directory; test evidence is never overwritten")
    before = digest(source)
    shutil.copyfile(source, disk)
    probe = output / "probe"
    with (output / "build.log").open("w") as log:
        subprocess.run(["swiftc", "-O", "-warnings-as-errors", str(root / "vmapp/LaunchSettings.swift"), str(root / "vmapp/probe.swift"), "-o", str(probe)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["codesign", "--sign", "-", "--force", "--entitlements",
                        str(root / "vmapp/ojjyOS.entitlements"), str(probe)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    started = time.time()
    with (output / "probe.log").open("w") as log:
        subprocess.run([str(probe), str(disk), str(output / "vars.fd"),
                        str(output / "serial.log"), str(args.seconds),
                        "--memory", str(args.memory), "--screen", args.screen],
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.seconds + 30)
    text = (output / "serial.log").read_text(errors="replace")
    required = ["[PLATFORM] Apple Virtualization", "[VIRTIO-CONSOLE] Serial output attached",
                "attached (PCI)", "[GIC] GICv3 ready", "[VIRTIO-BLK] Attached",
                "[VIRTIO-RNG] Entropy source attached", "[VIRTIO-PCI] 5 virtio device(s) attached",
                "[USB-HID] Boot keyboard attached", "[USB-HID] Absolute pointer attached",
                "[INIT] Entering main loop", "[NET] DHCP lease"]
    missing = [marker for marker in required if marker not in text]
    if missing or "[PANIC]" in text or "rejected features" in text:
        raise RuntimeError(f"VZ boot/device failure: missing {missing}; see {output / 'serial.log'}")
    epoch = int(re.search(r"clock starts from the firmware time (\d+)", text)[1])
    if abs(epoch - started) > 120:
        raise RuntimeError(f"Firmware clock differs from host by {epoch - started:.0f} seconds")
    health = re.search(r"\[TIMER\] Health ms=(\d+) irqs=(\d+) clock=([\d:T-]+) end_ms=(\d+) ntp_ms=(-?\d+)", text)
    if not health:
        raise RuntimeError("No virtual-timer/clock health sample; see serial.log")
    ms, irqs = int(health[1]), int(health[2])
    clock_epoch = int(datetime(*map(int, re.split(r"\D+", health[3])), tzinfo=timezone.utc).timestamp())
    end_ms,ntp_ms=int(health[4]),int(health[5])
    check_clock(epoch,ms,end_ms,irqs,clock_epoch,ntp_ms,args.seconds)
    if digest(source) != before:
        raise RuntimeError("Source image changed while running the private-disk test")
    report = {"boot": "passed", "virtio_devices": 5, "usb_hid_enumerated": 2, "dhcp": "passed",
              "memory_mb_requested": args.memory, "screen": args.screen,
              "pmm_usable_mb": int(re.search(r"\[PMM\] Total usable memory: (\d+) MB", text)[1]),
              "clock_offset_seconds": round(epoch - started, 3),
               "timer_sample_ms": ms, "timer_irqs": irqs, "clock_progression": "passed",
              "timer_sample_end_ms": end_ms, "ntp_offset_ms_at_sample": ntp_ms,
              "source_sha256": before, "source_unchanged": True,
              "limitations": "No USB input or saved-login assertions in this boot check"}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: VZ boot, five PCI devices, DHCP, firmware clock; evidence: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--seconds", type=int, default=12)
    parser.add_argument("--memory", type=int, default=2048)
    parser.add_argument("--screen", choices=["1440x900", "1280x800", "1600x900"], default="1440x900")
    run(parser.parse_args())
