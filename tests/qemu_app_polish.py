#!/usr/bin/env python3
"""APP-CAL-01 private-disk interactions and light/dark evidence. Created by Jonas Lee."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
from PIL import Image, ImageChops
from qemu_smoke import VM
from qemu_notes import Desk


def run(args):
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'
    shutil.copyfile(args.image, disk)
    with disk.open('r+b') as f:
        f.seek(67584 * 512)
        f.write(bytes((102400 - 67584) * 512))
    vm = VM('aarch64', disk, out, 'polish')
    checks = []
    if not args.basic:
        kernel = Path(args.image).resolve().with_name('kernel.bin')
        nm = shutil.which('llvm-nm') or '/opt/homebrew/opt/llvm/bin/llvm-nm'
        symbols = subprocess.check_output([nm, str(kernel)], text=True)
        refresh = re.search(r'^([0-9a-fA-F]+) [bBdD] monitor_refresh_ms$', symbols, re.M)
        assert refresh, 'matching kernel lacks Monitor refresh probe'
        refresh_address = int(refresh[1], 16)
    try:
        vm.wait('Entering main loop')
        time.sleep(2)
        vm.screenshot(out / 'setup.png')
        width, height = Image.open(out / 'setup.png').size
        scale = 2 if width >= 2560 else 1
        width, height = width // scale, height // scale
        desk = Desk(vm, out, width, height)
        x, y = (width - 440) // 2, (height - 340) // 2
        for i, text in enumerate(('Jonas Lee', 'polish', 'testpass', 'testpass')):
            desk.click(x + 200, y + 86 + i * 40)
            vm.type(text)
        desk.click(x + 200, y + 309)
        vm.wait("User 'polish' logged in")
        vm.wait('[UI] Desktop presented')

        def open_app(name):
            mark = len(vm.text())
            desk.chord('meta_l', 'spc'); time.sleep(.2)
            vm.type(name + '\n')
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                m = re.search(r"\[UI\] window '[^']+' at (\d+),(\d+) (\d+)x(\d+)", vm.text()[mark:])
                if m:
                    time.sleep(1)
                    return tuple(map(int, m.groups()))
                time.sleep(.1)
            raise AssertionError(f'{name} did not open')

        def snap(name):
            desk.snap(name, .35)
            return Image.open(out / (name + '.png')).convert('RGB')

        def crop(image, rect):
            return image.crop(tuple(int(v * scale) for v in rect))

        def shrink_check(name, geometry, minimum, mode):
            x, y, w, h = geometry
            desk.move(x + w - 6, y + h - 6); time.sleep(.1)
            vm.qmp('input-send-event', {'events':[{'type':'btn','data':{'down':True,'button':'left'}}]})
            time.sleep(.1)
            desk.move(x + 180, y + 160); time.sleep(.2)
            vm.qmp('input-send-event', {'events':[{'type':'btn','data':{'down':False,'button':'left'}}]})
            resized = open_app(name)
            assert resized[2:] == minimum, f'{name} minimum size mismatch: {resized}'
            snap(mode + '-' + name.replace(' ', '-') + '-minimum')
            checks.append(mode + ': ' + name + ' minimum-size guard')

        for mode in ('light', 'dark'):
            if mode == 'dark':
                sx, sy, sw, sh = open_app('settings')
                main_x, main_w = sx + 12 + 170, sw - 24 - 170
                segment_x = main_x + main_w - 16 - 240
                mark = len(vm.text())
                desk.click(segment_x + 120, sy + 40 + 40 + 13)
                vm.wait('[UI] Appearance mode=dark', after=mark)
            cx, cy, cw, ch = open_app('calculator')
            vm.press('esc'); vm.type('0.1'); desk.chord('shift', 'equal'); vm.type('0.2'); vm.press('ret')
            vm.wait("display='0.3' history='0.1 + 0.2 ='")
            snap(mode + '-calculator-history')
            checks.append(mode + ': keyboard decimals and equation history')
            vm.press('esc'); vm.type('1/0'); vm.press('ret')
            vm.wait("display='Can't divide by 0' history='1 / 0 ='")
            snap(mode + '-calculator-error')
            mark = len(vm.text()); vm.type('7')
            vm.wait("display='7' history=''", after=mark)
            vm.press('esc')
            # Independent on-screen targets derived from the rendered window.
            key_w = ((cw - 24) - 42) // 4
            key_h = ((ch - 52) - 92 - 34) // 5
            bx = cx + 24 + (key_w + 6)
            by = cy + 40 + 92 + 3 * (key_h + 6)  # 2
            desk.move(bx + key_w // 2, by + key_h // 2)
            vm.qmp('input-send-event', {'events': [{'type':'btn', 'data':{'down':True,'button':'left'}}]})
            time.sleep(.04)
            vm.screenshot(out / (mode + '-calculator-pressed.png'))
            vm.qmp('input-send-event', {'events': [{'type':'btn', 'data':{'down':False,'button':'left'}}]})
            released = snap(mode + '-calculator-released')
            pressed = Image.open(out / (mode + '-calculator-pressed.png')).convert('RGB')
            rect = (bx + 8, by + 5, bx + key_w - 8, by + 10)
            diff = ImageChops.difference(crop(pressed, rect), crop(released, rect))
            assert diff.getbbox(), 'key feedback did not visibly expire'
            vm.wait("key=13 display='2'")
            checks.append(mode + ': mouse hit target, pressed feedback and expiry, error recovery')
            if not args.basic:
                shrink_check('calculator', (cx,cy,cw,ch), (320,360), mode)

            cx, cy, cw, ch = open_app('calendar')
            month = snap(mode + '-calendar-today')
            header_x, header_w = cx + 12 + 170, cw - 24 - 380
            desk.click(header_x + header_w - 87, cy + 60)
            previous = snap(mode + '-calendar-previous')
            rect = (header_x + 5, cy + 40, header_x + 190, cy + 66)
            assert ImageChops.difference(crop(month, rect), crop(previous, rect)).getbbox(), 'month navigation did not change title'
            desk.click(header_x + header_w - 28, cy + 60)  # Today
            returned = snap(mode + '-calendar-returned')
            assert not ImageChops.difference(crop(month, rect), crop(returned, rect)).getbbox(), 'Today did not restore the month'
            desk.chord('meta_l', 'n')
            event = snap(mode + '-calendar-new-event')
            # Event creation must change the actual grid/agenda, not only chrome.
            rect = (header_x, cy + 106, cx + cw - 24, cy + ch - 36)
            assert ImageChops.difference(crop(returned, rect), crop(event, rect)).getbbox(), 'new event did not render'
            checks.append(mode + ': Calendar previous month, Today and new event')
            if not args.basic:
                shrink_check('calendar', (cx,cy,cw,ch), (760,440), mode)

            about = open_app('about')
            snap(mode + '-about')
            checks.append(mode + ': About capture for version and Jonas credit review')
            if not args.basic:
                shrink_check('about', about, (500,350), mode)

            if not args.basic:
                mx, my, mw, mh = open_app('activity monitor')
                mark = len(vm.text())
                desk.click(mx + 26 + 56, my + 62)
                vm.wait('[MONITOR] tab=0', after=mark)
                snap(mode + '-monitor-processes')
                for tab, name in ((1, 'memory'), (2, 'frames')):
                    mark = len(vm.text())
                    desk.click(mx + 12 + 14 + tab * 112 + 56, my + 40 + 22)
                    vm.wait(f'[MONITOR] tab={tab}', after=mark)
                    time.sleep(1.2)
                    snap(mode + '-monitor-' + name)
                assert re.search(r'\[MONITOR\] tab=2 samples=[1-9]\d*', vm.text()), 'no measured frame samples'
                checks.append(mode + ': all Monitor tabs and measured frame samples')
                shrink_check('activity monitor', (mx,my,mw,mh), (640,440), mode)
                # After minimize animations settle, no hidden one-second work.
                desk.click(mx + 34, my + 14)
                time.sleep(1.5)
                probe = out / 'monitor-refresh.bin'
                def refresh_value():
                    vm.qmp('pmemsave', {'val':refresh_address, 'size':8, 'filename':str(probe)})
                    return int.from_bytes(probe.read_bytes(), 'little')
                before = refresh_value()
                time.sleep(2.2)
                assert refresh_value() == before, 'minimized Monitor kept refreshing'
                open_app('activity monitor')
                time.sleep(1.2)
                assert refresh_value() > before, 'restored Monitor did not resume updates'
                checks.append(mode + ': minimized Monitor stops updates and resumes on restore')
        (out / 'report.json').write_text(json.dumps({'checks':checks, 'count':len(checks)}, indent=2)+'\n')
        print(f'PASS: {len(checks)} app flows, light/dark; {out}')
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--basic', action='store_true', help='step 2 only, before Monitor wiring')
    run(parser.parse_args())
