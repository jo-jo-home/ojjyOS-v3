#!/usr/bin/env python3
"""Private-disk desktop UI tour: screenshots of setup, desktop, Finder (incl.
titlebar toolbar clicks), Terminal, Settings, About and an open menu.
Created by Jonas Lee. Uses the QEMU smoke VM helper; never touches the
user's disk (the image is copied and only the copy's data area is cleared).
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
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))  # user data only; the picture pack follows
    vm = VM(args.arch, disk, out, 'tour')
    try:
        vm.wait('Entering main loop'); time.sleep(3)
        shot = out / '00-setup.png'; vm.screenshot(shot)
        width, height = Image.open(shot).size

        def move(x, y):
            vm.qmp('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + width - 2) // (width - 1)}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + height - 2) // (height - 1)}}]})

        def click(x, y):
            move(x, y); time.sleep(.1)
            for down in [True, False]:
                vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                time.sleep(.1)
            time.sleep(.4)

        def snap(name):
            time.sleep(1); vm.screenshot(out / f'{name}.png')

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented')
        move(width // 2, height // 2); snap('01-desktop')

        def spotlight(app):
            vm.key('meta_l', True); vm.press('spc'); vm.key('meta_l', False)
            time.sleep(.3); vm.type(app + '\n'); time.sleep(1.5)

        spotlight('finder'); snap('02-finder')
        click(350, 108); snap('02b-finder-icons')  # titlebar view toggle
        spotlight('terminal'); vm.type('run hello\n'); vm.wait('status=exited code=0'); snap('03-terminal')
        spotlight('settings'); snap('04-settings')
        click(500, 258); snap('04b-settings-24h')  # 24-Hour Clock row
        spotlight('about'); snap('05-about')
        click(196, 12); snap('06-file-menu')  # after 'About ojjyOS' name; vm.press('esc')
        print(f'PASS: UI tour screenshots in {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', default='aarch64')
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
