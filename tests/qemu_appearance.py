#!/usr/bin/env python3
"""Settings > Appearance, live, on a private disk copy: the appearance
segmented control (Light/Dark/Auto), accent swatches, Liquid Glass
Clear/Tinted, menu bar Glass/Solid and window corners Standard/Round all
apply live, log `[UI] Appearance ...` lines, and survive a cold boot.
Auto flips to dark between 19:00 and 07:00 UTC (the guest clock is UTC).
Needs an image built with the local wallpaper pack (Tahoe pairing).
Created by Jonas Lee.
"""
import argparse
import re
from datetime import datetime, timezone
from pathlib import Path
import shutil
import time
from PIL import Image

from qemu_smoke import VM


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:      # fresh user data (private copy only)
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))

    vm = VM('aarch64', disk, out, 'ap1')
    try:
        vm.wait('Entering main loop')
        # Fresh data: no system.conf yet, so defaults apply (no summary line
        # is logged until something is saved).
        shot = out / '00-setup.png'; time.sleep(2); vm.screenshot(shot)
        pw, ph = Image.open(shot).size
        k = 2 if pw >= 2560 else 1
        width, height = pw // k, ph // k

        def move(x, y):
            vm.qmp('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + width - 2) // (width - 1)}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + height - 2) // (height - 1)}}]})

        def click(x, y):
            move(x, y); time.sleep(.15)
            for down in (True, False):
                vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                time.sleep(.1)
            time.sleep(.4)

        def snap(name, wait=.8):
            time.sleep(wait); vm.screenshot(out / f'{name}.png')

        def spotlight(app):
            vm.key('meta_l', True); vm.press('spc'); vm.key('meta_l', False); time.sleep(.3)
            mark = len(vm.text())
            vm.type(app + '\n')
            m = None
            deadline = time.monotonic() + 15
            while not m and time.monotonic() < deadline:
                m = re.search(r"\[UI\] window '%s' at (\d+),(\d+) (\d+)x(\d+)" % app.title(), vm.text()[mark:])
                time.sleep(.2)
            assert m, f'{app} window did not open'
            time.sleep(1.5)                               # open animation
            return [int(v) for v in m.groups()]

        # First-boot account
        cx, cy = (width - 440) // 2, (height - 340) // 2
        click(cx + 200, cy + 86); vm.type('Jonas Lee')
        for i, value in enumerate(['jonas', 'appypass', 'appypass'], start=1):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'jonas' logged in"); vm.wait('[UI] Desktop presented')
        snap('01-desktop-light', 1.5)

        wx, wy, ww, wh = spotlight('settings')
        main_x = wx + 12 + 160 + 10
        main_w = (ww - 24) - 160 - 10
        sx = main_x + main_w - 16 - 240             # segmented control x
        ry = lambda i: wy + 40 + 40 + i * 30         # row i top (content_y + 40 + 30i)
        ax = main_x + main_w - 16 - 228              # accent swatch row x0

        click(wx + 52, wy + 62)                       # sidebar: Appearance

        click(sx + 120, ry(0) + 13)                   # Dark
        vm.wait('[UI] Appearance mode=dark')
        vm.wait("[UI] Desktop picture 'Tahoe Dark'")
        snap('02-dark-blue')

        click(ax + 6 * 30 + 9, ry(1) + 12)           # accent: Green
        vm.wait('[UI] Appearance accent=Green')
        click(sx + 180, ry(2) + 13)                   # Liquid Glass: Tinted
        vm.wait('[UI] Appearance glass=tinted')
        click(sx + 180, ry(3) + 13)                   # Menu Bar: Solid
        vm.wait('[UI] Appearance menubar=solid')
        click(sx + 180, ry(4) + 13)                   # Window Corners: Round
        vm.wait('[UI] Appearance corners=round')
        click(sx + 150, ry(5) + 13)                   # Terminal Theme: Ocean
        vm.wait('[UI] Appearance termtheme=Ocean')
        snap('03-dark-green-tinted', 1.0)

        click(sx + 40, ry(0) + 13)                    # Light
        vm.wait('[UI] Appearance mode=light')
        vm.wait("[UI] Desktop picture 'Tahoe Light'")
        snap('04-light-green-tinted')

        click(sx + 200, ry(0) + 13)                   # Auto (from Light)
        vm.wait('[UI] Appearance mode=auto')
        utc_hour = datetime.now(timezone.utc).hour
        if utc_hour >= 19 or utc_hour < 7:
            vm.wait('[UI] Appearance auto -> dark')
            vm.wait("[UI] Desktop picture 'Tahoe Dark'")
        click(sx + 40, ry(0) + 13)                    # Light (persisted state)
        vm.wait('[UI] Appearance mode=light')
        vm.wait("[UI] Desktop picture 'Tahoe Light'")

        vm.console()
        mark = len(vm.text())
        vm.type('sync\n'); vm.wait('State saved to disk.', after=mark, timeout=45)
        vm.type('shutdown\n'); vm.wait('[POWER] Shutdown', timeout=45)
        vm.process.wait(timeout=15)
    finally:
        vm.close()

    vm = VM('aarch64', disk, out, 'ap2')
    try:
        vm.wait('Entering main loop')
        vm.wait('[UI] Appearance mode=light accent=Green glass=tinted menubar=solid corners=round termtheme=Ocean')
        vm.type('appypass\n')
        vm.wait('[UI] Desktop presented', timeout=30)
        snap('05-cold-boot-persisted', 1.5)
        print('PASS: appearance settings apply live and survive a cold boot; evidence', out, flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
