#!/usr/bin/env python3
"""Private-disk pointer alignment, scanout latency and scene-repaint checks.
Created by Jonas Lee. Requires Pillow and the existing QEMU smoke dependencies.
"""
import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
from PIL import Image
from qemu_smoke import VM


ROOT = Path(__file__).resolve().parent.parent


def cursor_image():
    """The anti-aliased pointer from kernel/src/cursor_image.{h,c}."""
    header = (ROOT / 'kernel/src/cursor_image.h').read_text()
    get = lambda name: int(re.search(rf'#define {name} (\d+)', header)[1])
    w, h, hx, hy = get('CURSOR_W'), get('CURSOR_H'), get('CURSOR_HOT_X'), get('CURSOR_HOT_Y')
    body = (ROOT / 'kernel/src/cursor_image.c').read_text().split('{', 1)[1].split('};', 1)[0]
    data = [int(v) for v in re.findall(r'\b\d+\b', body)]
    return w, h, hx, hy, data


def template():
    """Fully opaque pointer pixels (exact colours), relative to the hotspot."""
    w, h, hx, hy, data = cursor_image()
    return {(x - hx, y - hy): tuple(data[(y * w + x) * 4:(y * w + x) * 4 + 3])
            for y in range(h) for x in range(w) if data[(y * w + x) * 4 + 3] == 255}


def probe():
    """First solid white body pixel, relative to the hotspot."""
    return next(k for k, v in sorted(template().items(), key=lambda kv: (kv[0][1], kv[0][0])) if v == (255, 255, 255))


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    # Clear ONLY the new private disk. The user's accounts are never modified.
    with disk.open('r+b') as file:
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))  # user data only; the picture pack follows
    nm = shutil.which('llvm-nm') or '/opt/homebrew/opt/llvm/bin/llvm-nm'
    symbols = subprocess.check_output([nm, str(Path(args.kernel).resolve())], text=True)
    addresses = {name: int(addr, 16) for addr, name in re.findall(
        r'^([0-9a-fA-F]+) [bBdD] (full_presents|cursor_updates)$', symbols, re.M)}
    vm = VM('aarch64', disk, out, 'pointer')
    try:
        vm.wait('Entering main loop'); time.sleep(1)
        first = out / 'setup-before.png'; vm.screenshot(first)
        width, height = Image.open(first).size
        found = re.findall(r'(?:VIRTIO-GPU\] Display|Framebuffer:) \d+x\d+ @ (?:0x)+([0-9a-fA-F]+)', vm.text())
        base = int([a for a in found if int(a, 16)][-1], 16)
        # ramfb is tightly packed in this platform. Discover dimensions/address
        # each boot; never use another VM's physical framebuffer address.
        def read(address, size):
            path = out / 'probe.bin'
            vm.qmp('pmemsave', {'val': address, 'size': size, 'filename': str(path)})
            return path.read_bytes()
        def stats():
            return tuple(struct.unpack('<Q', read(addresses[n], 8))[0]
                         for n in ['full_presents', 'cursor_updates'])
        def move(x, y):
            rx, ry = (x * 32767 + width - 2) // (width - 1), (y * 32767 + height - 2) // (height - 1)
            vm.qmp('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': rx}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': ry}}]})
            return rx * (width - 1) // 32767, ry * (height - 1) // 32767
        def click(x, y):
            move(x, y)
            for down in [True, False]:
                vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                time.sleep(.08)
        def settle():
            # Boot/login fade-ins legitimately present scene frames for a few
            # seconds; baseline only once the scene is idle.
            last, deadline = stats(), time.monotonic() + 15
            while time.monotonic() < deadline:
                time.sleep(.7); now = stats()
                if now[0] == last[0]: return now
                last = now
            raise AssertionError(f'scene never became idle: {last}')
        before = settle() if addresses else None
        positions = [(100, 100), (200, 150), (800, 500), (100, 100),
                     (0, 0), (width - 1, height - 1), (0, height - 1), (width - 1, 0), (100, 100)]
        latencies = []
        for i, (x, y) in enumerate(positions):
            started = time.monotonic(); x, y = move(x, y)
            deadline = started + 1
            # Wait for an opaque pointer pixel that is on screen (white body if visible).
            visible = {k: v for k, v in template().items() if 0 <= x + k[0] < width and 0 <= y + k[1] < height}
            px, py = probe() if probe() in visible else next(iter(visible))
            r, g, b = visible[(px, py)]
            want = (r << 16) | (g << 8) | b
            while struct.unpack('<I', read(base + ((y + py) * width + x + px) * 4, 4))[0] & 0xffffff != want:
                if time.monotonic() > deadline: raise AssertionError(f'cursor did not reach {(x, y)}')
            latencies.append((time.monotonic() - started) * 1000)
            path = out / f'position-{i}.png'; vm.screenshot(path)
            image = Image.open(path).convert('RGB')
            assert all(image.getpixel((x + dx, y + dy)) == color
                       for (dx, dy), color in template().items()
                       if 0 <= x + dx < width and 0 <= y + dy < height), (x, y)
        if before:
            after = stats()
            assert after[0] == before[0], f'cursor-only movement repainted the full scene: {before} -> {after}'
            assert after[1] > before[1], 'overlay never updated'
            print(f'cursor movement: full frames {before[0]} -> {after[0]}, overlays {before[1]} -> {after[1]}', flush=True)
        print(f'9 absolute positions/corners passed; QMP-to-scanout ms: {[round(v, 2) for v in latencies]}', flush=True)
        # Click fields and submit at their actual visible positions.
        cx, cy = (width - 440) // 2, (height - 340) // 2
        for i, value in enumerate(['jonas', 'tester', 'testpass', 'testpass']):
            click(cx + 200, cy + 86 + i * 40); vm.type(value)
        vm.screenshot(out / 'setup-filled.png')
        click(cx + 200, cy + 309)
        vm.wait("User 'tester' logged in"); vm.wait('[UI] Desktop presented')
        time.sleep(2)
        move(100, 100); time.sleep(.1)
        before = settle()
        for i in range(40): move(100 + (i % 10) * 30, 100 + (i % 5) * 25)
        time.sleep(.1); after = stats()
        assert before[0] == after[0], f'desktop pointer movement invalidated scene: {before} -> {after}'
        print(f'desktop 40 moves: full frames {before[0]} -> {after[0]}, overlays {before[1]} -> {after[1]}', flush=True)
        vm.screenshot(out / 'desktop.png')
        vm.key('meta_l', True); vm.press('spc'); vm.key('meta_l', False)
        vm.type('terminal\n'); time.sleep(1)
        vm.type('run hello\n'); vm.wait('status=exited code=0')
        vm.screenshot(out / 'terminal.png')
        print(f'PASS: pointer alignment, overlay, setup clicks, desktop and Terminal; evidence: {out}', flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--kernel', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
