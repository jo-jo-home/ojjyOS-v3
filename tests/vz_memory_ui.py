#!/usr/bin/env python3
"""HW-13 PMM/About/Monitor agreement on private native disks. Created by Jonas Lee."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

from vz_smoke import NativeVM, digest


def run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = Path(args.image).resolve()
    before = digest(source)
    disk = output / 'test-disk.img'
    shutil.copyfile(source, disk)
    with disk.open('r+b') as file:
        file.seek(67584 * 512)
        file.write(bytes((102400 - 67584) * 512))
    bundle = Path(args.app).resolve().parents[2]
    if bundle.suffix != '.app':
        raise RuntimeError('Expected executable inside native app bundle')
    private = output / 'ojjyOS.app'
    shutil.copytree(bundle, private)
    reader = output / 'read-capture'
    subprocess.run(['swiftc', '-O', '-warnings-as-errors',
                    str(Path(__file__).with_name('read_capture.swift')), '-o', str(reader)], check=True)
    vm = NativeVM(private / 'Contents/MacOS/ojjyOS', disk, output, 1, memory=args.memory)
    checks = []
    try:
        vm.wait('Entering main loop')
        vm.wait('[TIMER] Health')
        if args.memory >= 4096:
            vm.wait('[KVM] HIGH zero/write/read/free')
            assert re.search(r'\[KVM\] HIGH zero/write/read/free.*result=PASS', vm.text())
        match = re.search(r'\[PMM\] (\d+) MB RAM, (\d+) MB usable', vm.text())
        assert match, 'missing PMM capacity receipt'
        total, managed = map(int, match.groups())
        capacity = f'{round(total / 1024)} GB'
        assert abs(total - args.memory) <= 2, (total, args.memory)
        for value in ['Jonas Lee', 'tester', 'testpass', 'testpass']:
            vm.type(value)
            vm.press(43)
        vm.press(40)
        vm.wait("User 'tester' logged in")
        vm.wait('[UI] Desktop presented')

        def open_app(name):
            mark = len(vm.text())
            vm.key(227, True)
            vm.press(44)
            vm.key(227, False)
            time.sleep(.2)
            vm.type(name + '\n')
            vm.wait('[UI] window ', after=mark)
            geometry = re.search(r"\[UI\] window '[^']+' at (\d+),(\d+) (\d+)x(\d+)", vm.text()[mark:])
            assert geometry, name
            time.sleep(.7)
            return tuple(map(int, geometry.groups()))

        def capture(name):
            path = output / f'{name}.png'
            time.sleep(.7)
            vm.screenshot(path)
            lines = json.loads(subprocess.check_output([str(reader), str(path)], text=True))
            (output / f'{name}-text.json').write_text(json.dumps(lines, indent=2) + '\n')
            return ' '.join(lines)

        for mode in ('light', 'dark'):
            if mode == 'dark':
                settings_x, settings_y, settings_w, _ = open_app('settings')
                main_x = settings_x + 12 + 170
                main_width = settings_w - 24 - 170
                mark = len(vm.text())
                vm.command(f'click {main_x + main_width - 16 - 240 + 120} {settings_y + 93}')
                vm.wait('[UI] Appearance mode=dark', after=mark)
            open_app('about')
            text = capture(f'{mode}-about')
            assert f'{capacity} memory ({total} MB RAM)' in text, text
            assert 'Created by Jonas Lee' in text, text
            checks.append(f'{mode}: About {capacity}, exact {total} MB RAM and Jonas credit')
            monitor_x, monitor_y, _, _ = open_app('activity monitor')
            mark = len(vm.text())
            vm.command(f'click {monitor_x + 194} {monitor_y + 62}')
            vm.wait('[MONITOR] RAM=', after=mark)
            receipt = re.search(r'\[MONITOR\] RAM=(\d+) managed=(\d+) used=(\d+) free=(\d+) reserved=(\d+) MB', vm.text()[mark:])
            assert receipt, vm.text()[mark:]
            ram, capacity_mb, used, free, reserved = map(int, receipt.groups())
            assert (ram, capacity_mb) == (total, managed)
            assert abs(total - managed - reserved) <= 1
            assert abs(managed - used - free) <= 1
            text = capture(f'{mode}-monitor-memory')
            for expected in [f'Physical memory - {capacity}', f'{total} MB RAM', f'{managed} MB managed',
                             f'{used} MB used', f'{free} MB free', f'{reserved} MB reserved']:
                assert expected in text, (expected, text)
            assert 'still limited' not in text
            checks.append(f'{mode}: Monitor PMM agreement; {used}/{managed} MB managed, {free} MB free, {reserved} MB reserved')
        vm.console()
        vm.shutdown()
        assert digest(source) == before, 'source disk changed'
        report = {'result': 'PASS', 'memory_requested_mb': args.memory, 'pmm_ram_mb': total,
                  'pmm_managed_mb': managed, 'source_sha256': before, 'source_unchanged': True,
                  'checks': checks, 'screenshots_require_visual_review': True}
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print('PASS PMM/About/Monitor light+dark:', output, flush=True)
    finally:
        vm.close()
        assert digest(source) == before, 'source disk changed'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--app', required=True)
    parser.add_argument('--memory', type=int, choices=[2048, 4096, 8192], required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
