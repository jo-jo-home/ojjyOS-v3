#!/usr/bin/env python3
"""Real-website tour for the Web app (needs internet; not a pass/fail gate
for the site contents). Boots a private disk copy, creates an account,
opens Web and visits each site, saving a screenshot plus the load result
line. Usage: qemu_web_tour.py --image IMG --output DIR [--sites a,b,...]
Created by Jonas Lee.
"""
import argparse
from pathlib import Path
import re
import shutil
import time
from PIL import Image
from qemu_smoke import VM

SITES = [
    ('wikipedia', 'https://en.wikipedia.org/wiki/Operating_system'),
    ('search', 'operating system'),
    ('hn', 'news.ycombinator.com'),
    ('npr', 'text.npr.org'),
    ('python', 'docs.python.org/3/'),
    ('mdn', 'developer.mozilla.org/en-US/docs/Web/HTML'),
    ('github', 'github.com'),
    ('bbc', 'bbc.com'),
    ('apple', 'apple.com'),
    ('weather', 'weather.gov'),
    ('cnn-lite', 'lite.cnn.com'),
    ('old-reddit', 'old.reddit.com'),
    ('youtube', 'youtube.com'),
    ('craigslist', 'craigslist.org'),
]


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))
    sites = SITES
    if args.sites:
        wanted = args.sites.split(',')
        sites = [s for s in SITES if s[0] in wanted] or [(w, w) for w in wanted]
    vm = VM('aarch64', disk, out, 'tour')
    results = []
    try:
        vm.wait('Entering main loop'); vm.wait('[NET] DHCP lease', timeout=60); time.sleep(2)
        shot = out / 'setup.png'; vm.screenshot(shot)
        width, height = Image.open(shot).size
        if width >= 2560: width, height = width // 2, height // 2

        def move(x, y):
            vm.qmp('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + width - 2) // (width - 1)}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + height - 2) // (height - 1)}}]})

        def click(x, y):
            move(x, y); time.sleep(.1)
            for down in [True, False]:
                vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                time.sleep(.1)
            time.sleep(.3)

        def chord(code):
            vm.key('meta_l', True); vm.press(code); vm.key('meta_l', False); time.sleep(.3)

        def until(pattern, timeout=60, start=None):
            mark = len(vm.text()) if start is None else start
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                m = re.search(pattern, vm.text()[mark:])
                if m: return m
                time.sleep(.2)
            return None

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(1)
        mark = len(vm.text())
        chord('spc'); vm.type('web\n')
        until(r'\[WEB\] view ', 20, mark)
        time.sleep(2)
        # Maximise the Web window (green button: Cmd+Ctrl+F is not wired; use zoom via double-click title).
        if args.zoom:
            view = re.findall(r'\[WEB\] view (\d+),(\d+) (\d+)x(\d+)', vm.text())[-1]
            click(int(view[0]) + 200, int(view[1]) - 14); time.sleep(.2)
            click(int(view[0]) + 200, int(view[1]) - 14); time.sleep(1)
        vm.screenshot(out / '00-start.png')
        for name, url in sites:
            mark = len(vm.text())
            chord('l'); vm.type(url + '\n')
            m = until(r'\[WEB\] (loaded status=\d+[^\n]*|failed: [^\n]*)', args.timeout, mark)
            until(r'\[WEB\] layout items', 15, mark)
            line = m[1] if m else 'timeout'
            time.sleep(args.settle)
            vm.screenshot(out / f'{name}.png')
            results.append((name, url, line))
            print(f'{name:14} {url:50} {line}', flush=True)
    finally:
        vm.close()
    (out / 'results.txt').write_text(''.join(f'{n}\t{u}\t{l}\n' for n, u, l in results))
    print(f'DONE: {out}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--sites', default='')
    parser.add_argument('--timeout', type=int, default=60)
    parser.add_argument('--zoom', action='store_true')
    parser.add_argument('--settle', type=float, default=4)
    run(parser.parse_args())
