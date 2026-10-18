#!/usr/bin/env python3
"""Live browser test on a private disk copy against the local test server
(guest URL http://10.0.2.2:<port>/): start page, address bar, link click,
redirect, form submit, Back, HTTPS notice, screenshots. Optional real-site
visits with --internet (not a gate). Created by Jonas Lee.
"""
import argparse
from pathlib import Path
import re
import shutil
import time
from PIL import Image
from qemu_smoke import VM
import web_server


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:
        file.seek(67584 * 512); file.write(bytes((262144 - 34 - 67584 + 1) * 512))
    server = web_server.start()
    port = server.server_address[1]
    tls_server = web_server.start_tls(str(out))
    tls_port = tls_server.server_address[1]
    base = f'http://10.0.2.2:{port}'
    vm = VM('aarch64', disk, out, 'web')
    try:
        vm.wait('Entering main loop'); vm.wait('[NET] DHCP lease', timeout=30); time.sleep(2)
        shot = out / 'setup.png'; vm.screenshot(shot)
        width, height = Image.open(shot).size
        if width >= 2560: width, height = width // 2, height // 2   # HiDPI: logical coordinates

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
            raise AssertionError(f'no {pattern!r} in serial log')

        def snap(name, wait=1.2):
            time.sleep(wait); vm.screenshot(out / f'{name}.png')

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(1)

        mark = len(vm.text())
        chord('spc'); vm.type('web\n')
        until(r'\[WEB\] view ', 20, mark)
        snap('01-start-page', 2)        # also lets the open animation settle
        view = re.findall(r'\[WEB\] view (\d+),(\d+) (\d+)x(\d+) page-origin (\d+),(\d+)', vm.text()[mark:])[-1]
        ox, oy = int(view[4]), int(view[5])
        print('browser view (settled):', view, flush=True)

        def visit(url, pattern=r'\[WEB\] loaded status=(\d+) type=(.*?) bytes=(\d+) ms=(\d+)', timeout=60):
            mark = len(vm.text())
            chord('l'); vm.type(url + '\n')
            return until(pattern, timeout, mark), mark

        loaded, mark = visit(f'{base}/')
        assert loaded[1] == '200' and int(loaded[3]) == len(web_server.PAGE), loaded[0]
        print('index:', loaded[0], flush=True)
        links = re.findall(r"\[WEB\] link (\d+) '([^']*)' at (-?\d+),(-?\d+)", vm.text()[mark:])
        assert links and links[0][1] == '/second.html', links
        snap('02-test-page')

        # Click the first link at its laid-out position (latest view rect:
        # the window's open animation may still have been running earlier).
        view = re.findall(r'\[WEB\] view (\d+),(\d+) (\d+)x(\d+) page-origin (\d+),(\d+)', vm.text())[-1]
        ox, oy = int(view[4]), int(view[5])
        lx, ly = ox + int(links[0][2]) + 12, oy + int(links[0][3]) + 6
        mark = len(vm.text())
        move(lx, ly); time.sleep(.6); snap('03-hover-status', .3)
        click(lx, ly)
        got = until(r'\[WEB\] GET (\S+)', 10, mark)
        assert got[1].endswith('/second.html'), got[0]
        until(r'\[WEB\] loaded status=200', 30, mark)
        snap('04-second-page')

        # Back returns to the index (a replay, not a new entry).
        mark = len(vm.text())
        vm.key('meta_l', True); vm.press('bracket_left'); vm.key('meta_l', False)
        got = until(r'\[WEB\] GET (\S+)', 10, mark)
        assert got[1].endswith(f':{port}/'), got[0]
        until(r'\[WEB\] loaded status=200', 30, mark)

        # Form: Tab focuses the text field, Enter submits a GET query.
        mark = len(vm.text())
        vm.press('tab'); vm.type('ojjy os'); vm.press('ret')
        got = until(r'\[WEB\] GET (\S+)', 10, mark)
        assert got[1].endswith('/search?q=ojjy+os'), got[0]
        until(r'\[WEB\] loaded status=200', 30, mark)
        snap('05-form-result')

        loaded, _ = visit(f'{base}/redirect')
        assert loaded[1] == '200' and int(loaded[3]) == len(web_server.SECOND), loaded[0]
        loaded, _ = visit(f'{base}/chunked')
        assert loaded[1] == '200' and loaded[2].startswith('text/plain'), loaded[0]
        snap('06-plain-text')

        # HTTPS against the local TLS 1.3 server (self-signed: encrypted, unverified)
        loaded, _ = visit(f'https://10.0.2.2:{tls_port}/', timeout=90)
        assert loaded[1] == '200' and int(loaded[3]) == len(web_server.PAGE), loaded[0]
        print('local https:', loaded[0], flush=True)
        snap('07-https-local', 1.0)

        mark = len(vm.text())
        chord('l'); vm.type('ojjy:about\n'); snap('08-about')
        print('server saw:', web_server.Handler.requests, flush=True)

        if args.internet:
            for name, url in [('12-https-wikipedia', 'https://en.wikipedia.org/wiki/Operating_system'),
                              ('13-https-npr', 'https://text.npr.org/'),
                              ('14-https-hn', 'https://news.ycombinator.com/'),
                              ('09-cern', 'http://info.cern.ch/hypertext/WWW/TheProject.html'),
                              ('10-68k-news', 'http://68k.news/'),
                              ('11-wiby-search', 'operating system')]:
                try:
                    loaded, _ = visit(url, r'\[WEB\] (loaded status=\d+[^\n]*|failed: [^\n]*)', 60)
                    print(name, loaded[1], flush=True)
                    snap(name, 2)
                except AssertionError as error:
                    print(name, 'no result (not a gate):', error, flush=True)
        print(f'PASS: browser; evidence {out}', flush=True)
    finally:
        vm.close()
        server.shutdown()
        tls_server.shutdown()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--internet', action='store_true')
    run(parser.parse_args())
