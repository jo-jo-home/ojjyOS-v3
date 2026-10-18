#!/usr/bin/env python3
"""Private-disk VZ boot/PCI regression. Created by Jonas Lee.

This is a boot/device check, not the later USB-input/persistence smoke.
Compiles and signs only the probe owned by this test; never opens the
installed writable disk or changes its EFI variable store.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


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
        subprocess.run(["swiftc", "-O", str(root / "vmapp/probe.swift"), "-o", str(probe)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["codesign", "--sign", "-", "--force", "--entitlements",
                        str(root / "vmapp/ojjyOS.entitlements"), str(probe)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    started = time.time()
    with (output / "probe.log").open("w") as log:
        subprocess.run([str(probe), str(disk), str(output / "vars.fd"),
                        str(output / "serial.log"), str(args.seconds)],
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.seconds + 30)
    text = (output / "serial.log").read_text(errors="replace")
    required = ["[PLATFORM] Apple Virtualization", "[VIRTIO-CONSOLE] Serial output attached",
                "attached (PCI)", "[GIC] GICv3 ready", "[VIRTIO-BLK] Attached",
                "[VIRTIO-RNG] Entropy source attached", "[VIRTIO-PCI] 5 virtio device(s) attached",
                "[INIT] Entering main loop", "[NET] DHCP lease"]
    missing = [marker for marker in required if marker not in text]
    if missing or "[PANIC]" in text or "rejected features" in text:
        raise RuntimeError(f"VZ boot/device failure: missing {missing}; see {output / 'serial.log'}")
    epoch = int(re.search(r"clock starts from the firmware time (\d+)", text)[1])
    if abs(epoch - started) > 120:
        raise RuntimeError(f"Firmware clock differs from host by {epoch - started:.0f} seconds")
    if digest(source) != before:
        raise RuntimeError("Source image changed while running the private-disk test")
    report = {"boot": "passed", "pci_devices": 5, "dhcp": "passed",
              "clock_offset_seconds": round(epoch - started, 3),
              "source_sha256": before, "source_unchanged": True,
              "limitations": "No USB input or saved-login assertions in this boot check"}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: VZ boot, five PCI devices, DHCP, firmware clock; evidence: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--seconds", type=int, default=12)
    run(parser.parse_args())
