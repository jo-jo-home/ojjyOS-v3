#!/usr/bin/env python3
"""Notes, live, on a private disk copy with fresh user data:

- first launch copies ~/Documents/Notes.txt into the new ~/Notes folder;
- New Note (button and Cmd+N) makes "New Note.txt" / "New Note 2.txt";
- typing autosaves once per pause, not once per key (the old Notes wrote
  the file on every key press);
- switching notes and closing the window save at once, and a load + save
  round trip keeps the byte count (no extra empty line per save);
- a note typed right before a console shutdown is saved by the
  save-before-sync hook and survives the cold boot byte for byte;
- a file too long for the editor opens read-only and is never rewritten;
- Spotlight finds a note and opens it in Notes; locking saves too.

Run with OJJY_ACCEL=hvf for real-speed timings. Created by Jonas Lee.
"""
import argparse
from pathlib import Path
import re
import shutil
import time
from PIL import Image

from qemu_smoke import VM

PASSWORD = 'notespass'


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

    def spotlight(self, app):
        self.chord('meta_l', 'spc'); time.sleep(.3)
        mark = len(self.vm.text())
        self.vm.type(app + '\n')
        m = None
        deadline = time.monotonic() + 20
        while not m and time.monotonic() < deadline:
            m = re.search(r"\[UI\] window '%s' at (\d+),(\d+) (\d+)x(\d+)" % app.title(), self.vm.text()[mark:])
            time.sleep(.2)
        assert m, f'{app} window did not open'
        time.sleep(1.5)                                   # open animation
        return [int(v) for v in m.groups()]


class NotesWindow:
    """Click targets, from the same numbers as draw_notes_window()."""

    def __init__(self, desk, geometry):
        self.desk = desk
        self.x, self.y, self.w, self.h = geometry
        self.cx, self.cy = self.x + 12, self.y + 40
        self.sw = min(180, (self.w - 24) // 3)

    def new_note(self):
        self.desk.click(self.cx + self.sw // 2, self.cy + 32 + 12)

    def row_xy(self, i):
        return self.cx + self.sw // 2, self.cy + 66 + i * 40 + 20

    def row(self, i, settle=.4):
        self.desk.click(*self.row_xy(i), settle)

    def close(self):
        self.desk.click(self.x + 20, self.y + 18)


def wait_re(vm, pattern, after=0, timeout=40):
    deadline = time.monotonic() + timeout
    while True:
        m = re.search(pattern, vm.text()[after:])
        if m:
            return m
        if vm.process.poll() is not None or time.monotonic() > deadline:
            raise AssertionError(f'guest did not log {pattern!r}; see {vm.log}')
        time.sleep(.1)


def notes_lines(vm, after=0):
    return [line for line in vm.text()[after:].splitlines() if line.startswith('[NOTES]')]


def run(args):
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=False)
    disk = out / 'test-disk.img'; shutil.copyfile(args.image, disk)
    with disk.open('r+b') as file:      # fresh user data (private copy only)
        file.seek(67584 * 512); file.write(bytes((102400 - 67584) * 512))
    checks = []

    def ok(text):
        checks.append(text); print('ok:', text, flush=True)

    vm = VM('aarch64', disk, out, 'notes1')
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

        # 1. First launch: ~/Notes is made and the old single file copied in.
        mark = len(vm.text())
        notes = NotesWindow(desk, desk.spotlight('notes'))
        vm.wait('[NOTES] Imported Documents/Notes.txt', after=mark)
        vm.wait('[NOTES] 1 notes in /Users/jonas/Notes', after=mark)
        seeded = int(wait_re(vm, r"\[NOTES\] Opened 'Notes\.txt' (\d+) bytes", mark)[1])
        assert seeded > 0, 'imported note is empty'
        assert (notes.w, notes.h) == (640, 400), (notes.w, notes.h)
        ok(f'first launch imported Documents/Notes.txt ({seeded} bytes) into ~/Notes')
        desk.snap('01-imported')

        # 2. New Note button, then typing: one autosave per pause.
        mark = len(vm.text())
        notes.new_note()
        vm.wait("[NOTES] New note 'New Note.txt'", after=mark)
        vm.wait("[NOTES] Opened 'New Note.txt' 0 bytes", after=mark)
        mark = len(vm.text())
        vm.type('Groceries\nmilk and eggs')                       # 23 keys
        vm.wait("[NOTES] Saved 'New Note.txt' 24 bytes", after=mark, timeout=30)
        time.sleep(2.5)
        saves = sum(1 for line in notes_lines(vm, mark) if line.startswith("[NOTES] Saved 'New Note.txt'"))
        assert 1 <= saves <= 2, f'{saves} saves for 23 keys (expected one per pause)'
        ok(f'23 typed keys autosaved {saves}x (24 bytes), not once per key')
        desk.snap('02-typed-autosaved')

        # 3. Cmd+N, type, then switch at once: the switch saves first, and the
        #    reopened note has exactly the bytes it was saved with.
        mark = len(vm.text())
        desk.chord('meta_l', 'n')
        vm.wait("[NOTES] New note 'New Note 2.txt'", after=mark)
        vm.wait("[NOTES] Opened 'New Note 2.txt' 0 bytes", after=mark)
        mark = len(vm.text())
        desk.move(*notes.row_xy(0)); time.sleep(.15)              # New Note.txt
        vm.type('Second not', pace=.05)
        typed_at = time.monotonic()                               # the last key goes out now
        vm.type('e', pace=.05)
        vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': True, 'button': 'left'}}]})
        click_gap = time.monotonic() - typed_at                   # last key -> button down
        time.sleep(.1)
        vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': False, 'button': 'left'}}]})
        vm.wait("[NOTES] Opened 'New Note.txt' 24 bytes", after=mark)
        lines = notes_lines(vm, mark)
        i = lines.index("[NOTES] Opened 'New Note.txt' 24 bytes")
        assert i > 0 and lines[i - 1] == "[NOTES] Saved 'New Note 2.txt' 12 bytes", lines
        ok(f'switching saved the open note first (click {click_gap:.2f}s after the last key); '
           'reopened note kept 24 bytes (no extra line per save)')
        desk.snap('03-switched')

        # 4. Edit and close the window at once: closing saves (no window is
        #    left for the autosave to run in).
        mark = len(vm.text())
        vm.type('Fresh ')
        notes.close()
        vm.wait("[NOTES] Saved 'New Note.txt' 30 bytes", after=mark, timeout=15)
        time.sleep(1.5)
        mark = len(vm.text())
        notes = NotesWindow(desk, desk.spotlight('notes'))
        vm.wait('[NOTES] 3 notes in /Users/jonas/Notes', after=mark)
        vm.wait("[NOTES] Opened 'New Note.txt' 30 bytes", after=mark)
        ok('closing the window saved the edit (reopened with 30 bytes)')

        # 5. Type, drop to the console at once and shut down: the save-
        #    before-sync hook writes the note (the console runs no autosave).
        notes.row(1)
        vm.wait("[NOTES] Opened 'New Note 2.txt' 12 bytes", after=mark)
        mark = len(vm.text())
        vm.type('Hi ')
        vm.console()
        before_console = notes_lines(vm, mark)
        assert not any("Saved 'New Note 2.txt'" in line for line in before_console), \
            f'autosave ran before the console switch; hook not exercised: {before_console}'
        mark = len(vm.text())
        vm.type('shutdown\n')
        vm.wait('[POWER] Shutdown', after=mark, timeout=60)
        tail = vm.text()[mark:]
        saved = tail.find("[NOTES] Saved 'New Note 2.txt' 15 bytes")
        assert 0 <= saved < tail.find('[POWER] Shutdown'), 'note was not saved before shutdown'
        vm.process.wait(timeout=15)
        ok('console shutdown saved the unsaved note first (hook), 15 bytes')
    finally:
        vm.close()

    vm = VM('aarch64', disk, out, 'notes2')
    try:
        vm.wait('Entering main loop')
        desk.vm = vm
        vm.type(PASSWORD + '\n')
        vm.wait('[UI] Desktop presented', timeout=40)

        # 6. Cold boot: every note is back, byte for byte.
        mark = len(vm.text())
        notes = NotesWindow(desk, desk.spotlight('notes'))
        vm.wait('[NOTES] 3 notes in /Users/jonas/Notes', after=mark)
        vm.wait("[NOTES] Opened 'New Note.txt' 30 bytes", after=mark)
        notes.row(1)
        vm.wait("[NOTES] Opened 'New Note 2.txt' 15 bytes", after=mark)
        ok('after a cold boot both notes have the saved byte counts (30, 15)')
        desk.snap('04-cold-boot')

        # 7. A file longer than the editor (lines > 79 columns) opens
        #    read-only; typing does not change or save it.
        desk.spotlight('terminal')
        vm.type('cp /System/Web/home.html Notes/Web.txt\n'); time.sleep(1)
        mark = len(vm.text())
        notes = NotesWindow(desk, desk.spotlight('notes'))            # raise -> re-scan
        notes.row(3)                                                  # New Note, New Note 2, Notes, Web
        size = int(wait_re(vm, r"\[NOTES\] Opened 'Web\.txt' (\d+) bytes read-only", mark)[1])
        mark = len(vm.text())
        vm.type('zzz'); desk.chord('meta_l', 's'); time.sleep(3)
        assert not any("Saved 'Web.txt'" in line for line in notes_lines(vm, mark)), 'read-only note was saved'
        desk.snap('05-read-only')
        notes.row(0); notes.row(3)
        wait_re(vm, r"\[NOTES\] Opened 'Web\.txt' %d bytes read-only" % size, mark)
        ok(f'too-long file opened read-only, typing + Cmd+S left it at {size} bytes')

        # 8. Spotlight finds a note and opens it in Notes.
        mark = len(vm.text())
        desk.chord('meta_l', 'spc'); time.sleep(.3)
        vm.type('new note 2'); desk.snap('06-spotlight-note', .8)
        vm.press('ret')
        vm.wait("[NOTES] Opened 'New Note 2.txt' 15 bytes", after=mark, timeout=20)
        ok('Spotlight result for a note opened it in Notes')

        # 9. Lock right after typing: the note is saved while locked and is
        #    still there after unlocking.
        time.sleep(1.5)
        mark = len(vm.text())
        vm.type('Ok ')
        locked_at = time.monotonic()
        desk.chord('ctrl', 'meta_l', 'q')
        vm.wait("[NOTES] Saved 'New Note 2.txt' 18 bytes", after=mark, timeout=15)
        lock_latency = time.monotonic() - locked_at
        desk.snap('07-locked', 1.0)
        mark = len(vm.text())
        vm.type(PASSWORD + '\n')
        vm.wait("User 'jonas' logged in", after=mark, timeout=40)
        vm.wait('[UI] Desktop presented', after=mark, timeout=40)
        ok(f'locking saved the note (seen {lock_latency:.2f}s after the lock keys)')

        # 10. Dark mode look (Control Center > Appearance).
        desk.click(desk.width - 40, 13); time.sleep(.6)
        desk.click(desk.width - 340 + 16 + 69, 36 + 106 + 28)
        vm.press('esc')
        desk.snap('08-dark', 2.5)
        (out / 'checks.txt').write_text('\n'.join(checks) + '\n')
        print('PASS: Notes sidebar, New Note, autosave and save paths work and survive a cold boot; evidence',
              out, flush=True)
    finally:
        vm.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    run(parser.parse_args())
