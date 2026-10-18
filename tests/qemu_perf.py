#!/usr/bin/env python3
"""Live renderer performance receipts on a private disk copy: window-open
animation durations, frame times while dragging a window, and screenshots.
Reads the kernel's "[PERF]" serial lines. Created by Jonas Lee.
"""
import argparse
import json
import os
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
    vm = VM('aarch64', disk, out, 'perf')
    try:
        vm.wait('Entering main loop'); time.sleep(3)
        shot = out / 'setup.png'; vm.screenshot(shot)
        width, height = Image.open(shot).size
        if width >= 2560: width, height = width // 2, height // 2   # HiDPI: logical coordinates

        def move(x, y):
            vm.qmp('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + width - 2) // (width - 1)}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + height - 2) // (height - 1)}}]})

        def button(down):
            vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})

        def click(x, y):
            move(x, y); time.sleep(.1)
            button(True); time.sleep(.1); button(False); time.sleep(.3)

        def chord(*codes):
            for c in codes[:-1]: vm.key(c, True)
            vm.press(codes[-1])
            for c in reversed(codes[:-1]): vm.key(c, False)
            time.sleep(.3)

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['Perf Tester', 'perf', 'perfpass', 'perfpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'perf' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(2)

        opens = []
        last = None
        for name in ['finder', 'terminal', 'settings', 'web', 'calculator']:
            mark = len(vm.text())
            chord('meta_l', 'spc'); vm.type(name + '\n')
            deadline = time.monotonic() + 60
            m = None
            while time.monotonic() < deadline and not m:
                m = re.search(r"\[PERF\] open '([^']*)' frames=(\d+) ms=(\d+)", vm.text()[mark:])
                time.sleep(.2)
            if m:
                opens.append({'app': m.group(1), 'frames': int(m.group(2)), 'ms': int(m.group(3))})
            w = re.findall(r"\[UI\] window '[^']*' at (\d+),(\d+) (\d+)x(\d+)", vm.text()[mark:])
            if w: last = tuple(int(v) for v in w[-1])
            time.sleep(1)
        vm.screenshot(out / 'windows.png')

        # Drag the top window by its title bar for ~4 s, then read frame stats.
        drag = {}
        if last:
            x, y, w, h = last
            mark = len(vm.text())
            gx, gy = x + w // 2, y + 14
            move(gx, gy); time.sleep(.2); button(True)
            start = time.monotonic(); steps = 0
            while time.monotonic() - start < 4:
                dx = int(120 * ((steps % 40) / 20 - 1))
                move(gx + abs(dx) - 60, gy + 30 + (steps % 7)); steps += 1; time.sleep(.02)
            button(False)
            time.sleep(6)   # let the 5 s [PERF] window close
            stats = re.findall(r"\[PERF\] frames=(\d+) avg_ms=(\d+) max_ms=(\d+)", vm.text()[mark:])
            if stats:
                f, a, mx = (int(v) for v in stats[0])
                drag = {'frames_5s': f, 'avg_ms': a, 'max_ms': mx, 'moves_sent': steps}
        vm.screenshot(out / 'after-drag.png')
        result = {'screen': [width, height], 'accel': os.environ.get('OJJY_ACCEL', 'tcg'),
                  'opens': opens, 'drag': drag}
        (out / 'perf.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2))
        print(f'DONE: {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
