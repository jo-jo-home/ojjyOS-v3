#!/usr/bin/env python3
"""Live network check on a private disk copy: DHCP, ping to the gateway,
HTTP via Terminal curl (plain, redirect, chunked, 300 KB) against a local
host server reached at 10.0.2.2, and network off/on. Created by Jonas Lee.
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
    vm = VM('aarch64', disk, out, 'net')
    try:
        vm.wait('Entering main loop'); vm.wait('[NET] DHCP lease', timeout=30)
        print('DHCP:', re.search(r'\[NET\] DHCP lease[^\n]*', vm.text())[0], flush=True)
        time.sleep(2)
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

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(1)
        vm.key('meta_l', True); vm.press('spc'); vm.key('meta_l', False); time.sleep(.3)
        vm.type('terminal\n'); time.sleep(1.5)

        def command(text, receipt, timeout=60):
            mark = len(vm.text())
            vm.type(text + '\n')
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                tail = vm.text()[mark:]
                found = re.search(receipt, tail)
                if found:
                    return found
                time.sleep(.2)
            raise AssertionError(f'{text!r}: no {receipt!r} in serial log')

        vm.type('ifconfig\n'); time.sleep(1); vm.screenshot(out / '01-ifconfig.png')
        found = command('ping 10.0.2.2 3', r'\[NET\] ping (\d+) sent, (\d+) received')
        print('ping gateway:', found[0], flush=True)
        assert int(found[2]) >= 1, 'no ping replies from the gateway'
        base = f'http://10.0.2.2:{port}'
        found = command(f'curl {base}/', r'\[NET\] curl done status=(\d+) bytes=(\d+) redirects=(\d+)')
        assert found[1] == '200' and int(found[2]) == len(web_server.PAGE), found[0]
        vm.screenshot(out / '02-curl-index.png')
        found = command(f'curl {base}/redirect', r'\[NET\] curl done status=(\d+) bytes=(\d+) redirects=(\d+)')
        assert found[1] == '200' and found[3] == '1' and int(found[2]) == len(web_server.SECOND), found[0]
        found = command(f'curl {base}/chunked', r'\[NET\] curl done status=(\d+) bytes=(\d+) redirects=(\d+) truncated=(\d)')
        assert found[1] == '200' and int(found[2]) == 10 + 10 + 5001 + 10 and found[4] == '0', found[0]
        vm.screenshot(out / '03-curl-chunked.png')
        started = time.monotonic()
        found = command(f'curl -I {base}/big', r'\[NET\] curl done status=(\d+) bytes=(\d+) redirects=\d+ truncated=(\d)',
                        timeout=120)
        elapsed = time.monotonic() - started
        # curl's 256 KiB buffer holds headers + body: body is cut, flagged truncated
        assert found[1] == '200' and 250000 < int(found[2]) < 256 * 1024 and found[3] == '1', found[0]
        print(f'300 KB download truncated at 256 KiB buffer in {elapsed:.1f}s', flush=True)
        found = command(f'curl {base}/missing', r'\[NET\] curl done status=(\d+)')
        assert found[1] == '404', found[0]
        found = command('curl http://10.0.2.2:1/', r'\[NET\] curl failed: ([^\n]*)')
        print('closed port:', found[1], flush=True)
        vm.type('net off\n'); time.sleep(.5)
        found = command(f'curl {base}/', r'network is off')
        vm.type('net on\n')
        vm.wait('[NET] DHCP lease', timeout=30, after=len(vm.text()) - 2000)
        found = command(f'curl {base}/second.html', r'\[NET\] curl done status=(\d+)')
        assert found[1] == '200'
        vm.screenshot(out / '04-after-reconnect.png')
        if args.internet:
            try:
                found = command('nslookup example.com', r'\[NET\] nslookup ([^\n]*)', timeout=20)
                print('internet DNS:', found[1], flush=True)
                found = command('curl http://example.com/', r'\[NET\] curl (done status=\d+|failed: [^\n]*)', timeout=40)
                print('internet HTTP:', found[1], flush=True)
            except AssertionError as error:
                print('internet check skipped/failed (not a gate):', error, flush=True)
        print('server saw:', web_server.Handler.requests, flush=True)
        print(f'PASS: network + HTTP; evidence {out}', flush=True)
    finally:
        vm.close()
        server.shutdown()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--internet', action='store_true', help='also try example.com (not a gate)')
    run(parser.parse_args())
