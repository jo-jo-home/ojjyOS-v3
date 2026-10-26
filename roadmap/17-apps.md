# 17 — Apps (every app, one or more cards each) and every Settings pane

Order of life for each built-in app:
1. **P0 polish** inside today's kernel compositor (Track D `DESKTOP_PLAN.md`).
2. **Port** to a user-space ojjyKit process (`APP-PORT-*`, P2) with
   behaviour parity, then delete its kernel code.
3. **Grow** into a full modern app (later cards).

App rules (every APP/SET card):
- Uses ojjyKit controls and DS tokens only; follows DS-13 templates.
- Runs sandboxed with a declared entitlement profile once SEC-21 exists.
- Has: menus with standard items (About, Settings…, Quit, Edit menu,
  Window menu, Help), keyboard shortcuts, dark mode, accessibility labels,
  state restoration (UIK-49), an entry in the in-OS help (APP-HELP-01).
- Has a UI automation test in `tests/ui/` (UIK-50) and screenshots in the
  design tour.
- Documents formats it reads/writes in `APPS.md`.

---

## App packaging

### APP-00 — App bundle format v2
- Phase: P2 · Size: S · Status: todo
- Depends: ELF-05
- Files: kernel/src/fs/bundle.c → lib/libojjy/bundle.c, docs/abi/bundles.md, sysroot/Applications/*.app

**Context.** `.app` bundles today hold a manifest parsed by `bundle.c`;
the code itself is inside the kernel.
**Goal.** `Name.app/Contents/{Info.toml, ojjy/<arch>/<exe>, Resources/,
Libraries/, Signature}`: identifier (`com.ojjy.notes`), version,
minimum OS, icon, document types, URL schemes, entitlements, category,
localizations; universal bundles (aarch64 + x86_64).
**Done when.** Built-in apps ship as v2 bundles; Finder shows Get Info
data from `Info.toml`.
**Tests.** Host parser tests (fuzzed).
**Pitfalls.** Keep v1 bundle reading until all apps migrate.
**Refs.** D-007.

---

## P0 — finish current app polish (Track D, DESKTOP_PLAN D5–D8)

### APP-TERM-01 — Terminal themes and colours (D5)
- Phase: P0 · Size: M · Status: todo
- Depends: —
- Files: DESKTOP_PLAN.md D5

**Goal.** As D5: Tahoe Dark/Light/Ocean/Classic Green via `termtheme=`,
per-span colours, coloured prompt and `ls`, `theme <name>`, blinking caret,
scrollback indicator.
**Done when.** D5 acceptance.
**Tests.** As D5.
**Pitfalls.** —
**Refs.** DESKTOP_PLAN.md.

### APP-FINDER-01 — Finder polish (D6)
- Phase: P0 · Size: M · Status: todo
- Depends: —
- Files: DESKTOP_PLAN.md D6

**Goal.** As D6: path bar, status bar, labelled icon view, sidebar
sections incl. Downloads, column sorting, glass toolbar.
**Done when.** D6 acceptance.
**Tests.** As D6.
**Pitfalls.** —
**Refs.** DESKTOP_PLAN.md.

### APP-TEXTEDIT-01 — TextEdit polish (D6)
- Phase: P0 · Size: S · Status: todo
- Depends: —
- Files: DESKTOP_PLAN.md D6

**Goal.** Soft word wrap, word/character count, save notification.
**Done when.** D6 acceptance.
**Tests.** As D6.
**Pitfalls.** —
**Refs.** DESKTOP_PLAN.md.

### APP-NOTES-01 — Notes polish (D6)
- Phase: P0 · Size: S · Status: doing
- Depends: —
- Files: DESKTOP_PLAN.md D6, kernel/src/ui/notes.c/h (new), kernel/src/ui/compositor.c, kernel/src/ui/services.c, kernel/src/main.c (hook), tests/test_notes.c (new), tests/qemu_notes.py (new)

**Context (2026-10-26).** Notes edits exactly one file,
`~/Documents/Notes.txt`, and rewrites it (plus re-indexes Spotlight) on
every key press. Loading a file and saving it again adds one empty line
each time, and a file longer than the editor grid (64 lines x 79
columns) is cut short when it is saved.
**Goal.** Sidebar list of `~/Notes/*.txt`, New Note, autosave.
**Steps.**
- [x] 1. Notes library `kernel/src/ui/notes.c/h` (no drawing): `*.txt` filter, name sort (RAMFS has no timestamps), first-line preview, unique "New Note N.txt" names, `~/Notes` creation with a one-time copy of `~/Documents/Notes.txt`, scan/create, autosave timing. Host test `tests/test_notes.c` on a real RAMFS mounted at `/Users`, plus a preview fuzz loop; wired into `scripts/test.sh` and `kernel/Makefile`. Test: `bash scripts/test.sh` + sanitizer run. Trap: `vfs.c` calls `users_*` for home isolation; the test stubs those three hooks (as `test_persist` does) instead of linking `users.c`. — done 2026-10-26 (test: `bash scripts/test.sh` test_notes 91/91 incl. 4000-buffer preview fuzz, sanitizer run 91/91 with no ASan/UBSan reports, aarch64 dev build 0 warnings; evidence build/app-notes-01-step1-*.log)
- [ ] 2. Editor safety for autosave (`compositor.c` `textedit_load_file`/`textedit_save`): a load/save round trip keeps the byte count (no extra empty line); files longer than 64 lines or 79 columns open read-only and are never written back. Test: `tests/qemu_notes.py` byte counts across reopen + a copied long file stays unchanged. Trap: a full 64-line file ending in a newline is not "too long".
- [ ] 3. Notes window (`compositor.c`): sidebar (title + preview rows, selection, wheel scroll), New Note button + Cmd+N, typing into an empty library creates a note, autosave 1 s after the last edit instead of a write per key, save on switch/close/lock, re-scan on open/raise, reload on user switch, read-only notice. Test: `tests/qemu_notes.py` logs + screenshots. Trap: windows move between `app_states` slots on raise, so always look up by `app_window_index[APP_NOTES]`; keep the list one global (one Notes window), not per window (BSS).
- [ ] 4. `hook:` `main.c` `save_persistent_state()` first calls `compositor_save_documents()`, so sync/shutdown (also from the console after Shift+Esc) never loses the last second of typing. Test: `tests/qemu_notes.py` types, shuts down at once from the console, cold-boots and checks the byte count. Trap: only when the UI was initialized.
- [ ] 5. Spotlight finds notes (`services.c` indexes `~/Notes`) and opens them in Notes, not TextEdit (`open_path`). Test: `tests/qemu_notes.py` Spotlight query. Trap: the shared 32-slot file index.
- [ ] 6. Live test `tests/qemu_notes.py` (private disk, fresh data) runs all of the above on TCG and HVF; screenshots light + dark looked at. Evidence `build/app-notes-01-*`.
**Done when.**
- [ ] Notes shows a sidebar of `~/Notes/*.txt` (title + first-line preview, sorted by name); clicking a row opens that note.
- [ ] New Note (button and Cmd+N) creates and opens "New Note.txt", "New Note 2.txt", ...
- [ ] Edits save about 1 s after typing stops (one write per pause, not per key) and on switch, close, lock, sync and shutdown; the text survives a cold boot byte for byte.
- [ ] An existing `~/Documents/Notes.txt` is copied (not moved) into `~/Notes` on first launch; too-long notes are read-only, never truncated.
- [ ] Both dev builds 0 warnings; `scripts/test.sh` + sanitizer run pass; smoke passes; `checklist.py --strict` clean.
- [ ] Docs: `APPS.md` Notes section, DESKTOP_PLAN D6 log line, this card's Log, `STATUS.md`, `AGENTS.md` LATEST.
**Tests.** `tests/test_notes.c` (host + ASan/UBSan), `tests/qemu_notes.py` (TCG + HVF), `tests/qemu_smoke.py`.
**Pitfalls.** RAMFS has 256 nodes per instance in total, so the list is capped
(48 notes); `pmm_alloc_bytes` cannot free; the editor grid is 64 x 79.
**Refs.** DESKTOP_PLAN.md D6.

### APP-CAL-01 — Calendar, Calculator, Activity Monitor, About polish (D7)
- Phase: P0 · Size: M · Status: todo
- Depends: —
- Files: DESKTOP_PLAN.md D7

**Goal.** As D7: Calendar today circle + chips + glass header; Calculator
key highlight, bigger display, history; Activity Monitor sparkline,
memory bar, zebra table, segmented tabs; About hero icon, version card,
credits to Jonas Lee.
**Done when.** D7 acceptance.
**Tests.** As D7.
**Pitfalls.** —
**Refs.** DESKTOP_PLAN.md.

### APP-SET-01 — Settings, Login and Preview polish (D8)
- Phase: P0 · Size: M · Status: todo
- Depends: —
- Files: DESKTOP_PLAN.md D8

**Goal.** As D8: glass sidebar, Network/Users restyle, General page,
Liquid Glass login card, Preview opens PNG/JPEG from Finder.
**Done when.** D8 acceptance.
**Tests.** As D8.
**Pitfalls.** —
**Refs.** DESKTOP_PLAN.md.

---

## P2 — port every app to a user-space process

Each port card: same features as the kernel version, ojjyKit UI, runs as a
process launched through `appregd`, survives its own crash without
affecting the desktop, kernel code removed in the same card.

### APP-PORT-01 — Calculator (pilot port)
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-11, UIK-10, SHL-27, APP-00
- Files: apps/Calculator/ (from kernel/src/ui/calcmath.c + compositor.c)

**Context.** BETTER_OS_PLAN §1 chose Calculator as the pilot.
**Goal.** First real app process: window, buttons, keyboard input,
menus, copy/paste, `test_calc.c` logic reused.
**Done when.** Calculator runs as a process; `kill` on it leaves the
desktop untouched; old kernel code deleted.
**Tests.** `test_calc.c`; UI automation test.
**Pitfalls.** Use this port to fix every rough edge in UIK/WS before
porting the rest — log them as cards.
**Refs.** —

### APP-PORT-02 — About this ojjyOS
- Phase: P2 · Size: S · Status: todo
- Depends: APP-PORT-01, SYS-09
- Files: apps/About/

**Goal.** About window as a process with hero logo, version/build, chip,
memory, displays, storage summary, "Created by Jonas Lee", "System
Report…" button (APP-SYSINFO-01).
**Done when.** Parity + live system data.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** DS-14.

### APP-PORT-03 — Notes
- Phase: P2 · Size: M · Status: todo
- Depends: APP-PORT-01, UIK-18, UIK-22
- Files: apps/Notes/

**Goal.** Port with sidebar + editor layout (DS-13), autosave.
**Done when.** Parity; notes survive app crash (autosave).
**Tests.** UI test.
**Pitfalls.** Keep the existing notes files compatible.
**Refs.** —

### APP-PORT-04 — TextEdit
- Phase: P2 · Size: M · Status: todo
- Depends: APP-PORT-01, UIK-18, UIK-29
- Files: apps/TextEdit/

**Goal.** Port with document architecture basics, open/save panels.
**Done when.** Parity incl. word wrap and counts.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### APP-PORT-05 — Preview
- Phase: P2 · Size: M · Status: todo
- Depends: APP-PORT-01, UIK-34, GFX-03
- Files: apps/Preview/

**Goal.** Port image viewer (zoom to fit, zoom, rotate view, next/prev in
folder, slideshow of wallpapers).
**Done when.** Parity.
**Tests.** UI test.
**Pitfalls.** Decode in the sandboxed process.
**Refs.** —

### APP-PORT-06 — Calendar
- Phase: P2 · Size: M · Status: todo
- Depends: APP-PORT-01, UIK-32
- Files: apps/Calendar/

**Goal.** Port month view and events file format.
**Done when.** Parity.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### APP-PORT-07 — Activity Monitor
- Phase: P2 · Size: M · Status: todo
- Depends: APP-PORT-01, PROC-09, MEM-17, UIK-21
- Files: apps/ActivityMonitor/

**Goal.** Port with real process data (CPU, memory, threads, user),
quit/force quit with permissions.
**Done when.** Parity + real numbers.
**Tests.** UI test with a burner process.
**Pitfalls.** —
**Refs.** —

### APP-PORT-08 — Terminal
- Phase: P2 · Size: L · Status: todo
- Depends: APP-PORT-01, DRV-TTY-01, USR-20, TXT-11
- Files: apps/Terminal/

**Goal.** Terminal app running `ojsh` on a PTY; VT100/xterm emulation
(see APP-TERM-02 for the full list; minimum here: cursor movement, erase,
colours, alternate screen, UTF-8), themes from APP-TERM-01.
**Done when.** Old kernel console commands are available as programs or
shell builtins; parity of user-visible features.
**Tests.** Live tests with escape-sequence fixtures.
**Pitfalls.** Keep kernel console for recovery only.
**Refs.** ECMA-48.

### APP-PORT-09 — Finder
- Phase: P2 · Size: L · Status: todo
- Depends: APP-PORT-01, UIK-21, UIK-22, UIK-23, FS-08
- Files: apps/Finder/

**Goal.** Port with icon and list views, sidebar, toolbar, copy/move/
rename/Trash, open with apps, live updates.
**Done when.** Parity incl. Trash Put Back.
**Tests.** `qemu_files.py` ported to UI automation.
**Pitfalls.** File operations in a background thread with progress.
**Refs.** —

### APP-PORT-10 — Settings
- Phase: P2 · Size: L · Status: todo
- Depends: APP-PORT-01, UIK-46, SVC-10
- Files: apps/Settings/

**Goal.** Port all existing panes (Appearance, Wallpaper, Network,
Users & Accounts, General…) onto the settings store; each later pane is
its own SET card.
**Done when.** Parity; `tests/qemu_appearance.py` passes.
**Tests.** Ported live tests.
**Pitfalls.** Privileged changes go through `authd` (SEC-12).
**Refs.** —

### APP-PORT-11 — Web (ojjyWeb as a process)
- Phase: P2 · Size: L · Status: todo
- Depends: APP-PORT-01, NET-10, WEB-01
- Files: apps/ojjyWeb/ (from kernel/src/ui/browser*.c, kernel/src/web/)

**Goal.** The browser and its engine run in user space (single process
first; multi-process in WEB cards), renamed ojjyWeb.
**Done when.** Web tour passes; kernel web code removed.
**Tests.** `qemu_web_tour.py`, `web_render.c`.
**Pitfalls.** Large move — keep `tests/web_render.c` working throughout.
**Refs.** 27-ojjyweb.md.

### APP-PORT-12 — All built-in apps ported (integration)
- Phase: P2 · Size: S · Status: todo
- Depends: APP-PORT-02, APP-PORT-03, APP-PORT-04, APP-PORT-05, APP-PORT-06, APP-PORT-07, APP-PORT-08, APP-PORT-09, APP-PORT-10, APP-PORT-11
- Files: kernel/src/ui/compositor.c

**Goal.** Verify no app code remains in the kernel; unblock WS-18.
**Done when.** `grep APP_` in kernel finds only compatibility shims to
delete in WS-18.
**Tests.** Full matrix.
**Pitfalls.** —
**Refs.** —

---

## Growing the existing apps

### APP-FINDER-02 — Finder views: column and gallery, view options
- Phase: P3 · Size: M · Status: todo
- Depends: APP-PORT-09, SHL-21
- Files: apps/Finder/views/

**Goal.** Column view (Miller columns with preview column), gallery view
(large preview + filmstrip + metadata), per-folder view options (icon
size, grid spacing, sort, group by kind/date/tags, show item info,
calculate sizes), thumbnails from quicklookd.
**Done when.** All four views work with 5,000 files smoothly.
**Tests.** UI automation + performance test.
**Pitfalls.** Thumbnail generation must be lazy and cancellable.
**Refs.** —

### APP-FINDER-03 — Finder tabs and windows
- Phase: P4 · Size: S · Status: todo
- Depends: APP-FINDER-02, UIK-27
- Files: apps/Finder/

**Goal.** Multiple windows, tabs (Cmd-T), drag tabs out, merge all
windows, "Open in New Tab", spring-loaded folders during drags.
**Done when.** Tabs and windows behave like macOS Finder.
**Tests.** UI tests.
**Pitfalls.** —
**Refs.** —

### APP-FINDER-04 — Tags, search and smart folders
- Phase: P4 · Size: M · Status: todo
- Depends: APP-FINDER-02, FS-07, SVC-30
- Files: apps/Finder/search/

**Goal.** Coloured tags (sidebar section, assign via menu/drag), search
field with scopes (This Mac / current folder) and tokens (kind, date,
tag), saved searches as smart folders, Recents view.
**Done when.** Smart folder of "PDFs modified this week" updates live.
**Tests.** UI tests with seeded files.
**Pitfalls.** —
**Refs.** —

### APP-FINDER-05 — Get Info and file metadata
- Phase: P3 · Size: S · Status: todo
- Depends: APP-PORT-09, FS-09
- Files: apps/Finder/info/

**Goal.** Info window: name/extension, kind, size (computed), where,
dates, tags, comments, "Open with" (change all), sharing & permissions
(owner, group, ACL editor with admin auth), locked flag, preview.
**Done when.** Permission edits apply and persist.
**Tests.** UI tests.
**Pitfalls.** —
**Refs.** —

### APP-FINDER-06 — Robust file operations
- Phase: P3 · Size: M · Status: todo
- Depends: APP-PORT-09, FS-15
- Files: apps/Finder/fileops/

**Goal.** Background copy/move with progress window, pause/cancel,
conflict dialog (Keep Both, Replace, Skip, apply to all), undo of moves
and renames, clone copies on ojjyFS, cross-volume moves, verify after
copy option, Duplicate, Make Alias/symlink, batch rename.
**Done when.** Copying 10,000 small files shows accurate progress and
can be cancelled cleanly.
**Tests.** Live tests incl. disk-full and removed-disk cases.
**Pitfalls.** Never leave half-copied files without marking them.
**Refs.** —

### APP-FINDER-07 — Sidebar, volumes and network locations
- Phase: P3 · Size: S · Status: todo
- Depends: APP-PORT-09, STO-03
- Files: apps/Finder/sidebar/

**Goal.** Favorites (editable), iCloud-like placeholders hidden, Locations
(volumes with eject, network, Mac shared folder), Tags; "Connect to
Server" (SMB later, NET-70).
**Done when.** USB stick appears/disappears live; eject works.
**Tests.** QEMU USB test.
**Pitfalls.** —
**Refs.** —

### APP-FINDER-08 — Archives
- Phase: P4 · Size: S · Status: todo
- Depends: APP-ARCHIVE-01
- Files: apps/Finder/

**Goal.** Compress (zip) selection, double-click archives to expand via
Archive Utility, preview archive contents in Quick Look.
**Done when.** Round trip zip ↔ macOS Archive Utility compatible.
**Tests.** Live test.
**Pitfalls.** Zip-slip path traversal.
**Refs.** —

### APP-TERM-02 — Terminal emulation completeness
- Phase: P3 · Size: M · Status: todo
- Depends: APP-PORT-08
- Files: apps/Terminal/vt/

**Goal.** xterm-256color compatible: SGR attributes, 256 and 24-bit
colour, alternate screen, scroll regions, insert/delete line/char, tabs,
bracketed paste, mouse reporting (SGR 1006), OSC title, OSC 8 hyperlinks,
OSC 52 clipboard (opt-in), Unicode width (wide CJK, emoji), combining
characters, `TERM=xterm-256color` terminfo shipped.
**Done when.** `vim`, `htop`-like and `less` ports render correctly;
vttest-style fixtures pass.
**Tests.** Host tests with escape fixtures (golden screens).
**Pitfalls.** Performance with huge outputs (cat of 100 MB).
**Refs.** ECMA-48; XTerm control sequences (public doc).

### APP-TERM-03 — Terminal tabs, splits, profiles
- Phase: P4 · Size: M · Status: todo
- Depends: APP-TERM-02, UIK-27
- Files: apps/Terminal/

**Goal.** Tabs, split panes (horizontal/vertical), profiles (font, size,
theme, opacity/blur, cursor style), find in scrollback, 100k-line
scrollback, marks and jump between prompts (shell integration), copy
modes, "New Terminal at Folder" from Finder.
**Done when.** Features usable from menus and shortcuts.
**Tests.** UI tests.
**Pitfalls.** —
**Refs.** —

### APP-TEXTEDIT-02 — TextEdit rich text and formats
- Phase: P4 · Size: M · Status: todo
- Depends: APP-PORT-04, UIK-31
- Files: apps/TextEdit/formats/

**Goal.** Rich text (fonts, sizes, bold/italic/underline, colours,
alignment, lists, tables simple), formats: plain text (encodings), RTF
read/write, HTML read/write, Markdown read/export, DOCX read (basic);
print (SVC-50), versions (UIK-48).
**Done when.** A DOCX with headings and lists opens readably; RTF round
trip with macOS TextEdit.
**Tests.** Format fixtures (fuzzed parsers).
**Pitfalls.** DOCX/RTF parsers are untrusted-input parsers.
**Refs.** RTF 1.9 spec, ECMA-376 (OOXML).

### APP-NOTES-02 — Notes: rich notes and organisation
- Phase: P4 · Size: M · Status: todo
- Depends: APP-PORT-03, UIK-43
- Files: apps/Notes/

**Goal.** Folders, pinned notes, checklists, tables, images and
attachments, links between notes, search, Quick Note (hot corner),
lock note with password, export to PDF/Markdown; storage format
documented (one folder per note with JSON + assets).
**Done when.** Feature list complete; notes searchable from Spotlight.
**Tests.** UI tests.
**Pitfalls.** Migration from the plain-text notes.
**Refs.** —

### APP-PREVIEW-02 — Preview: PDF and image editing
- Phase: P6 · Size: L · Status: todo
- Depends: APP-PORT-05, GFX-08
- Files: apps/Preview/

**Goal.** PDF viewing (thumbnails sidebar, search, select text, copy),
annotations (highlight, notes, shapes, signature), fill forms, merge/
reorder pages; image adjustments (crop, rotate, resize, colour adjust,
markup), export to PNG/JPEG/PDF/HEIC.
**Done when.** A PDF form can be filled, signed and saved.
**Tests.** UI tests with fixture PDFs.
**Pitfalls.** Save must not corrupt PDFs (incremental update).
**Refs.** ISO 32000-2.

### APP-CAL-02 — Calendar: full calendaring
- Phase: P6 · Size: L · Status: todo
- Depends: APP-PORT-06, NET-76
- Files: apps/Calendar/

**Goal.** Day/week/month/year views, multiple calendars with colours,
recurring events (RRULE), alerts via notifyd, invitations (iTIP basic),
time zones, CalDAV accounts (Internet Accounts), ICS import/export.
**Done when.** Sync with a CalDAV server both ways.
**Tests.** Host RRULE tests; live sync test with a local server.
**Pitfalls.** Recurrence exceptions.
**Refs.** RFC 5545, 5546.

### APP-CALC-02 — Calculator modes
- Phase: P4 · Size: S · Status: todo
- Depends: APP-PORT-01
- Files: apps/Calculator/

**Goal.** Scientific mode, programmer mode (hex/bin/oct, bitwise),
history tape, unit and currency conversion (rates downloaded daily,
opt-in), RPN option.
**Done when.** Modes switch with Cmd-1/2/3.
**Tests.** `test_calc.c` extended with scientific functions (user space
allows FP — double precision with correct rounding display).
**Pitfalls.** Decimal display of binary floats — use decimal arithmetic
for basic mode.
**Refs.** —

### APP-MON-02 — Activity Monitor: full tabs
- Phase: P4 · Size: M · Status: todo
- Depends: APP-PORT-07, SCH-07, BLK-01, NET-01
- Files: apps/ActivityMonitor/

**Goal.** CPU, Memory (pressure graph), Energy (on laptops), Disk, Network
tabs with graphs; process inspector (open files, ports, threads,
sample/backtrace), hierarchical view, filters, Dock icon graph mode.
**Done when.** All tabs show live data.
**Tests.** UI tests.
**Pitfalls.** Sampling overhead.
**Refs.** —

---

## Settings panes (one card each)

All panes live in the Settings app (APP-PORT-10) on ojjyKit forms
(UIK-38), read/write the settings store (SVC-10), require admin
authentication (lock icon) for system-wide changes, and appear in
Settings search.

### SET-01 — Settings search and navigation
- Phase: P3 · Size: S · Status: todo
- Depends: APP-PORT-10
- Files: apps/Settings/search.c

**Goal.** Search field highlighting matching panes and controls,
back/forward, deep links (`ojjy-settings://network/wifi`) used by other
apps and Spotlight.
**Done when.** Searching "dark" jumps to Appearance.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-02 — General > About
- Phase: P3 · Size: S · Status: todo
- Depends: APP-PORT-10, SYS-09
- Files: apps/Settings/panes/about.c

**Goal.** Name (editable computer name → hostname/mDNS), version, build,
chip, memory, serial-like ID, displays, storage, "System Report".
**Done when.** Renaming the computer updates `.local` name.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-03 — General > Software Update
- Phase: P5 · Size: S · Status: todo
- Depends: SVC-40
- Files: apps/Settings/panes/update.c

**Goal.** Check/download/install updates, automatic updates options,
release notes, rollback to previous version button.
**Done when.** Update flow works end to end with a local update server.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SET-04 — General > Storage
- Phase: P6 · Size: S · Status: todo
- Depends: FS-19
- Files: apps/Settings/panes/storage.c

**Goal.** Usage bar by category, recommendations (empty Trash
automatically, remove old downloads), per-volume list.
**Done when.** Categories match Finder totals within 5%.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SET-05 — General > Language & Region
- Phase: P4 · Size: S · Status: todo
- Depends: I18N-01
- Files: apps/Settings/panes/language.c

**Goal.** Preferred languages, per-app language, region formats (dates,
numbers, currency, measurement, first weekday, calendar), live preview.
**Done when.** Changing region changes menu bar date format immediately.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-06 — General > Date & Time
- Phase: P2 · Size: S · Status: todo
- Depends: TIM-03
- Files: apps/Settings/panes/datetime.c

**Goal.** Automatic time (NTP server), time zone (map or list,
automatic by IP opt-in), 24-hour toggle (exists), clock options for the
menu bar (seconds, date, analog).
**Done when.** Zone change updates the clock.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-07 — General > Sharing
- Phase: P6 · Size: S · Status: todo
- Depends: NET-30
- Files: apps/Settings/panes/sharing.c

**Goal.** Toggles for Remote Login (SSH), File Sharing (SMB, once NET-71
exists), Screen Sharing (NET-31), ojjyDrop visibility, Media sharing; each shows status and
access lists.
**Done when.** Toggling SSH starts/stops `ojsshd`.
**Tests.** Live test.
**Pitfalls.** Off by default.
**Refs.** —

### SET-08 — General > Startup Disk, Transfer or Reset
- Phase: P5 · Size: S · Status: todo
- Depends: DRV-FW-01, SVC-41
- Files: apps/Settings/panes/startup.c

**Goal.** Choose startup volume/slot, restart into Recovery, "Erase All
Content and Settings" (secure wipe of data volume with confirmation).
**Done when.** Erase flow returns the VM to Setup Assistant.
**Tests.** Live test on a private disk.
**Pitfalls.** Multiple confirmations; admin password.
**Refs.** —

### SET-09 — Appearance
- Phase: P3 · Size: S · Status: todo
- Depends: APP-PORT-10, DS-11
- Files: apps/Settings/panes/appearance.c

**Context.** Exists (Light/Dark/Auto, accents, glass, menu bar, corners,
Terminal theme).
**Goal.** Port plus: icon & widget style (DS-06), sidebar icon size,
scroll bar visibility, click in scroll bar behaviour, text size shortcut.
**Done when.** All options apply live.
**Tests.** `qemu_appearance.py` ported.
**Pitfalls.** —
**Refs.** —

### SET-10 — Accessibility
- Phase: P4 · Size: M · Status: todo
- Depends: A11Y-01
- Files: apps/Settings/panes/accessibility.c

**Goal.** Vision (screen reader, zoom, display: increase contrast, reduce
transparency, reduce motion, colour filters, pointer size, text size),
Hearing (captions, mono audio), Motor (sticky/slow keys, mouse keys,
dwell, keyboard navigation), shortcut panel (Cmd-Opt-F5).
**Done when.** Every option implemented by A11Y cards is reachable.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-11 — Control Center and menu bar
- Phase: P4 · Size: S · Status: todo
- Depends: SHL-11
- Files: apps/Settings/panes/controlcenter.c

**Goal.** Choose which modules show in the menu bar / Control Center,
clock options, auto-hide menu bar.
**Done when.** Changes apply live.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-12 — Spotlight
- Phase: P4 · Size: S · Status: todo
- Depends: SHL-08
- Files: apps/Settings/panes/spotlight.c

**Goal.** Result categories on/off, privacy exclusions (folders), web
suggestions toggle, reindex button.
**Done when.** Excluded folder results disappear.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SET-13 — Privacy & Security
- Phase: P5 · Size: M · Status: todo
- Depends: SEC-11, SEC-22
- Files: apps/Settings/panes/privacy.c

**Goal.** Per-permission app lists (Location, Contacts, Calendars,
Photos, Camera, Microphone, Screen Recording, Files & Folders, Full Disk
Access, Accessibility, Input Monitoring), security (allow apps from
Store / identified developers / anywhere with warning), FileVault-like
encryption, firewall, lockdown-style hardened mode (optional).
**Done when.** Revoking camera access blocks the Camera app next launch.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### SET-14 — Desktop & Dock
- Phase: P3 · Size: S · Status: todo
- Depends: SHL-05, SHL-14
- Files: apps/Settings/panes/dock.c

**Goal.** Dock size, magnification, position, minimise effect, auto-hide,
show recents, indicators, Stage/tiling modes, Mission Control options,
hot corners, taskbar mode toggle (SHL-06), default web browser.
**Done when.** All options live.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-15 — Displays
- Phase: P4 · Size: M · Status: todo
- Depends: DRV-DISP-01, WS-09, WS-13
- Files: apps/Settings/panes/displays.c

**Goal.** Resolution/scaled sizes ("Larger text" ↔ "More space"),
refresh rate, arrangement of multiple displays, mirroring, main display,
Night Shift schedule, brightness, colour profile.
**Done when.** Scaled sizes change the UI scale live.
**Tests.** QEMU multi-display test.
**Pitfalls.** Confirm dialog with revert timer for risky changes.
**Refs.** —

### SET-16 — Wallpaper and Screen Saver
- Phase: P3 · Size: S · Status: todo
- Depends: SHL-14, DS-10
- Files: apps/Settings/panes/wallpaper.c

**Goal.** Port wallpaper grid (exists) + photos from Pictures, colours,
dynamic wallpapers, per-display/per-Space choice; screen savers (aerial-
like animations, photo slideshow, clock) with start-after and hot corner.
**Done when.** Screen saver starts after the idle time.
**Tests.** UI test.
**Pitfalls.** Screen saver uses little CPU.
**Refs.** —

### SET-17 — Battery / Energy
- Phase: P9 · Size: S · Status: todo
- Depends: SVC-60
- Files: apps/Settings/panes/energy.c

**Goal.** Battery health and history graph, low power mode, sleep timers,
wake for network access, optimized charging (laptops).
**Done when.** Works on a laptop (record).
**Tests.** Hardware record.
**Pitfalls.** —
**Refs.** —

### SET-18 — Lock Screen and Login Password
- Phase: P3 · Size: S · Status: todo
- Depends: SHL-15
- Files: apps/Settings/panes/lockscreen.c

**Goal.** Screen off/lock timers, require password after sleep, message on
lock screen, show user list vs name field, change password, password
hint.
**Done when.** Options apply.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### SET-19 — Users & Groups
- Phase: P3 · Size: M · Status: todo
- Depends: APP-PORT-10, PROC-04
- Files: apps/Settings/panes/users.c

**Context.** Exists as "Users & Accounts".
**Goal.** Add/delete users (admin auth), roles (admin/standard),
groups, guest user, picture, automatic login, reset password; keep S1/S6
security rules.
**Done when.** All operations with correct authorization.
**Tests.** Existing security host tests + UI tests.
**Pitfalls.** —
**Refs.** —

### SET-20 — Passwords
- Phase: P5 · Size: S · Status: todo
- Depends: APP-PASSWORDS-01
- Files: apps/Settings/panes/passwords.c

**Goal.** Shortcut into the Passwords app; AutoFill settings for
ojjyWeb.
**Done when.** —
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-21 — Internet Accounts
- Phase: P6 · Size: M · Status: todo
- Depends: NET-75, NET-76
- Files: apps/Settings/panes/accounts.c, services/accountsd

**Goal.** Add mail/calendar/contacts accounts (IMAP/SMTP, CalDAV,
CardDAV, OAuth2 providers later) shared by Mail, Calendar, Contacts.
**Done when.** One account powers all three apps.
**Tests.** Live test with local servers.
**Pitfalls.** Credentials in the keychain only.
**Refs.** —

### SET-22 — Wi-Fi
- Phase: P9 · Size: S · Status: todo
- Depends: NET-40
- Files: apps/Settings/panes/wifi.c

**Goal.** Networks list, known networks, ask to join, hotspot options,
details (IP, router, DNS, privacy MAC).
**Done when.** Works with real Wi-Fi (record); honest "no Wi-Fi
hardware" state in VMs (today's tile controls the virtual NIC).
**Tests.** UI test (VM state).
**Pitfalls.** —
**Refs.** —

### SET-23 — Bluetooth
- Phase: P9 · Size: S · Status: todo
- Depends: NET-60
- Files: apps/Settings/panes/bluetooth.c

**Goal.** Device list, pairing flow with codes, forget, battery levels.
**Done when.** Pairing works (record).
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-24 — Network (and VPN, Firewall)
- Phase: P5 · Size: M · Status: todo
- Depends: NET-11, NET-14
- Files: apps/Settings/panes/network.c

**Context.** Exists (virtual NIC status, Wi-Fi tile honesty).
**Goal.** Services list (Ethernet, Wi-Fi, USB, VPN), per-service details
(DHCP/manual IPv4/IPv6, DNS, search domains, proxies, MTU), locations,
firewall settings and app list; VPN configurations (WireGuard import) are
added when NET-50 lands.
**Done when.** Static IP config works and persists.
**Tests.** Live tests.
**Pitfalls.** Changing network must not drop the UI.
**Refs.** —

### SET-25 — Notifications and Focus
- Phase: P4 · Size: S · Status: todo
- Depends: SHL-13
- Files: apps/Settings/panes/notifications.c

**Goal.** Per-app notification style (banners/alerts/none), previews,
sounds, badges; Focus mode editor.
**Done when.** Per-app settings respected.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SET-26 — Sound
- Phase: P3 · Size: S · Status: todo
- Depends: AUD-02, DS-08
- Files: apps/Settings/panes/sound.c

**Goal.** Output/input device selection, volume, balance, input level
meter, alert sound choice and volume, startup sound toggle, UI sound
effects toggle.
**Done when.** Device switching routes audio live.
**Tests.** Live test with two QEMU audio devices.
**Pitfalls.** —
**Refs.** —

### SET-27 — Keyboard
- Phase: P4 · Size: S · Status: todo
- Depends: I18N-02, SHL-19
- Files: apps/Settings/panes/keyboard.c

**Goal.** Key repeat rate/delay (today fixed 450/35 ms), input sources
(layouts, IMEs), modifier key remapping (Caps Lock → Ctrl/Esc, swap
Cmd/Opt for PC keyboards), shortcuts editor, dictation placeholder off,
text replacements.
**Done when.** Remapping Caps Lock works system-wide.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### SET-28 — Mouse and Trackpad
- Phase: P4 · Size: S · Status: todo
- Depends: DRV-IN-04
- Files: apps/Settings/panes/pointer.c

**Goal.** Tracking speed with acceleration curve, natural scrolling,
secondary click, tap to click, gestures with preview animations, scroll
speed.
**Done when.** Settings apply live.
**Tests.** UI tests.
**Pitfalls.** Absolute tablet in VMs has no acceleration.
**Refs.** —

### SET-29 — Printers & Scanners
- Phase: P6 · Size: S · Status: todo
- Depends: SVC-50
- Files: apps/Settings/panes/printers.c

**Goal.** Add printers (discovered IPP Everywhere via mDNS, manual IP),
default printer, queue viewer.
**Done when.** Printing to an IPP test printer works.
**Tests.** Live test with a simulated IPP printer.
**Pitfalls.** —
**Refs.** —

### SET-30 — Game Controllers
- Phase: P6 · Size: S · Status: todo
- Depends: DRV-IN-06
- Files: apps/Settings/panes/controllers.c

**Goal.** Connected controllers, button test view, remapping.
**Done when.** —
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SET-31 — Developer (Linux environment, ojjyWin, packages)
- Phase: P7 · Size: S · Status: todo
- Depends: LNX-40, PKG-01
- Files: apps/Settings/panes/developer.c

**Goal.** Enable Linux environment (download/install), ojjyWin options
(Windows version reported, DPI, drive mapping), package repositories
(enable community/OUR with warnings), developer mode (debugging
entitlements, kernel log level).
**Done when.** Toggling Linux environment installs it.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

---

## New apps

### APP-ARCHIVE-01 — Archive Utility and libarchive-like library
- Phase: P4 · Size: M · Status: todo
- Depends: APP-PORT-09, USR-01
- Files: lib/libarchive_ojjy/, apps/ArchiveUtility

**Goal.** Read/write zip (deflate, store, zip64), tar (+gz, +xz read,
+zstd), read 7z (LZMA) and rar (read-only, if feasible from public
docs); expand on double-click; progress; encrypted zip (AES) read.
**Done when.** Archives created on macOS/Windows open correctly.
**Tests.** Host fixtures, fuzzing, zip-slip tests.
**Pitfalls.** Decompression bombs: limits and progress-based abort.
**Refs.** PKWARE APPNOTE.TXT, POSIX ustar.

### APP-DISK-01 — Disk Utility
- Phase: P3 · Size: M · Status: todo
- Depends: STO-02, FS-14
- Files: apps/DiskUtility/

**Goal.** Volumes/disks sidebar, info (SMART for NVMe), First Aid,
Erase (format choices), Partition (pie chart editor), Mount/Unmount,
create disk images, restore image to disk.
**Done when.** Format and partition a USB stick end to end.
**Tests.** Live tests on virtual disks.
**Pitfalls.** Multiple confirmations for destructive actions.
**Refs.** —

### APP-CONSOLE-01 — Console (logs)
- Phase: P3 · Size: S · Status: todo
- Depends: SVC-20
- Files: apps/Console/

**Goal.** Live streaming log viewer (process, subsystem, level filters,
search), crash reports list, kernel log, export.
**Done when.** Live log streams while apps run.
**Tests.** UI test.
**Pitfalls.** Privacy: redacted fields for non-admins.
**Refs.** —

### APP-SYSINFO-01 — System Information
- Phase: P3 · Size: S · Status: todo
- Depends: DRV-CORE-01, SYS-09
- Files: apps/SystemInformation/

**Goal.** Hardware (CPU, memory, PCI, USB tree, storage, displays, audio,
network), software (installed apps, packages, extensions, logs), export
report.
**Done when.** USB tree matches `lsusb`.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### APP-FONTS-01 — Font Book
- Phase: P4 · Size: S · Status: todo
- Depends: TXT-08
- Files: apps/FontBook/

**Goal.** Browse families, preview, install/remove user fonts, validate
fonts (TXT-01 checks), collections.
**Done when.** Installing a TTF makes it available in TextEdit.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### APP-PASSWORDS-01 — Passwords (keychain front end)
- Phase: P5 · Size: M · Status: todo
- Depends: SEC-40
- Files: apps/Passwords/

**Goal.** Saved website/app passwords, passkeys (later with WEB),
Wi-Fi passwords, verification codes (TOTP), security recommendations
(reused/weak), import from CSV (Chrome/Safari exports), auth required to
view.
**Done when.** ojjyWeb autofill uses it; TOTP codes correct (RFC 6238
vectors).
**Tests.** Host TOTP tests; UI tests.
**Pitfalls.** Clipboard auto-clear after copy.
**Refs.** RFC 6238.

### APP-PHOTOS-01 — Photos: library and viewing
- Phase: P6 · Size: L · Status: todo
- Depends: UIK-23, GFX-03, USR-40
- Files: apps/Photos/

**Goal.** Library database (ojjyDB, USR-40), import from folders/cameras
(HEIC photos once GFX-06 lands), grid by years/months/days, full-screen viewer with
zoom/swipe, albums, favourites, search by date/place (EXIF GPS),
Live-photo-like video clips playback.
**Done when.** 10,000-photo library scrolls smoothly.
**Tests.** Performance test with generated images.
**Pitfalls.** Thumbnail cache size management.
**Refs.** EXIF 2.32.

### APP-PHOTOS-02 — Photos: editing
- Phase: P6 · Size: M · Status: todo
- Depends: APP-PHOTOS-01, GPU-03
- Files: apps/Photos/edit/

**Goal.** Non-destructive edits: auto-enhance, light/colour sliders,
crop/straighten, filters, red-eye, markup; revert to original.
**Done when.** Edits render in real time at full screen.
**Tests.** Pixel tests of filters.
**Pitfalls.** —
**Refs.** —

### APP-MUSIC-01 — Music player and library
- Phase: P6 · Size: L · Status: todo
- Depends: MED-01, MED-10, UIK-21
- Files: apps/Music/

**Goal.** Import local music (MP3, AAC, FLAC, ALAC, Opus, Vorbis),
library by artists/albums/songs, playlists, queue, gapless playback,
equalizer, mini player, Now Playing in Control Center, media keys,
internet radio streams.
**Done when.** 5,000-track library plays gaplessly.
**Tests.** Live tests with fixture audio.
**Pitfalls.** Tag parsing (ID3v2, Vorbis comments, MP4 atoms) fuzzed.
**Refs.** ID3v2.4 spec.

### APP-VIDEO-01 — Videos (media player)
- Phase: P6 · Size: M · Status: todo
- Depends: MED-01, UIK-41
- Files: apps/Videos/

**Goal.** Play MP4/MKV/WebM (H.264, VP9, AV1 software decode, AAC/Opus),
subtitles (SRT, WebVTT, embedded), chapters, speed, picture-in-picture,
trimming, export.
**Done when.** 1080p30 H.264 plays smoothly under VZ (software decode).
**Tests.** Live tests with fixtures; dropped-frame counter.
**Pitfalls.** Performance: SIMD decoders, multi-threaded.
**Refs.** —

### APP-MAIL-01 — Mail
- Phase: P6 · Size: L · Status: todo
- Depends: NET-75, SET-21, UIK-40, UIK-25
- Files: apps/Mail/

**Goal.** Multiple accounts, unified inbox, threaded conversations,
compose (rich text, attachments, signatures), search (local index),
rules, junk filtering (simple Bayesian), remote content blocked by
default, notifications.
**Done when.** Daily use with a real IMAP account works.
**Tests.** Live tests with a local IMAP/SMTP server.
**Pitfalls.** HTML mail rendering only in a sandboxed web view.
**Refs.** —

### APP-CONTACTS-01 — Contacts
- Phase: P6 · Size: M · Status: todo
- Depends: NET-76, SEC-11
- Files: apps/Contacts/, services/contactsd

**Goal.** Contact cards, groups, vCard import/export, CardDAV sync,
contacts API for apps (permission-gated).
**Done when.** Sync with a CardDAV server.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** RFC 6350.

### APP-REMIND-01 — Reminders
- Phase: P6 · Size: M · Status: todo
- Depends: SVC-11, NET-76
- Files: apps/Reminders/

**Goal.** Lists, due dates/times, recurring, subtasks, flags, smart
lists (Today, Scheduled), notifications, CalDAV VTODO sync.
**Done when.** A due reminder notifies on time.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** RFC 5545 VTODO.

### APP-CLOCK-01 — Clock
- Phase: P4 · Size: S · Status: todo
- Depends: TIM-03, SVC-11
- Files: apps/Clock/

**Goal.** World clock with map, alarms (wake from sleep where possible),
timers, stopwatch with laps.
**Done when.** Alarm fires with sound and notification.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### APP-WEATHER-01 — Weather
- Phase: P6 · Size: S · Status: todo
- Depends: NET-17
- Files: apps/Weather/

**Goal.** Current conditions, hourly and 10-day forecast, multiple
locations, animated backgrounds, widget; data from a free public API
(attribution shown, configurable).
**Done when.** Forecast for New York shows correctly.
**Tests.** Live test with recorded API responses.
**Pitfalls.** API terms of use recorded in THIRD_PARTY.md.
**Refs.** —

### APP-MAPS-01 — Maps
- Phase: P6 · Size: L · Status: todo
- Depends: NET-17, GFX-04
- Files: apps/Maps/

**Goal.** OpenStreetMap vector or raster tiles (respecting tile usage
policy; configurable provider), smooth pan/zoom, search (Nominatim-like
geocoding, opt-in), directions via a public routing service, location
pin (no GPS in VMs).
**Done when.** Search a city and get walking directions.
**Tests.** Live test with recorded responses.
**Pitfalls.** OSM attribution and usage limits.
**Refs.** OSM tile usage policy.

### APP-CAMERA-01 — Camera and Photo Booth
- Phase: P6 · Size: S · Status: todo
- Depends: MED-30, SEC-11
- Files: apps/Camera/

**Goal.** Live preview, take photos/videos, effects, saves to Photos;
camera permission prompt.
**Done when.** Works with a USB webcam (record) or VZ camera if offered.
**Tests.** Live test with a fake camera device.
**Pitfalls.** —
**Refs.** —

### APP-VOICE-01 — Voice Memos
- Phase: P6 · Size: S · Status: todo
- Depends: AUD-02, MED-11
- Files: apps/VoiceMemos/

**Goal.** Record, trim, play, waveform view, export (Opus/AAC).
**Done when.** Record → play back under VZ microphone.
**Tests.** Live test.
**Pitfalls.** Microphone permission + indicator in the menu bar.
**Refs.** —

### APP-CODE-01 — ojjyCode: editor core
- Phase: P6 · Size: L · Status: todo
- Depends: UIK-18, APP-TERM-02
- Files: apps/ojjyCode/

**Goal.** Code editor: file tree, tabs, syntax highlighting (grammar
engine with C, Python, JS/TS, Rust, Markdown, JSON, shell), multiple
cursors, find/replace with regex, minimap, integrated terminal panel,
themes matching Terminal.
**Done when.** Jonas edits ojjyOS source inside ojjyOS comfortably.
**Tests.** UI tests; highlighting golden tests.
**Pitfalls.** Large files (100k lines) must stay smooth.
**Refs.** —

### APP-CODE-02 — ojjyCode: language servers and git
- Phase: P7 · Size: M · Status: todo
- Depends: APP-CODE-01, USR-31
- Files: apps/ojjyCode/lsp/, git/

**Goal.** LSP client (completion, diagnostics, go to definition) for
clangd and others installed via packages, git integration (status,
diff gutter, commit, branch) through the ported git.
**Done when.** clangd diagnostics appear for ojjyOS sources.
**Tests.** Live test with clangd.
**Pitfalls.** —
**Refs.** LSP spec.

### APP-STORE-01 — ojjy Store
- Phase: P6 · Size: L · Status: todo
- Depends: PKG-01, PKG-20, UIK-40
- Files: apps/Store/

**Goal.** Graphical front end for ojjypkg and signed bundles: browse
categories, search, app pages (screenshots, description, ratings later),
install/update/uninstall with progress, Updates tab, Linux and Windows
app sections when available.
**Done when.** Install and update an app from a local repository.
**Tests.** Live tests with a local repo server.
**Pitfalls.** Only signed packages; show permissions before install.
**Refs.** —

### APP-HELP-01 — Help viewer and tips
- Phase: P4 · Size: M · Status: todo
- Depends: UIK-40
- Files: apps/Help/, /System/Library/Help/

**Goal.** Searchable help books (HTML in bundles), Help menu search that
also finds menu items (highlighting them), "Tips" welcome app for new
users.
**Done when.** Help menu search finds "New Folder" in Finder's menu.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### APP-GAMES-01 — Games pack 1: Chess
- Phase: P4 · Size: M · Status: todo
- Depends: UIK-01
- Files: apps/Chess/

**Goal.** Chess with an original engine (alpha-beta, levels), 2 players,
PGN save/load, glass board themes.
**Done when.** Plays legal chess (perft test suite).
**Tests.** Perft host tests.
**Pitfalls.** —
**Refs.** —

### APP-GAMES-02 — Games pack 2: Solitaire, Minesweeper, 2048, Snake
- Phase: P3 · Size: M · Status: todo
- Depends: APP-PORT-01
- Files: apps/Games/

**Context.** BETTER_OS_PLAN §4 lists Snake/2048/Minesweeper.
**Goal.** Four small polished games in Tahoe style with high scores and
animations.
**Done when.** All four playable with keyboard and mouse.
**Tests.** Host logic tests; UI smoke.
**Pitfalls.** —
**Refs.** —

### APP-PAINT-01 — Paint
- Phase: P4 · Size: M · Status: todo
- Depends: GFX-02, UIK-30
- Files: apps/Paint/

**Goal.** Simple raster editor: brushes (pressure with pens), shapes,
fill, text, layers (basic), selection, undo, PNG/JPEG export.
**Done when.** Draw and save an image.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### APP-BOOKS-01 — Books (ePub/PDF reader)
- Phase: P6 · Size: M · Status: todo
- Depends: UIK-40, GFX-08
- Files: apps/Books/

**Goal.** ePub 3 reader (reflowable via the web engine), PDF reading
mode, library, bookmarks, highlights, themes (sepia/night).
**Done when.** A public-domain ePub reads well.
**Tests.** Fixture tests.
**Pitfalls.** —
**Refs.** EPUB 3.3.

### APP-NEWS-01 — Feeds (RSS/Atom reader)
- Phase: P6 · Size: S · Status: todo
- Depends: NET-17
- Files: apps/Feeds/

**Goal.** Subscribe to RSS/Atom feeds, unread counts, reader view.
**Done when.** Common feeds parse.
**Tests.** Parser fixtures (fuzzed XML).
**Pitfalls.** XML external entities disabled.
**Refs.** RFC 4287.

### APP-DICT-01 — Dictionary and thesaurus
- Phase: P6 · Size: S · Status: todo
- Depends: UIK-40
- Files: apps/Dictionary/

**Goal.** Offline dictionary from an openly licensed source, look up from
any text (Ctrl-Cmd-D), Spotlight definitions.
**Done when.** Look up works system-wide.
**Tests.** UI test.
**Pitfalls.** Licence of the dictionary data recorded.
**Refs.** —

### APP-SHORTCUTS-01 — Shortcuts (automation)
- Phase: P7 · Size: L · Status: todo
- Depends: IPC-07, SHL-27
- Files: apps/Shortcuts/, services/automationd

**Goal.** Visual automation: actions exported by apps (ojidl-described),
variables, conditionals, run from menu bar/Spotlight/keyboard shortcut,
also callable from the shell (`shortcuts run`).
**Done when.** "Resize images in a folder" shortcut works.
**Tests.** Live test.
**Pitfalls.** Permissions per action.
**Refs.** —

### APP-MIGRATE-01 — Migration Assistant
- Phase: P6 · Size: M · Status: todo
- Depends: STO-01, SHL-40
- Files: apps/MigrationAssistant/

**Goal.** Move users, files and settings from another ojjyOS machine
(network) or a Rewind backup; import files from a Mac/PC share.
**Done when.** New VM receives a user from an old VM.
**Tests.** Two-VM test.
**Pitfalls.** UID conflicts.
**Refs.** —

### APP-OFFICE-01 — ojjyWrite (word processor)
- Phase: P10 · Size: L · Status: todo
- Depends: APP-TEXTEDIT-02, GFX-08
- Files: apps/ojjyWrite/

**Goal.** Page layout word processor: styles, headers/footers, page
numbers, footnotes, images, tables, track changes (basic), DOCX/ODT
import/export, PDF export, templates.
**Done when.** A school report can be written and exported to DOCX that
opens in Word/Google Docs correctly.
**Tests.** Round-trip fixtures.
**Pitfalls.** DOCX fidelity is endless — define a supported subset.
**Refs.** ECMA-376, ODF 1.3.

### APP-OFFICE-02 — ojjySheets (spreadsheet)
- Phase: P10 · Size: L · Status: todo
- Depends: APP-PORT-01, UIK-21
- Files: apps/ojjySheets/

**Goal.** Grid with formulas (common 150 functions), formatting, charts,
sorting/filters, XLSX/CSV/ODS import/export.
**Done when.** A grades spreadsheet with charts round-trips through XLSX.
**Tests.** Formula engine host tests.
**Pitfalls.** Recalculation performance; circular references.
**Refs.** ECMA-376 SpreadsheetML.

### APP-OFFICE-03 — ojjySlides (presentations)
- Phase: P10 · Size: L · Status: todo
- Depends: APP-OFFICE-01, ANI-02
- Files: apps/ojjySlides/

**Goal.** Slides with themes, text/images/shapes, transitions and builds,
presenter view, PPTX import/export, PDF export.
**Done when.** A 10-slide deck presents full screen with transitions.
**Tests.** Fixtures.
**Pitfalls.** —
**Refs.** ECMA-376 PresentationML.
