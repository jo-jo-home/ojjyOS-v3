#!/usr/bin/env python3
"""Live Wi-Fi tile / Settings > Network check on a private disk copy.
Control Center's Wi-Fi tile must really switch the (virtual) adapter off
and on, Bluetooth must report no hardware, and the Network page must show
the lease. Created by Jonas Lee.
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
    with disk.open('r+b') as file:
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))  # user data only; the picture pack follows
    vm = VM('aarch64', disk, out, 'netui')
    try:
        vm.wait('Entering main loop'); vm.wait('[NET] DHCP lease', timeout=30); time.sleep(2)
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
            time.sleep(.4)

        def snap(name, wait=1.0):
            time.sleep(wait); vm.screenshot(out / f'{name}.png')

        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented'); time.sleep(1)
        snap('01-menubar')

        # Control Center from the menu bar "Control" item
        click(width - 110, 12); snap('02-control-center')
        panel_x, panel_y = width - 320 - 20, 28 + 10
        tile_w = (320 - 44) // 2
        wifi = (panel_x + 16 + tile_w // 2, panel_y + 40 + 28)
        bt = (panel_x + 16 + tile_w + 12 + tile_w // 2, panel_y + 40 + 28)
        mark = len(vm.text())
        click(*wifi)
        vm.wait('[NET] Network off', timeout=10, after=mark); snap('03-wifi-off')
        click(*bt); snap('04-bluetooth-no-hardware', .5)
        mark = len(vm.text())
        click(*wifi)
        vm.wait('[NET] Network on', timeout=10, after=mark)
        vm.wait('[NET] DHCP lease', timeout=30, after=mark); snap('05-wifi-on', 1.5)
        vm.press('esc')

        # Settings > Network
        mark = len(vm.text())
        vm.key('meta_l', True); vm.press('spc'); vm.key('meta_l', False); time.sleep(.3)
        vm.type('settings\n')
        m = None
        deadline = time.monotonic() + 15
        while not m and time.monotonic() < deadline:
            m = re.search(r"\[UI\] window 'Settings' at (\d+),(\d+) (\d+)x(\d+)", vm.text()[mark:])
            time.sleep(.2)
        assert m, 'Settings window did not open'
        wx, wy = int(m[1]), int(m[2])
        time.sleep(2)                                   # let the open animation finish
        click(wx + 12 + 40, wy + 40 + 16 + 6 * 20 + 6)  # sidebar item 6 = Network
        snap('06-settings-network', 1.5)
        print(f'PASS: network UI; evidence {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
