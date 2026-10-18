#!/usr/bin/env python3
"""Private-disk file-copy regression: `cp a a` and Finder Cmd+C/Cmd+V in the
same folder used to truncate the source to 0 bytes. Produces Terminal and
Finder screenshots for review. Created by Jonas Lee. Never touches the
user's disk (copies the image and clears only the copy's data area).
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
    with disk.open('r+b') as file:
        file.seek(67584 * 512); file.write(bytes((262144 - 34 - 67584 + 1) * 512))
    vm = VM(args.arch, disk, out, 'files')
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

        def chord(code):
            vm.key('meta_l', True); vm.press(code); vm.key('meta_l', False); time.sleep(.3)

        def spotlight(app):
            chord('spc'); vm.type(app + '\n'); time.sleep(1.5)

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(1)

        spotlight('terminal')
        for command in ['echo hello > a.txt', 'cp a.txt a.txt', 'cp a.txt ./a.txt',
                        'cp a.txt Documents', 'cat a.txt', 'cat Documents/a.txt']:
            vm.type(command + '\n'); time.sleep(.4)
        time.sleep(1); vm.screenshot(out / '01-terminal-cp.png')

        # Finder opens on the home folder; select a.txt (third entry) and
        # copy/paste it into the same folder twice.
        spotlight('finder')
        for _ in range(3): vm.press('down')
        chord('c'); chord('v'); chord('v')
        time.sleep(1); vm.screenshot(out / '02-finder-paste.png')

        spotlight('terminal')
        for command in ['clear', 'ls', 'cat a.txt', 'cat a copy.txt', 'cat a copy 2.txt']:
            vm.type(command + '\n'); time.sleep(.4)
        time.sleep(1); vm.screenshot(out / '03-terminal-after-paste.png')
        print(f'DONE: review screenshots in {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', default='aarch64')
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
