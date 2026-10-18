# Bug & improvement pass (after cursor/UI work)

Created by Jonas Lee. Jonas asked (2026-10-17): after the needed work, find
bugs/improvements, and keep writing context into the project while working.
Each item: what was wrong, the fix, and how it was verified.

## Fixed

1. **Data loss: copying a file onto itself.** Terminal `cp a a` / `cp a ./a`
   and Finder Cmd+C, Cmd+V in the same folder opened the destination with
   TRUNC before reading the source -> file wiped to 0 bytes. `copy_file_path`
   now refuses identical/existing destinations, checks short writes (disk
   full) and removes partial copies. Terminal `cp` normalises both paths,
   reports "same file", copies *into* a directory destination (Unix), and
   overwrites other existing files like Unix. Finder paste picks a free
   macOS-style name: `a copy.txt`, `a copy 2.txt`.
2. **Finder Cmd+C opened Control Center** (global shortcut ran first), so
   Finder copy was unreachable. Finder now owns Cmd+C when focused (as the
   editors already did). README shortcut table updated.
3. **Stale Finder listings.** Files created elsewhere never appeared. Finder
   re-reads on raise and after every Terminal command. Selection is clamped
   after refresh (Delete/Enter could hit a stale entry); a vanished folder
   no longer retries `opendir` every frame.
4. **Back/forward history (Finder and Web).** Navigating after Back did not
   discard forward entries. Web's Back/Forward replay went through the push
   path; with a full history it shifted/corrupted the whole list.
5. **Unchecked `vfs_join_path`.** On overflow callers used uninitialised
   stack bytes as a path for rename/unlink. Join now leaves "" on failure
   and Finder delete/drag/paste check the result.
6. `echo text > file` stored the space before `>`.
7. **Account names "." / ".." / ".hidden" accepted.** Home is /Users/<name>,
   so ".." made the home "/". Leading dots and over-long names (previously
   silently truncated) are rejected; control chars/'|' in full names become
   spaces so a name cannot inject users.db lines. +6 host assertions.

Gate for items 1-7: both builds zero warnings; host suite 1084 C assertions
+ 9 Python, normal and ASan/UBSan; smoke --protected both arches PASS
(`build/smoke-bugs-{arm,intel}`); installed images keep identical data.
Verification: `tests/qemu_files.py` (private disk) - evidence `build/files-3`
(Terminal shows "same file" refusals, original intact, Finder copies). Earlier
UI-pass bug (Finder titlebar toolbar unreachable) is in CURSOR_UI_PLAN.md.

## Candidates / not yet done

- Terminal `adduser` makes an *admin* account for any logged-in user and
  passwords are typed in clear in Terminal history (authorization milestone).
- Finder Delete removes immediately (no Trash/confirmation).
- Finder cannot copy folders (copy_file_path is files only).
- Intel uses relative PS/2 mouse; host/guest pointer can diverge there.
- `raise_window_to_top` memcpy's ~110 KB AppWindowState per raise.
