#!/usr/bin/env python3
"""Desktop pictures, live, on a private disk copy: the pack is found, Tahoe
is the default, Settings > Wallpaper lists and applies pictures, the grid
scrolls, Light/Dark switches paired pictures, Preview sets a picture, and
the choice survives a cold boot. Needs an image built with the local pack
(tools/import_wallpapers.py --fetch). Created by Jonas Lee.
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
    with disk.open('r+b') as file:      # fresh user data (private copy only)
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))  # user data only; the picture pack follows
    vm = VM('aarch64', disk, out, 'wp1')
    try:
        vm.wait('Entering main loop')
        count = int(re.search(r'\[WALLPACK\] (\d+) desktop pictures', vm.text())[1])
        assert count > 0, 'no desktop pictures in the image'
        vm.wait("[UI] Desktop picture 'Tahoe Light'")
        time.sleep(2)
        shot = out / '00-setup.png'; vm.screenshot(shot)
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

        def wheel(x, y, down, notches):
            move(x, y); time.sleep(.1)
            for _ in range(notches):
                for pressed in (True, False):
                    # Same mapping as every app here: wheel-up moves the content on.
                    vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {
                        'down': pressed, 'button': 'wheel-up' if down else 'wheel-down'}}]})
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
            time.sleep(2)                               # open animation
            return [int(v) for v in m.groups()]

        cx, cy = (width - 440) // 2, (height - 340) // 2
        click(cx + 200, cy + 86); vm.type('Jonas Lee')
        for i, value in enumerate(['jonas', 'wallpass', 'wallpass'], start=1):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'jonas' logged in"); vm.wait('[UI] Desktop presented')
        snap('01-desktop-tahoe-light', 1.5)

        # Settings > Wallpaper grid (same geometry as wallpaper_grid()).
        wx, wy, ww, wh = spotlight('settings')
        click(wx + 12 + 40, wy + 40 + 16 + 2 * 20 + 6)
        snap('02-settings-wallpaper', 2.5)
        main_x, main_w = wx + 12 + 170, ww - 24 - 170
        cols = max(1, (main_w - 32 + 16) // (144 + 16))
        step_x = (main_w - 32 - 144) // (cols - 1) if cols > 1 else 0
        top = wy + 40 + 60 + 6

        def cell(i, scroll=0):
            return main_x + 16 + (i % cols) * step_x + 72, top + (i // cols) * (90 + 34) - scroll + 45

        # Choice 2 + n is pack entry n; pick the first "any" picture after the Tahoe set.
        names = ['ojjyOS Waves Light', 'ojjyOS Waves Dark']
        names += re.findall(r'^\s*\("([^"]+)"', (Path(__file__).resolve().parents[1] /
                                                  'tools/import_wallpapers.py').read_text(), re.M)
        goa = names.index('Goa Coast')
        mark = len(vm.text())
        click(*cell(goa))
        vm.wait("[UI] Desktop picture 'Goa Coast'", after=mark, timeout=60)
        snap('03-goa-coast', 2.5)

        wheel(main_x + main_w // 2, top + 150, True, 6)
        snap('04-grid-scrolled', 1.0)
        wheel(main_x + main_w // 2, top + 150, False, 12)

        # Tahoe Light, then the Dark toggle must swap it for Tahoe Dark.
        mark = len(vm.text())
        click(*cell(names.index('Tahoe Light')))
        vm.wait("[UI] Desktop picture 'Tahoe Light'", after=mark, timeout=60)
        mark = len(vm.text())
        click(width - 40, 13); time.sleep(.6)
        click(width - 340 + 16 + 69, 36 + 106 + 28)      # Control Center > Appearance
        vm.wait("[UI] Desktop picture 'Tahoe Dark'", after=mark, timeout=60)
        vm.press('esc')
        snap('05-dark-tahoe-dark', 2.5)

        # Preview: next picture, then "Set as Desktop Picture".
        px, py, pww, pwh = spotlight('preview')
        snap('06-preview', 1.5)
        cxp, cyp, cwp, chp = px + 12, py + 40, pww - 24, pwh - 52
        bar_y = cyp + chp - 40 + 6
        click(cxp + 48 + 15, bar_y + 13)                 # next
        snap('07-preview-next', 3)
        mark = len(vm.text())
        click(cxp + cwp - 12 - 84, bar_y + 13)           # Set as Desktop Picture
        picked = re.search(r"\[UI\] Desktop picture '([^']+)'", vm.text()[mark:] or '')
        deadline = time.monotonic() + 60
        while not picked and time.monotonic() < deadline:
            time.sleep(.3)
            picked = re.search(r"\[UI\] Desktop picture '([^']+)'", vm.text()[mark:])
        assert picked and picked[1] == names[names.index('Tahoe Dark') + 1], picked
        chosen = picked[1]
        snap('08-preview-set', 2.5)

        vm.console()
        mark = len(vm.text())
        vm.type('sync\n'); vm.wait('State saved to disk.', after=mark, timeout=45)
        vm.type('shutdown\n'); vm.wait('[POWER] Shutdown', timeout=45)
        vm.process.wait(timeout=15)
    finally:
        vm.close()

    vm = VM('aarch64', disk, out, 'wp2')
    try:
        vm.wait('Entering main loop')
        vm.wait(f"[UI] Desktop picture '{chosen}'", timeout=60)
        vm.type('wallpass\n')
        vm.wait('[UI] Desktop presented', timeout=30)
        snap_path = out / '09-cold-boot.png'; time.sleep(1.5); vm.screenshot(snap_path)
        print(f'PASS: desktop pictures ({count} in pack, cold boot kept {chosen!r}); evidence {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
