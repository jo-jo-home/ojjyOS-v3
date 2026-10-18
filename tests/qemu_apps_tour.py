#!/usr/bin/env python3
"""Open every built-in app one at a time on a private disk copy and capture
it (for review of the whole desktop). Created by Jonas Lee.
"""
import argparse
from pathlib import Path
import re
import shutil
import time
from PIL import Image
from qemu_smoke import VM

APPS = ['finder', 'terminal', 'textedit', 'notes', 'preview', 'calendar', 'calculator',
        'activity monitor', 'settings', 'about', 'web']


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:
        file.seek(67584 * 512); file.write(bytes((262144 - 34 - 67584 + 1) * 512))
    vm = VM(args.arch, disk, out, 'apps')
    try:
        vm.wait('Entering main loop'); time.sleep(3)
        shot = out / 'setup.png'; vm.screenshot(shot)
        width, height = Image.open(shot).size

        def click(x, y):
            vm.qmp('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + width - 2) // (width - 1)}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + height - 2) // (height - 1)}}]})
            time.sleep(.1)
            for down in [True, False]:
                vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                time.sleep(.1)
            time.sleep(.3)

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(1)
        vm.screenshot(out / '00-desktop.png')
        for n, app in enumerate(APPS, 1):
            mark = len(vm.text())
            vm.key('meta_l', True); vm.press('spc'); vm.key('meta_l', False); time.sleep(.3)
            vm.type(app + '\n')
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and not re.search(r"\[UI\] window '", vm.text()[mark:]):
                time.sleep(.2)
            time.sleep(2.5)
            vm.screenshot(out / f'{n:02d}-{app.replace(" ", "-")}.png')
            vm.key('meta_l', True); vm.press('q'); vm.key('meta_l', False); time.sleep(.8)
        print(f'DONE: {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', default='aarch64')
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
