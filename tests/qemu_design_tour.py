#!/usr/bin/env python3
"""Design review tour on a private disk copy: screenshots of the setup
screen, desktop, menus, context menu, dock hover, Spotlight, Control Center,
Finder, Settings and the lock screen. Works at 1x and 2x (HiDPI): clicks use
logical coordinates. Created by Jonas Lee.
"""
import argparse
from pathlib import Path
import shutil
import time
from PIL import Image
from qemu_smoke import VM


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:      # fresh data partition (private copy only)
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))  # user data only; the picture pack follows
    vm = VM('aarch64', disk, out, 'tour')
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

        def click(x, y, button='left'):
            move(x, y); time.sleep(.15)
            for down in (True, False):
                vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': button}}]})
                time.sleep(.1)
            time.sleep(.4)

        def chord(*codes):
            for c in codes[:-1]: vm.key(c, True)
            vm.press(codes[-1])
            for c in reversed(codes[:-1]): vm.key(c, False)
            time.sleep(.4)

        def snap(name, wait=.8):
            time.sleep(wait); vm.screenshot(out / f'{name}.png')

        cx, cy = (width - 440) // 2, (height - 340) // 2
        click(cx + 200, cy + 86); vm.type('Jonas Lee')
        snap('01-setup-typing', .3)
        for i, value in enumerate(['jonas', 'tourpass', 'tourpass'], start=1):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'jonas' logged in"); vm.wait('[UI] Desktop presented')
        if args.dark:
            # Control Center > Appearance tile (third tile: left column, second row)
            click(width - 40, 13); time.sleep(.6)
            click(width - 340 + 16 + 69, 36 + 106 + 28); time.sleep(.6)
            vm.press('esc'); click(700, 500)
        snap('02-desktop', 1.5)

        click(140, 13); snap('03-file-menu', .5); vm.press('esc'); click(700, 500)
        click(700, 400, 'right'); snap('04-context-menu', .5); vm.press('esc'); click(700, 500)
        move(width // 2, height - 40); snap('05-dock-hover', .8)
        move(width // 2, height // 2)
        chord('meta_l', 'spc'); vm.type('ca'); snap('06-spotlight', .6); vm.press('esc')
        click(width - 40, 13); snap('07-control-center', .6); vm.press('esc'); click(700, 500)
        chord('meta_l', 'spc'); vm.type('settings\n'); snap('08-settings', 1.2)
        chord('meta_l', 'spc'); vm.type('finder\n'); snap('09-finder', 1.2)
        for i, app in enumerate(['calculator', 'calendar', 'activity monitor', 'about', 'web', 'terminal', 'textedit'], start=11):
            chord('meta_l', 'spc'); vm.type(app + '\n'); snap(f'{i}-{app.replace(" ", "-")}', 1.5)
        chord('ctrl', 'meta_l', 'q'); snap('10-lock-screen', 1.2)
        print(f'DONE: {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--dark', action='store_true', help='switch to Dark appearance after login')
    run(parser.parse_args())
