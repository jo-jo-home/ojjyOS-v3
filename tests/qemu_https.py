#!/usr/bin/env python3
"""Live HTTPS identity test (NET-02) on a private disk copy.

Local part (always): two TLS 1.3 servers on the host, reached as 10.0.2.2 -
one with a chain from an untrusted test CA, one self-signed. The browser
must show "This connection is not private" with the right reason, Go Back
must return to the previous page, Visit anyway must load the page with the
crossed lock (per host, this session), and Terminal curl must refuse
unless -k. With --internet: real sites must load verified (lock), and a
known expired site and a wrong host must show the warning. badssl.com only
speaks TLS 1.2 (NET-03), so its cases are recorded, not gated.
Screenshots of every state. Created by Jonas Lee.
"""
import argparse
from pathlib import Path
import re
import shutil
import socket
import sys
import time
from PIL import Image
from qemu_smoke import VM
import test_tls_local
import web_server
import x509_fixtures as fx


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))  # user data only; the picture pack follows
    now = time.time()
    stamp = lambda t: time.strftime('%Y%m%d%H%M%SZ', time.gmtime(t))     # noqa: E731
    pki = fx.build(str(out / 'pki'), stamp(now - 86400), stamp(now + 3650 * 86400))
    chain = out / 'untrusted-chain.pem'
    chain.write_text(Path(pki.certs['leaf_a'].pem).read_text() + Path(pki.certs['int_a'].pem).read_text())
    plain = web_server.start()
    ca_server = test_tls_local.serve(str(chain), pki.certs['leaf_a'].key)        # SAN IP:10.0.2.2, CA not trusted
    self_server = web_server.start_tls(str(out))                                 # self-signed, SAN IP:10.0.2.2
    port, ca_port, self_port = (s.server_address[1] for s in (plain, ca_server, self_server))
    results = []
    vm = VM('aarch64', disk, out, 'https')
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
            raise AssertionError(f'no {pattern!r} in serial log')

        def snap(name, wait=1.2):
            time.sleep(wait); vm.screenshot(out / f'{name}.png')

        def view():
            v = re.findall(r'\[WEB\] view (\d+),(\d+) (\d+)x(\d+) page-origin (\d+),(\d+)', vm.text())[-1]
            return [int(n) for n in v]

        def visit(url, pattern, timeout=90):
            mark = len(vm.text())
            chord('l'); vm.type(url + '\n')
            return until(pattern, timeout, mark), mark

        def click_link(prefix, mark):
            """Click a link the page logged after mark; returns the log mark taken just before the click."""
            link = until(r"\[WEB\] link \d+ '(" + re.escape(prefix) + r"[^']*)' at (-?\d+),(-?\d+)", 20, mark)
            v = view()
            before = len(vm.text())
            click(v[4] + int(link[2]) + 12, v[5] + int(link[3]) + 6)
            return before

        def page_info(name):
            v = view()
            click(v[0] + 140 + 14, v[1] + 46 + 14)             # the lock: omni_rect + 4..30
            snap(name, .8)
            vm.press('esc'); time.sleep(.4)
            move(v[0] + v[2] // 2, v[1] + v[3] - 40)          # keep the pointer off the lock for later shots

        def record(label, ok, detail):
            results.append(f"{'PASS' if ok else 'FAIL'} {label}: {detail}")
            print(results[-1], flush=True)
            assert ok, f'{label}: {detail}'

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in", timeout=120); vm.wait('[UI] Desktop presented', timeout=120); time.sleep(1)
        mark = len(vm.text())
        chord('spc'); vm.type('web\n')
        until(r'\[WEB\] view ', 30, mark)
        snap('00-start', 2)

        # 1. a plain page, then an untrusted CA -> warning; Go Back returns
        loaded, _ = visit(f'http://10.0.2.2:{port}/', r'\[WEB\] loaded status=(\d+)')
        warned, mark = visit(f'https://10.0.2.2:{ca_port}/', r'\[WEB\] cert warning 10\.0\.2\.2: ([^\n]*)')
        record('untrusted CA warning', warned[1] == 'unknown issuer: ojjyOS Test Root A', warned[1])
        snap('01-warning-unknown-issuer')
        page_info('02-warning-page-info')
        mark = click_link('ojjy:back', mark)
        got = until(r'\[WEB\] GET (\S+)', 20, mark)
        until(r'\[WEB\] loaded status=200', 30, mark)
        record('Go Back', got[1].endswith(f':{port}/'), got[1])
        snap('03-after-go-back')

        # 2. self-signed -> warning; Visit anyway -> page with the crossed lock
        warned, mark = visit(f'https://10.0.2.2:{self_port}/', r'\[WEB\] cert warning 10\.0\.2\.2: ([^\n]*)')
        record('self-signed warning', warned[1] == 'self-signed certificate', warned[1])
        snap('04-warning-self-signed')
        mark = click_link('ojjy:proceed', mark)
        until(r'\[WEB\] certificate exception for 10\.0\.2\.2', 20, mark)
        loaded = until(r'\[WEB\] loaded status=(\d+) type=(.*?) bytes=(\d+)', 90, mark)
        sec = until(r'\[WEB\] security 10\.0\.2\.2 (\w+) ([^\n]*)', 10, mark)
        record('Visit anyway', loaded[1] == '200' and int(loaded[3]) == len(web_server.PAGE) and
               sec[1] == 'exception', f'{loaded[0]} / {sec[0]}')
        snap('05-visit-anyway-crossed-lock')
        page_info('06-page-info-exception')
        # the exception is per host for this session: the other port loads too
        loaded, mark = visit(f'https://10.0.2.2:{ca_port}/', r'\[WEB\] (loaded status=\d+|cert warning[^\n]*)')
        record('exception covers the host', loaded[1].startswith('loaded status=200'), loaded[1])

        # 3. Terminal curl never sees the browser's exception: refuses, -k connects
        chord('spc'); vm.type('terminal\n'); time.sleep(2)

        def command(text, receipt, timeout=60):
            mark = len(vm.text())
            vm.type(text + '\n')
            return until(receipt, timeout, mark)

        found = command(f'curl https://10.0.2.2:{ca_port}/', r'\[NET\] curl (failed: [^\n]*|done[^\n]*)')
        record('curl refuses', found[1] == 'failed: certificate problem: unknown issuer: ojjyOS Test Root A', found[1])
        found = command(f'curl -k https://10.0.2.2:{ca_port}/', r'\[NET\] curl tls verified=(\d) ([^\n]*)')
        record('curl -k connects unverified', found[1] == '0' and found[2] == 'unknown issuer: ojjyOS Test Root A',
               found[0])
        snap('07-terminal-curl', .8)

        if args.internet:
            for host, path in [('example.com', '/'), ('en.wikipedia.org', '/wiki/Operating_system'),
                               ('www.apple.com', '/'), ('letsencrypt.org', '/')]:
                found = command(f'curl -I https://{host}{path}', r'\[NET\] curl (tls verified=\d [^\n]*|failed: [^\n]*)', 90)
                record(f'curl {host}', found[1].startswith('tls verified=1'), found[1])
            snap('08-terminal-real-sites', .8)
            chord('spc'); vm.type('web\n'); time.sleep(1.5)
            for i, (host, url) in enumerate([('example.com', 'https://example.com/'),
                                             ('en.wikipedia.org', 'https://en.wikipedia.org/wiki/Operating_system'),
                                             ('www.apple.com', 'https://www.apple.com/'),
                                             ('letsencrypt.org', 'https://letsencrypt.org/')]):
                found, _ = visit(url, r'\[WEB\] (security \S+ \w+ [^\n]*|cert warning[^\n]*|failed: [^\n]*)', 120)
                record(f'browser {host}', found[1].startswith(f'security {host} verified'), found[1])
                snap(f'{10 + i:02d}-verified-{host}', 2.5)
                if i == 0: page_info('14-page-info-verified')
            found, _ = visit('https://expired-isrgrootx1.letsencrypt.org/', r'\[WEB\] (cert warning [^\n]*|failed: [^\n]*)', 120)
            record('real expired site', 'certificate expired on' in found[1], found[1])
            snap('15-warning-real-expired')
            ip = socket.gethostbyname('github.com')
            found, _ = visit(f'https://{ip}/', r'\[WEB\] (cert warning [^\n]*|failed: [^\n]*)', 120)
            record('real wrong host', f'name mismatch: {ip} vs github.com' in found[1], found[1])
            snap('16-warning-real-wrong-host')
            for host in ['expired.badssl.com', 'wrong.host.badssl.com', 'self-signed.badssl.com',
                         'untrusted-root.badssl.com']:
                try:
                    found, _ = visit(f'https://{host}/', r'\[WEB\] (cert warning [^\n]*|failed: [^\n]*|loaded[^\n]*)', 90)
                    results.append(f'INFO {host} (TLS 1.2 only, NET-03): {found[1]}')
                except AssertionError as error:
                    results.append(f'INFO {host}: no result ({error})')
                print(results[-1], flush=True)
            snap('17-badssl-tls12')
        print(f'PASS: https identity; evidence {out}', flush=True)
    finally:
        vm.close()
        for server in (plain, ca_server, self_server):
            server.shutdown()
        (out / 'results.txt').write_text('\n'.join(results) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--internet', action='store_true', help='also real sites (verified + expired + wrong host)')
    sys.exit(run(parser.parse_args()))
