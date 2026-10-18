#!/usr/bin/env python3
"""Native app USB/view-input, Retina capture and saved-login smoke.

Created by Jonas Lee. Uses private disks, EFI stores and owner-only sockets.
Never opens or stops an installed/user VM. Build scripts/build-vmapp.sh first.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time

from PIL import Image, ImageChops
from qemu_smoke import protected_checks

ROOT = Path(__file__).resolve().parents[1]
TEMP = Path(tempfile.gettempdir()) / 'opencode'


def pointer_hotspot(before, after, x, y):
    if before.size != after.size:
        raise RuntimeError('Native host window was still resizing during pointer capture')
    sx, sy = after.width / 1440, after.height / 900
    region = (round((x-10)*sx), round((y-10)*sy), round((x+40)*sx), round((y+50)*sy))
    diff = ImageChops.difference(before, after).crop(region)
    # Ignore small CG color/dither changes and the soft shadow. The high-
    # contrast outline starts at the hotspot, not the bitmap's 2px margin.
    red, green, blue = diff.split()
    changed = ImageChops.lighter(ImageChops.lighter(red, green), blue).point(
        lambda value: 255 if value > 64 else 0).getbbox()
    return ((region[0]+changed[0])/sx, (region[1]+changed[1])/sy) if changed else None


class NativeVM:
    def __init__(self, app, disk, output, number):
        self.log = output / f'serial-{number}.log'
        TEMP.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.socket_dir = Path(tempfile.mkdtemp(prefix='ovz-', dir=TEMP))
        self.path = self.socket_dir / 'c'
        self.stderr = (output / f'app-{number}.log').open('wb')
        self.process = subprocess.Popen([str(app), '--image', str(disk), '--vars', str(output / 'vars.fd'),
                                         '--serial', str(self.log), '--control', str(self.path)],
                                        stdout=self.stderr, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 20
        while not self.path.exists():
            if self.process.poll() is not None or time.monotonic() >= deadline:
                self.close()
                raise RuntimeError(f'Native control socket unavailable; see {output}')
            time.sleep(.05)

    def text(self):
        return self.log.read_text(errors='replace') if self.log.exists() else ''

    def wait(self, text, timeout=40, after=0):
        deadline = time.monotonic() + timeout
        while text not in self.text()[after:]:
            if self.process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError(f'Guest did not produce {text!r}; see {self.log}')
            time.sleep(.05)

    def command(self, line):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(30)
            connection.connect(str(self.path))
            connection.sendall(line.encode() + b'\n')
            data = b''
            while not data.endswith(b'\n'):
                chunk = connection.recv(4096)
                if not chunk:
                    raise RuntimeError('Control channel closed without a response')
                data += chunk
            result = json.loads(data)
            if not result.get('ok'):
                raise RuntimeError(result)
            return result

    def key(self, usage, down):
        if isinstance(usage, str):
            usage = {'meta_l': 227, 'spc': 44}[usage]
        self.command(f'key {usage} {int(down)}')

    def press(self, usage):
        self.key(usage, True)
        self.key(usage, False)

    def type(self, text, pace=None):
        lines = text.split('\n')
        for index, line in enumerate(lines):
            if line:
                if pace is None:
                    self.command('text ' + line)
                else:
                    for char in line:
                        self.command('text ' + char)
                        time.sleep(pace)
            if index < len(lines) - 1:
                self.press(40)
        time.sleep(.1)

    def console(self):
        self.key(225, True)
        self.press(41)
        self.key(225, False)
        time.sleep(.2)

    def screenshot(self, target):
        self.command('screenshot ' + str(target))
        image = Image.open(target).convert('RGB')
        # A successful API call can still save a black/blank Metal surface.
        colors = image.resize((100, 60)).getcolors(6001)
        if not colors or len(colors) < 20 or image.width < 1440:
            raise RuntimeError(f'Blank or non-Retina native capture: {target}, {image.size}')

    def shutdown(self):
        self.type('sync\n')
        self.wait('State saved to disk.')
        self.type('shutdown\n')
        self.wait('[POWER] Shutdown')
        deadline = time.monotonic() + 10
        while self.command('status')['state'] != 0:
            if time.monotonic() >= deadline:
                raise RuntimeError('Guest did not stop cleanly')
            time.sleep(.05)
        self.command('quit')
        self.process.wait(timeout=10)

    def fullscreen(self, active):
        self.command('fullscreen')
        deadline = time.monotonic() + 10
        while self.command('status')['fullscreen'] != active:
            if time.monotonic() >= deadline:
                raise RuntimeError('Native full-screen transition failed')
            time.sleep(.05)
        time.sleep(1)

    def close(self):
        if self.process.poll() is None:
            # Only our child process using the private test disk.
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.stderr.close()
        if self.path.exists():
            self.path.unlink()
        self.socket_dir.rmdir()


def digest(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    disk = output / 'test-disk.img'
    if disk.exists():
        raise RuntimeError('Choose a new evidence directory')
    source, app = Path(args.image).resolve(), Path(args.app).resolve()
    before = digest(source)
    shutil.copyfile(source, disk)
    if args.fresh_data:
        with disk.open('r+b') as file:
            file.seek(67584 * 512)
            remaining = (262144 - 34 - 67584 + 1) * 512
            while remaining:
                chunk = bytes(min(1024 * 1024, remaining))
                file.write(chunk)
                remaining -= len(chunk)
    vm = NativeVM(app, disk, output, 1)
    try:
        vm.wait('[INIT] Entering main loop')
        vm.wait('[TIMER] Health')
        # Check the actual live host, not only DiskLease in a unit fixture.
        for locked in (disk, disk.parent / '.image-update.lock'):
            with locked.open('r+b') as file:
                try:
                    fcntl.lockf(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    pass
                else:
                    fcntl.lockf(file, fcntl.LOCK_UN)
                    raise RuntimeError(f'Live VM did not retain exclusive lock: {locked}')
        # Force a hidden-window precondition. Capture must make this VM's
        # Metal surface visible before saving, not return a blank surface.
        vm.command('hide')
        if vm.command('status')['visible']:
            raise RuntimeError('Failed to establish hidden-window capture precondition')
        vm.screenshot(output / 'setup.png')
        if not vm.command('status')['visible']:
            raise RuntimeError('Capture did not restore its own window visibility')
        if 'first-boot setup active' not in vm.text():
            raise RuntimeError('Setup smoke requires --fresh-data (private disk only)')
        vm.type('oooo')
        for _ in range(4):
            vm.press(42)
        vm.command('move 100 100')
        time.sleep(.2)
        vm.screenshot(output / 'pointer-100-100.png')
        before_pointer = Image.open(output / 'setup.png').convert('RGB')
        after_pointer = Image.open(output / 'pointer-100-100.png').convert('RGB')
        hotspot = pointer_hotspot(before_pointer, after_pointer, 100, 100)
        if not hotspot or abs(hotspot[0]-100)>3 or abs(hotspot[1]-100)>3:
            raise RuntimeError(f'Native absolute pointer does not land at (100,100): {hotspot}')
        for field, y in (('Jonas Lee',367), ('tester',407), ('testpass',447), ('testpass',487)):
            vm.command(f'click 750 {y}')
            vm.type(field)
        vm.command('click 720 590')
        vm.wait("Created account 'tester' (Jonas Lee)")
        vm.wait("User 'tester' logged in")
        vm.wait('[UI] Desktop presented')
        time.sleep(1)
        vm.screenshot(output / 'desktop.png')
        vm.fullscreen(True)
        vm.screenshot(output / 'fullscreen.png')
        vm.fullscreen(False)
        vm.console()
        start = len(vm.text())
        vm.type('whoami\n')
        vm.wait('tester (Jonas Lee)', after=start)
        for _ in range(256):
            vm.press(18)
            vm.press(42)
        start = len(vm.text())
        vm.type('whoami\n')
        vm.wait('tester (Jonas Lee)', after=start)
        if args.protected:
            protected_checks(vm, 'aarch64', output)
            vm.console()
        vm.shutdown()
    except BaseException:
        try:
            vm.screenshot(output / 'failure-first.png')
        except Exception:
            pass
        raise
    finally:
        vm.close()
    vm = NativeVM(app, disk, output, 2)
    try:
        vm.wait('[INIT] Entering main loop')
        vm.wait('[PERSIST] Restored 2 instance(s)')
        vm.type('testpass\n')
        vm.wait("User 'tester' logged in")
        vm.wait('[UI] Desktop presented')
        vm.screenshot(output / 'restored-desktop.png')
        vm.console()
        start = len(vm.text())
        vm.type('whoami\n')
        vm.wait('tester (Jonas Lee)', after=start)
        vm.shutdown()
    finally:
        vm.close()
    # Damage only the newest checkpoint in our private disk. The previous
    # complete save must restore the same account on a third cold boot.
    with disk.open('r+b') as file:
        slots = []
        for slot in range(2):
            offset = (67584 + slot * 16384) * 512
            file.seek(offset)
            header = struct.unpack('<6IQII', file.read(40))
            if header[0] == 0x4A534F50 and header[1] == 2:
                slots.append((header[6], offset))
        if len(slots) != 2:
            raise RuntimeError('Expected two native checkpoints before fallback test')
        _, offset = max(slots)
        file.seek(offset + 512 + 17)
        original = file.read(1)
        file.seek(offset + 512 + 17)
        file.write(bytes([original[0] ^ 0x80]))
    vm = NativeVM(app, disk, output, 3)
    try:
        vm.wait('[INIT] Entering main loop')
        vm.wait('payload checksum failed')
        vm.wait('[PERSIST] Restored 2 instance(s)')
        vm.type('testpass\n')
        vm.wait("User 'tester' logged in")
        vm.wait('[UI] Desktop presented')
        vm.screenshot(output / 'fallback-desktop.png')
        vm.console()
        vm.shutdown()
    finally:
        vm.close()
    if digest(source) != before:
        raise RuntimeError('Source image changed during private-disk testing')
    (output / 'report.json').write_text(json.dumps({'native_setup': 'passed', 'usb_keyboard': 'passed',
        'absolute_pointer': 'passed', 'setup_clicks': 'passed', 'retina_capture': 'passed',
        'sync_shutdown': 'passed', 'cold_login': 'passed', 'source_unchanged': True,
        'checkpoint_fallback': 'passed', 'native_fullscreen': 'passed',
        'protected_apps': 'passed' if args.protected else 'not requested',
        'limitations': 'Fixed 2880x1800 scanout; no live guest display-mode changes. Manual focus-loss behavior not exhaustively tested.'}, indent=2) + '\n')
    print(f'PASS: native USB setup/editing, Retina capture, sync/shutdown and saved login; evidence: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--app', default=str(ROOT / 'build/ojjyOS.app/Contents/MacOS/ojjyOS'))
    parser.add_argument('--output', required=True)
    parser.add_argument('--fresh-data', action='store_true')
    parser.add_argument('--protected', action='store_true', help='also run the shared EL0 boundary/cleanup suite on VZ')
    run(parser.parse_args())
