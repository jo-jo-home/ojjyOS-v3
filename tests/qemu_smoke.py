#!/usr/bin/env python3
"""Isolated live boot/input/persistence checks. Created by Jonas Lee.

Always runs against a private copy of --image, never a user's live disk.
Evidence (serial logs, screenshots, and the test disk) stays in --output.
Requires fresh data for setup; --fresh-data resets ONLY the private test copy.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time


class VM:
    def __init__(self, arch, disk, output, run):
        self.log = output / f"serial-{run}.log"
        # AF_UNIX paths on macOS are limited to 104 bytes. Use a private,
        # short temporary directory rather than a shared monitor pathname.
        self.socket_dir = tempfile.TemporaryDirectory(prefix="oq-", dir=os.environ.get("OJJY_QMP_TMP"))
        self.socket_path = Path(self.socket_dir.name) / "q.sock"
        self.connection = self.stream = None
        fw_name = "aarch64" if arch == "aarch64" else "x86_64"
        firmware = next((p / f"edk2-{fw_name}-code.fd" for p in
                         [Path("/opt/homebrew/share/qemu"), Path("/usr/local/share/qemu"), Path("/usr/share/qemu")]
                         if (p / f"edk2-{fw_name}-code.fd").exists()), None)
        if firmware is None:
            raise RuntimeError("Cannot find QEMU edk2 firmware")
        command = [f"qemu-system-{arch}", "-m", "2048", "-smp", "1", "-accel", "tcg",
                   "-display", "none", "-no-reboot",
                   "-serial", f"file:{self.log}", "-qmp", f"unix:{self.socket_path},server=on,wait=off"]
        if arch == "x86_64" and (firmware.parent / "edk2-i386-vars.fd").exists():
            # Homebrew ships code/vars totaling 4MiB; code alone cannot be
            # passed to -bios (its size is not a power of two).
            combined = output / f"firmware-{run}.fd"
            combined.write_bytes((firmware.parent / "edk2-i386-vars.fd").read_bytes() + firmware.read_bytes())
            command += ["-drive", f"if=pflash,format=raw,file={combined}"]
        else:
            command += ["-bios", str(firmware)]
        if arch == "aarch64":
            command += ["-M", "virt,gic-version=2", "-cpu", "cortex-a72",
                        "-global", "virtio-mmio.force-legacy=false", "-device", "ramfb",
                        "-device", "virtio-keyboard-device", "-device", "virtio-tablet-device",
                        "-drive", f"file={disk},format=raw,if=none,id=hd,cache=writeback",
                        "-device", "virtio-blk-device,drive=hd"]
        else:
            command += ["-M", "pc", "-vga", "std", "-drive", f"file={disk},format=raw,cache=writeback"]
        self.stderr = (output / f"qemu-{run}.log").open("wb")
        self.process = subprocess.Popen(command, stdout=self.stderr, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 15
            while not self.socket_path.exists():
                if self.process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("QEMU did not open its monitor")
                time.sleep(0.05)
            self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.connection.settimeout(15)
            while True:
                try:
                    self.connection.connect(str(self.socket_path))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    if self.process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError(f"QEMU monitor unavailable; see {output / f'qemu-{run}.log'}")
                    time.sleep(0.05)
            self.stream = self.connection.makefile("rb")
            json.loads(self.stream.readline())
            self.request_id = 0
            self.qmp("qmp_capabilities")
        except BaseException:
            self.close()
            raise

    def qmp(self, command, arguments=None):
        self.request_id += 1
        request = {"execute": command, "id": self.request_id}
        if arguments is not None:
            request["arguments"] = arguments
        self.connection.sendall(json.dumps(request).encode() + b"\n")
        while True:
            response = json.loads(self.stream.readline())
            if response.get("id") == self.request_id:
                if "error" in response:
                    raise RuntimeError(response["error"])
                return response.get("return")

    def text(self):
        return self.log.read_text(errors="replace") if self.log.exists() else ""

    def wait(self, text, timeout=100, after=0):
        deadline = time.monotonic() + timeout
        while text not in self.text()[after:]:
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(f"Guest did not produce {text!r}; see {self.log}")
            time.sleep(0.05)

    def key(self, code, down):
        self.qmp("input-send-event", {"events": [{"type": "key", "data": {
            "down": down, "key": {"type": "qcode", "data": code}}}]})

    def press(self, code, pace=0.08):
        self.key(code, True)
        self.key(code, False)
        time.sleep(pace)

    def type(self, text, pace=0.08):
        for char in text:
            code = {" ": "spc", "\n": "ret", "\t": "tab", "/": "slash", "-": "minus", ".": "dot"}.get(char, char)
            self.press(code, pace)

    def console(self):
        start = len(self.text())
        self.key("shift", True)
        self.press("esc")
        self.key("shift", False)
        self.wait("Type 'help' for available commands.", after=start, timeout=15)

    def screenshot(self, path):
        self.qmp("screendump", {"filename": str(path), "format": "png"})

    def close(self):
        if self.process.poll() is None:
            # Only the child launched by this instance is ever stopped.
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill(); self.process.wait()
        if self.stream: self.stream.close()
        if self.connection: self.connection.close()
        self.stderr.close()
        self.socket_path.unlink(missing_ok=True)
        self.socket_dir.cleanup()


def run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    disk = output / "test-disk.img"
    if disk.exists():
        raise RuntimeError("Output already contains a test disk; choose a new directory")
    shutil.copyfile(Path(args.image).resolve(), disk)
    if args.fresh_data:
        with disk.open("r+b") as file:
            file.seek(67584 * 512)
            remaining = (262144 - 34 - 67584 + 1) * 512
            zeros = bytes(1024 * 1024)
            while remaining:
                chunk = min(remaining, len(zeros))
                file.write(zeros[:chunk])
                remaining -= chunk
    vm = VM(args.arch, disk, output, 1)
    try:
        vm.wait("Entering main loop")
        print(f"{args.arch}: boot reached main loop", flush=True)
        if "Accounts: 0 registered" not in vm.text():
            raise RuntimeError("Setup test requires fresh data; --fresh-data resets only the private test copy")
        vm.type("oooo")
        for _ in range(4): vm.press("backspace")
        for field in ["jonas", "tester", "testpass", "testpass"]:
            vm.type(field)
            vm.press("tab")
        # The four Tabs returned focus to the first field; Enter submits all.
        vm.press("ret")
        vm.wait("Created account 'tester' (jonas)", timeout=30)
        vm.wait("User 'tester' logged in", timeout=30)
        vm.wait("[UI] Desktop presented", timeout=30)
        print(f"{args.arch}: setup typing, Backspace, account creation/login passed", flush=True)
        time.sleep(1)
        vm.screenshot(output / "desktop.png")
        vm.console()
        start = len(vm.text())
        vm.type("oooo")
        for _ in range(4): vm.press("backspace")
        vm.type("whoami\n")
        vm.wait("tester (jonas)", after=start, timeout=15)
        # Cross the 64-entry virtio ring repeatedly, including releases.
        for _ in range(256):
            vm.press("o", pace=0.005)
            vm.press("backspace", pace=0.005)
        start = len(vm.text())
        vm.type("whoami\n")
        vm.wait("tester (jonas)", after=start, timeout=15)
        for _ in range(3):
            start = len(vm.text())
            vm.type("heap\n", pace=0.025)
            vm.wait("Kernel Heap:", after=start, timeout=15)
        start = len(vm.text())
        vm.type("sync\n")
        vm.wait("State saved to disk.", after=start, timeout=45)
        vm.type("shutdown\n")
        vm.wait("[POWER] Shutdown", timeout=45)
        vm.process.wait(timeout=15)
        print(f"{args.arch}: console editing, sustained commands, sync and shutdown passed", flush=True)
    except BaseException:
        if vm.process.poll() is None:
            try:
                vm.screenshot(output / "failure-first-boot.png")
            except Exception:
                pass
        raise
    finally:
        vm.close()

    # Exercise the exact rebuild finalizer against a saved guest disk. The
    # candidate starts with an empty data partition and must not replace it.
    candidate = output / "rebuilt-candidate.img"
    shutil.copyfile(Path(args.image).resolve(), candidate)
    subprocess.run([sys.executable, "-B", str(Path(__file__).resolve().parents[1] / "tools/finalize_image.py"),
                    str(candidate), str(disk)], check=True)
    vm = VM(args.arch, disk, output, 2)
    try:
        vm.wait("Entering main loop")
        vm.wait("[PERSIST] Restored 2 instance(s)")
        # Cold boot must restore credentials, not merely an account label.
        vm.type("testpass\n")
        vm.wait("User 'tester' logged in", timeout=30)
        vm.wait("[UI] Desktop presented", timeout=30)
        vm.screenshot(output / "restored-desktop.png")
        vm.console()
        start = len(vm.text())
        vm.type("whoami\n")
        vm.wait("tester (jonas)", after=start, timeout=15)
        print(f"{args.arch}: rebuild and cold boot retain accounts and accept saved password", flush=True)
    except BaseException:
        if vm.process.poll() is None:
            try:
                vm.screenshot(output / "failure-cold-boot.png")
            except Exception:
                pass
        raise
    finally:
        vm.close()

    # Deliberately damage only this private test disk's newest payload. A
    # previous valid save must still boot and authenticate the same account.
    with disk.open("r+b") as file:
        slots = []
        for slot in range(2):
            offset = (67584 + slot * 16384) * 512
            file.seek(offset)
            header = struct.unpack("<6IQII", file.read(40))
            if header[0] == 0x4A534F50 and header[1] == 2:
                slots.append((header[6], offset))
        if len(slots) != 2:
            raise RuntimeError("Expected two committed checkpoint slots before recovery test")
        _, offset = max(slots)
        file.seek(offset + 512)
        byte = file.read(1)
        file.seek(offset + 512)
        file.write(bytes([byte[0] ^ 1]))
    vm = VM(args.arch, disk, output, 3)
    try:
        vm.wait("Entering main loop")
        vm.wait("payload checksum failed")
        vm.wait("[PERSIST] Restored 2 instance(s)")
        vm.type("testpass\n")
        vm.wait("User 'tester' logged in", timeout=30)
        vm.wait("[UI] Desktop presented", timeout=30)
        vm.screenshot(output / "recovered-desktop.png")
        print(f"{args.arch}: damaged newest checkpoint falls back and preserves login", flush=True)
    finally:
        vm.close()
    print(f"PASS: {args.arch}; evidence: {output}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=["aarch64", "x86_64"], required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--fresh-data", action="store_true", help="reset only the newly created private test disk's data partition")
    run(parser.parse_args())
