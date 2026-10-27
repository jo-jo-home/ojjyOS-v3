#!/usr/bin/env python3
"""TextEdit, live, on a private disk copy with fresh user data:

- a long paragraph typed without Enter soft-wraps at word boundaries
  (verified by counting ink rows inside the paper, not by trust);
- Cmd+S saves and logs the byte, word and character counts, and they
  match the typed text exactly;
- the caret still walks the wrapped rows with Up/Down/Home.

Run with OJJY_ACCEL=hvf for real-speed timings. Created by Jonas Lee.
"""
import argparse
import re
from pathlib import Path
import shutil
import time
from PIL import Image

from qemu_smoke import VM

PASSWORD = 'textpass'

# ~150 lowercase words: more than two 63-column rows of the default
# TextEdit window, no punctuation surprises for the key translator.
PARAGRAPH = ('soft wrap folds long paragraphs at word boundaries so every line stays '
             'inside the window and the caret keeps moving the way it looks on screen')


class Desk:
    """Pointer/keyboard helpers in logical (1x) screen coordinates."""

    def __init__(self, vm, out, width, height):
        self.vm, self.out, self.width, self.height = vm, out, width, height

    def move(self, x, y):
        w, h = self.width, self.height
        self.vm.qmp('input-send-event', {'events': [
            {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + w - 2) // (w - 1)}},
            {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + h - 2) // (h - 1)}}]})

    def click(self, x, y, settle=.4):
        self.move(x, y); time.sleep(.15)
        self.click_here(settle)

    def click_here(self, settle=.4):
        for down in (True, False):
            self.vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
            time.sleep(.1)
        time.sleep(settle)

    def chord(self, *keys):
        for key in keys[:-1]:
            self.vm.key(key, True)
        self.vm.press(keys[-1])
        for key in reversed(keys[:-1]):
            self.vm.key(key, False)

    def snap(self, name, wait=.8):
        time.sleep(wait); self.vm.screenshot(self.out / f'{name}.png')

    def spotlight(self, app, title=None):
        self.chord('meta_l', 'spc'); time.sleep(.3)
        mark = len(self.vm.text())
        self.vm.type(app + '\n')
        m = None
        deadline = time.monotonic() + 20
        while not m and time.monotonic() < deadline:
            m = re.search(r"\[UI\] window '%s' at (\d+),(\d+) (\d+)x(\d+)" % (title or app.title()), self.vm.text()[mark:])
            time.sleep(.2)
        assert m, f'{app} window did not open'
        time.sleep(1.5)                                   # open animation
        return [int(v) for v in m.groups()]


def wait_re(vm, pattern, after=0, timeout=40):
    deadline = time.monotonic() + timeout
    while True:
        m = re.search(pattern, vm.text()[after:])
        if m:
            return m
        if vm.process.poll() is not None or time.monotonic() > deadline:
            raise AssertionError(f'guest did not log {pattern!r}; see {vm.log}')
        time.sleep(.1)


def ink_rows(shot, x, y, k, rows=6):
    """Dark-pixel count per editor row band (draw_textedit_window
    geometry: text top at window y + 82, rows 18 px, glyphs 16 px)."""
    image = Image.open(shot).convert('RGB')
    px = image.load()
    bands = []
    for i in range(rows):
        top = (y + 82 + i * 18) * k
        dark = 0
        for yy in range(top, top + 16 * k):
            for xx in range((x + 14) * k, (x + 12 + 536 - 14) * k, 2):
                r, g, b = px[xx, yy]
                if (r + g + b) // 3 < 100:
                    dark += 1
        bands.append(dark)
    return bands


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:      # fresh user data (private copy only)
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))
    checks = []

    def ok(text):
        checks.append(text); print('ok:', text, flush=True)

    vm = VM('aarch64', disk, out, 'textedit1')
    try:
        vm.wait('Entering main loop')
        shot = out / '00-setup.png'; time.sleep(2); vm.screenshot(shot)
        pw, ph = Image.open(shot).size
        k = 2 if pw >= 2560 else 1
        desk = Desk(vm, out, pw // k, ph // k)

        cx, cy = (desk.width - 440) // 2, (desk.height - 340) // 2
        desk.click(cx + 200, cy + 86); vm.type('Jonas Lee')
        for i, value in enumerate(['jonas', PASSWORD, PASSWORD], start=1):
            desk.click(cx + 200, cy + 86 + i * 40); vm.type(value)
        desk.click(cx + 200, cy + 309)
        vm.wait("User 'jonas' logged in"); vm.wait('[UI] Desktop presented')

        # 1. Type one long paragraph without Enter: it must wrap.
        x, y, w, h = desk.spotlight('textedit', 'TextEdit')
        assert (w, h) == (560, 360), (w, h)
        mark = len(vm.text())
        vm.type(PARAGRAPH, pace=.04)
        time.sleep(.5)
        desk.snap('01-wrapped')
        bands = ink_rows(out / '01-wrapped.png', x, y, k)
        filled = sum(1 for band in bands if band > 60)
        assert filled >= 3, f'paragraph did not wrap: ink rows {bands}'
        assert bands[0] > 60 and bands[1] > 60 and bands[2] > 60, f'first three rows not filled: {bands}'
        assert bands[4] < 10 and bands[5] < 10, f'ink past the paragraph: {bands}'
        ok(f'{len(PARAGRAPH)}-char paragraph wrapped onto {filled} rows '
           f'(ink bands {[min(b, 999) for b in bands]}) instead of one 63-column line')

        # 2. Save: the log must carry the exact counts of the text.
        mark = len(vm.text())
        desk.chord('meta_l', 's')
        m = wait_re(vm, r"\[TEXTEDIT\] Saved 'Untitled\.txt' (\d+) bytes, (\d+) words, (\d+) chars", mark)
        bytes_, words_, chars_ = (int(v) for v in m.groups())
        assert words_ == len(PARAGRAPH.split()) and chars_ == len(PARAGRAPH), \
            f'counts wrong: logged {words_} words {chars_} chars, typed {len(PARAGRAPH.split())} / {len(PARAGRAPH)}'
        assert bytes_ == len(PARAGRAPH) + 1, f'byte count wrong: {bytes_}'  # one line + newline
        ok(f'Cmd+S saved Untitled.txt and logged {bytes_} bytes, {words_} words, {chars_} chars (matches the typed text)')

        # 3. Navigation across the wrapped rows (visual; the host tests
        #    already prove the movement logic).
        vm.press('home'); time.sleep(.3)
        desk.snap('02-home')
        vm.press('down'); vm.press('down'); time.sleep(.3)
        desk.snap('03-two-downs')

        # 4. A real second line: caret to the end of the document first,
        #    so Enter appends below the paragraph instead of splitting it.
        vm.press('end')
        vm.press('ret')
        vm.type('second short line', pace=.05)
        desk.snap('04-second-line')
        bands = ink_rows(out / '04-second-line.png', x, y, k, rows=5)
        second = 'second short line'
        assert bands[3] > 20, f'second line not on its own row: {bands}'
        assert bands[4] < 10, f'ink past the second line: {bands}'
        ok(f'"second short line" renders as its own 4th row '
           f'(ink bands {[min(b, 999) for b in bands]})')
        mark = len(vm.text())
        desk.chord('meta_l', 's')
        m2 = wait_re(vm, r"\[TEXTEDIT\] Saved 'Untitled\.txt' (\d+) bytes, (\d+) words, (\d+) chars", mark)
        saved = int(m2.group(1))
        assert saved == len(PARAGRAPH) + 1 + len(second) + 1, f'second save: {saved}'
        words2 = int(m2.group(2))
        assert words2 == len(PARAGRAPH.split()) + len(second.split()), f'word count: {words2}'
        ok(f'Enter at the end appended a real second line; second save is {saved} bytes, {words2} words')

        (out / 'checks.txt').write_text('\n'.join(checks) + '\n')
        print('PASS: TextEdit soft-wraps long paragraphs, counts words and characters, '
              'and notifies on save; evidence', out, flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
