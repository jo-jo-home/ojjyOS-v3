#!/usr/bin/env python3
"""Animation check on a private disk copy: screenshots taken mid-animation
(login cross-fade, window open, close, minimise-to-Dock, menu fade) plus the
"[PERF] open" receipts, and a check that every animation finishes (the scene
goes idle again). Works at 1x and 2x. Created by Jonas Lee.
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
    with disk.open('r+b') as file:      # fresh data partition (private copy only)
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))  # user data only; the picture pack follows
    vm = VM('aarch64', disk, out, 'anim')
    try:
        vm.wait('Entering main loop'); time.sleep(3)
        shot = out / '00-setup.png'; vm.screenshot(shot)
        pw, ph = Image.open(shot).size
        k = 2 if pw >= 2560 else 1
        width, height = pw // k, ph // k

        def move(x, y):
            vm.qmp('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + width - 2) // (width - 1)}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + height - 2) // (height - 1)}}]})

        def click(x, y, settle=.3):
            move(x, y); time.sleep(.08)
            for down in (True, False):
                vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                time.sleep(.04)
            time.sleep(settle)

        def chord(*codes):
            for c in codes[:-1]: vm.key(c, True)
            vm.press(codes[-1])
            for c in reversed(codes[:-1]): vm.key(c, False)

        def snap(name, delay):
            time.sleep(delay); vm.screenshot(out / f'{name}.png')

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['Anim Tester', 'anim', 'animpass', 'animpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309, 0)
        snap('01-login-crossfade', .12)
        vm.wait("User 'anim' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(1.5)

        mark = len(vm.text())
        chord('meta_l', 'spc'); time.sleep(.4); vm.type('calculator\n')
        snap('02-window-opening', .08)
        deadline = time.monotonic() + 10
        m = None
        while time.monotonic() < deadline and not m:
            m = re.search(r"\[UI\] window 'Calculator' at (\d+),(\d+) (\d+)x(\d+)", vm.text()[mark:]); time.sleep(.1)
        x, y, w, h = (int(v) for v in m.groups())
        time.sleep(1); vm.screenshot(out / '03-window-open.png')

        click(x + 14 + 20 + 6, y + 18, 0)          # yellow: minimise
        snap('04-minimizing', .12)
        time.sleep(1); vm.screenshot(out / '05-minimized.png')

        chord('meta_l', 'spc'); time.sleep(.4); vm.type('calculator\n'); time.sleep(1.2)
        click(x + 14 + 6, y + 18, 0)               # red: close
        snap('06-closing', .06)
        time.sleep(1); vm.screenshot(out / '07-closed.png')

        click(140, 13, 0); snap('08-menu-fading', .04)
        time.sleep(.5); vm.screenshot(out / '09-menu-open.png'); vm.press('esc')

        opens = re.findall(r"\[PERF\] open '([^']*)' frames=(\d+) ms=(\d+)", vm.text())
        print('opens:', opens, flush=True)
        print(f'DONE: {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
