#!/usr/bin/env python3
"""Live checks for the apps pass on a private disk copy: Terminal Tab
completion, Finder Trash, Calculator decimals, Calendar and Activity
Monitor layouts. Screenshots are the evidence. Created by Jonas Lee.
"""
import argparse
from pathlib import Path
import re
import shutil
import time
from PIL import Image
from qemu_smoke import VM


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))  # user data only; the picture pack follows
    vm = VM('aarch64', disk, out, 'apps')
    try:
        vm.wait('Entering main loop'); time.sleep(3)
        shot = out / 'setup.png'; vm.screenshot(shot)
        width, height = Image.open(shot).size
        if width >= 2560: width, height = width // 2, height // 2   # HiDPI: logical coordinates

        def click(x, y):
            vm.qmp('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + width - 2) // (width - 1)}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + height - 2) // (height - 1)}}]})
            time.sleep(.1)
            for down in [True, False]:
                vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                time.sleep(.1)
            time.sleep(.3)

        def chord(*codes):
            for c in codes[:-1]: vm.key(c, True)
            vm.press(codes[-1])
            for c in reversed(codes[:-1]): vm.key(c, False)
            time.sleep(.3)

        def open_app(name):
            mark = len(vm.text())
            chord('meta_l', 'spc'); vm.type(name + '\n')
            deadline = time.monotonic() + 10
            m = None
            while time.monotonic() < deadline and not m:
                m = re.search(r"\[UI\] window '[^']*' at (\d+),(\d+) (\d+)x(\d+)", vm.text()[mark:])
                time.sleep(.2)
            time.sleep(2)
            return tuple(int(v) for v in m.groups()) if m else None

        def snap(name, wait=1.0):
            time.sleep(wait); vm.screenshot(out / f'{name}.png')

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(1)

        # Terminal: Tab completes commands and file names
        open_app('terminal')
        vm.type('ec'); vm.press('tab'); vm.type('hello > note.txt\n')
        vm.type('cat no'); vm.press('tab'); vm.press('ret')
        vm.type('ls D'); vm.press('tab'); vm.press('tab')
        vm.press('ret')
        snap('01-terminal-tab')

        # Finder: select note.txt in Home, Delete -> Trash, then open Trash
        win = open_app('finder')
        for _ in range(3): vm.press('down')
        snap('02-finder-selected', .5)
        vm.press('delete'); snap('03-finder-after-delete')
        x, y, w, h = win
        click(x + 12 + 30, y + 40 + 32 + 5 * 20 + 6)            # sidebar "Trash"
        snap('04-finder-trash')
        click(x + 12 + (w - 24) - 180 + 60, y + 40 + (h - 52) - 24)   # Empty Trash
        snap('05-finder-trash-emptied')

        # Calculator decimals via keyboard
        open_app('calculator')
        vm.type('1'); chord('shift', 'slash') if False else vm.press('slash'); vm.type('3'); vm.press('ret')
        snap('06-calc-one-third', .5)
        vm.press('esc'); vm.type('0.1'); chord('shift', 'equal'); vm.type('0.2'); vm.press('ret')
        snap('07-calc-point-three', .5)

        open_app('calendar'); snap('08-calendar')
        open_app('activity monitor'); snap('09-activity-monitor')
        print(f'DONE: {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
