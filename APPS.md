# ojjyOS Core Apps Spec (Tahoe)

This document defines core app layouts, data models, and implementation details for Finder, Settings, Terminal, and TextEdit.

## Finder (File Manager)

### Layout

- Window: glass panel with title bar.
- Toolbar row (top of content):
  - Back/Forward buttons (left).
  - View toggle (list/icon).
  - Search box (right, filters live).
- Sidebar (left panel):
  - Favorites: Applications, System, Users, Home (`/Users/<user>`), Desktop (`/Users/<user>/Desktop`).
- Main content:
  - List view: columns (Name, Type, Size).
  - Icon view: 4-column grid with label below icons.
- App bundles:
  - `.app` directories shown as single items.
  - Clicking launches the app.
  - Back/Forward history supported.
  - Search filters current directory listing.

### Data model

- `FinderState`:
  - `path`: current directory.
  - `view_mode`: list or icon.
  - `entries[]`: name + type.
  - `selected`: selected row.

### Implementation

- Finder queries VFS via `vfs_opendir` + `vfs_readdir`.
- Sidebar click sets path to `/Applications`, `/System`, `/Users`, active user's `Home`, or active user's `Desktop`.
- List click:
  - Directory → navigates into it.
  - Bundle → loads manifest and launches app.
  - Text file → opens in TextEdit.
- Finder supports back/forward history and inline search filtering.
- Rename: press `R`, type new name, Enter to confirm, Esc to cancel.
- Delete: press Delete on selected item.
- File clipboard: Super+C/X/V to copy/cut/paste into current folder.
- Preview pane shows file metadata and up to 3 text lines.
- Drag move: click + drag file onto folder to move (RAMFS only).
- Drag indicator shows filename near cursor.
- Folder drop targets highlight while dragging.

## System Settings

### Layout

- Sidebar sections:
  - Appearance, Users & Accounts, Wallpaper, Dock & Menu Bar, Keyboard, Mouse/Trackpad, About.
- Main panel:
  - Appearance: Light/Dark toggle, 24-Hour Time toggle, Reduced Transparency toggle, Reduced Motion toggle.
  - Users & Accounts: Active user profile card (`Full Name (@username)` + home directory), Change Password form (`Current`, `New PW`, `Save PW` hashed with salted SHA-256), Registered Accounts list, and action buttons (`+ Add Account`, `Lock Screen`, `Switch User`).
  - Wallpaper: scrolling 3-column grid of every desktop picture (the two built-in "ojjyOS Waves" plus the official Tahoe pictures when installed), names, selection ring, scroll indicator.
  - Dock & Menu: Dock size + magnification sliders.
  - Keyboard: Shortcuts toggle.
  - Mouse/Trackpad: Tracking speed slider.
  - About: version + author (`Created by Jonas Lee`).

### Data model

- `SettingsState`:
  - `dark_mode`, `wifi_enabled`, `bluetooth_enabled`, `volume`, `brightness`, `dock_size`, `dock_magnify`, `mouse_speed`, `shortcuts_enabled`, `reduced_transparency`, `reduced_motion`, `wallpaper` (picture name, saved as `wallpaper=<name>`; system-wide).

### Implementation

- Settings writes to `/Library/Preferences/system.conf` (RAMFS-backed + persisted to disk).
- Users & Accounts reads/writes `/Library/Accounts/users.db` via `users.c`.
- Appearance toggle updates the compositor theme; a paired picture follows (Tahoe Light <-> Tahoe Dark, Beach Day <-> Night, Dawn <-> Dusk, Waves Light <-> Dark). All Light/Dark switches go through `toggle_appearance()`.
- Clicking a picture applies it immediately (`wallpaper_choose()`); Light/Dark pictures also switch the appearance to match. The window is 700x460.
- Planned (see `HANDOFF_UI.md` step C): Light/Dark/Auto, accent colour, Liquid Glass Clear/Tinted, menu bar style, window corners, Terminal theme.

## Terminal

### Layout

- Dark glass panel with monospace text.
- Output history and input prompt at bottom.

### Data model

- `TerminalState`:
  - `lines[]` output ring.
  - `input` current line.
  - `cwd` current working directory.

### Implementation

- Commands:
  - `help`, `ls`, `cd`, `pwd`, `cat`, `echo`, `touch`, `mkdir`, `open`, `clear`, `rm`, `mv`, `cp`, `sync`, `shutdown`, `reboot`, `whoami`, `users`, `adduser`, `passwd`, `su`, `lock`, `logout`.
- VFS integration for file and directory operations (`~` and `~/...` resolve to the active user's home directory).
- Prompt format: `<username>@ojjyos:<path> %`.
- Command history via Up/Down arrows.
- File ops: `rm`, `mv`, `cp`.

## TextEdit

### Layout

- Glass editor canvas with filename header.
- Status line at bottom (Open/Save status).
  - Dirty indicator + line/column display + word and character count
    (falls back to just the counts when the line would not fit).

### Data model

- `TextEditState` (in `kernel/src/ui/textedit.c/h`):
  - `lines[]` text buffer (64 lines x up to 255 characters).
  - `file_path` current file.
  - `status` string, `goal_col` remembered column for Up/Down,
    `view_cols`/`view_rows` the view size of the last draw.

### Implementation

- Soft word wrap: the document is laid out into visual rows of at most
  the window width (broken after the last space inside the window;
  words wider than the view hard-break). Up/Down/PageUp/PageDown move
  over the rows the eye sees, keeping the goal column; Home/End go to
  the line's start/end. Wrapping adds no bytes to the file.
- Loads file contents via VFS. Opening and saving keeps the bytes (the
  newline that ends the last line does not add an empty line); a file
  longer than 64 lines, with lines over 255 columns, or with NUL bytes,
  is shown cut down and opens read-only (edits blocked, Save refused) so
  it is never cut.
- Save writes to RAMFS-backed `/Users/<user>/Documents`.
- Super+O reopens last file opened from Finder.
- Super+S saves; an explicit save pushes a "Saved <name>" notification
  and logs the byte, word and character counts (Notes autosave stays
  quiet).
- Clipboard: Super+C/V/X copies/pastes/cuts current line.
- Editing: arrow navigation, line split/merge (a merge that would not
  fit is refused, never truncated), insertion at cursor, Backspace
  deletes the character left of the caret anywhere in the line.
- Word navigation: Ctrl+Left/Right jumps by word.
- Selection: Shift+Arrow highlights from an anchor (both directions).

## Notes

### Layout

- Sidebar (left): "Notes" + count, a "+ New Note" capsule button, then one
  row per note with its title (file name without `.txt`) and first line;
  the open note is highlighted in Notes yellow. The wheel scrolls the list.
- Editor (right): the shared TextEdit page with the yellow notepad header
  (note title, "Edited" or "Read-only" pill), ruled paper and status line.

### Data model

- Every note is a plain-text file in `/Users/<user>/Notes/` (`*.txt`, any
  case; hidden files skipped), listed by title because RAMFS keeps no
  timestamps. Up to 48 notes are listed ("More notes in Finder" beyond).
- `kernel/src/ui/notes.c/h` (no drawing, host-tested by
  `tests/test_notes.c`) lists the folder, builds previews, picks
  "New Note.txt", "New Note 2.txt", ... and decides when to autosave.
- First launch makes `~/Notes` and copies the old single
  `~/Documents/Notes.txt` into it (the original stays where it was).

### Implementation

- "+ New Note" or Cmd+N makes and opens a note; typing with no note open
  starts one.
- Autosave: one write once typing has paused for 1 second, and right away
  when switching notes, making a new one, closing the window, locking the
  screen, Cmd+S, and before every save to disk (sync, shutdown, restart).
- A file longer than the editor (64 lines x 79 columns) opens read-only
  and is never rewritten.
- Spotlight finds notes (shown as "Note") and opens them in Notes.
- Serial receipts: `[NOTES] Opened|Saved '<name>' <bytes> bytes`,
  `[NOTES] New note '<name>'` (`tests/qemu_notes.py` checks them).

## Preview

### Layout

- Desktop picture viewer: the picture at full resolution (16:10, rounded), ‹ › buttons, its name, and "Set as Desktop Picture" (shows a check mark for the current one).

### Implementation

- Opens on the current desktop picture; pack pictures are decoded from the disk pack (`wallpaper_decode`), the built-in waves are rendered at the viewer's size.
- "Set as Desktop Picture" calls `wallpaper_choose()` (same as Settings).
- Not yet: opening arbitrary image files from Finder (the JPEG decoder exists).

## Calendar

### Layout

- Glass header: month/year, navigation arrows, Today button, view switcher.
- Sidebar: calendars list + New Event.
- Month grid with weekday labels and event chips.
- Agenda pane with upcoming events.

### Data model

- `CalendarEvent`: date/time, title, location, all-day flag.
- `CalendarState`: current month/day, selected day, view mode, event list.

### Implementation

- Events stored in `/Users/<user>/Documents/Calendar.txt` (RAMFS).
- Month grid highlights the full current UTC date, not the same day number in another month/year; event chips fit the cell height.
- Quick Add button creates a default event at 09:00 for the selected day.
- Time format uses Settings toggle (12/24).
- Edit mode: press `E` on selected event, type new title, Enter to save.
- Delete: press Delete to remove selected event.
- Edit fields: `E` title, `T` time, `L` location, `O` notes; Tab cycles fields.
- Invalid time shows inline error and keeps edit mode active.

## Calculator (`Calculator.app`)

### Layout

- 72-pixel dark midnight readout with a right-aligned value and the latest equation above it.
- 5×4 rounded glass keypad (`C`, `+/-`, `%`, `/`, `7..9`, `*`, `4..6`, `-`, `1..3`, `+`, `0`, `.`, `BS`, `=`).
- Accepted keyboard/mouse keys highlight for 180 ms, then expire without continued repaints.

### Implementation

- Supports both mouse clicks on the keypad grid and direct keyboard entry (`0..9`, `+`, `-`, `*`, `/`, `Enter`/`=`, `Backspace`, `C`/`Esc`).
- Fixed-point arithmetic (six decimal places); division-by-zero and overflow show an error, and typing a digit starts a fresh calculation.
- `ui/calculator.c/h` owns input/history and shared draw/hit geometry; `ui/calcmath.c` owns arithmetic.

## Activity Monitor (`Monitor.app`)

### Layout

- **Processes / Memory / Frames:** Working segmented tabs, refreshed once per second while open and not minimized; no background refresh while locked.
- **Processes:** Zebra rows for PID, name, owner, state, task ticks and recorded memory. Ticks are bookkeeping, **not CPU utilization**; rows that fit are shown without scrolling.
- **Memory:** Measured RAM, managed capacity, used/free managed RAM and firmware/kernel reservations, plus bounded used/managed and kernel-heap bars and virtual-NIC traffic. Enabled high-RAM free pages count alongside LOW pages; reservations are not shown as allocations. GB labels round measured binary capacity to the nearest tenth; exact MB counters (binary MiB, rounded down) match the PMM serial receipt.
- **Frames:** Last 48 actual draw-and-present durations in milliseconds, sampled only while open and not minimized. Includes its own rendering cost; **not FPS or CPU percent**. The 16 ms guide is not a performance guarantee.
- **Footer:** Total tasks, open windows and uptime.

## About (`About.app`)

- Hero icon, glass Version 3.1.0 / Tahoe card, readable **Created by Jonas Lee** credit, original-kernel description and measured memory (for example `8 GB memory (8191 MB RAM)`, from PMM rather than the requested launch setting).
- Calendar, Calculator, Monitor and About enforce content-aware minimum window sizes so resizing cannot hide their controls.

---

## Web (`Web.app`, "ojjyOS Web")

A Chrome-style browser written from scratch for this kernel (it cannot run
real Chrome: see `BROWSER_CHROME_PLAN.md` section 0).

### Layout
- Tab strip in the title row next to the traffic lights (up to 12 tabs,
  favicon letter, title, close button, `+`).
- Toolbar: back, forward, reload/stop, home, omnibox (lock or "Not secure",
  suggestions from history/bookmarks/search), bookmark star, menu.
- Optional bookmarks bar; find bar; status bubble for hovered links;
  JavaScript info bar offering lighter versions of script-only sites.
- Built-in pages: `ojjy:newtab`, `ojjy:history`, `ojjy:bookmarks`,
  `ojjy:downloads`, `ojjy:settings`, `view-source:`.

### Data model
- Per user under `~/Library/Web/`: bookmarks, history, cookies, settings
  (search engine, home page, bookmarks bar).
- Downloads are saved to `~/Downloads`.

### Implementation
- `ui/browser.c` (tabs, fetching, images, input), `ui/browser_ui.c`
  (drawing), `ui/browser_profile.c` (stored data), `ui/browser_pages.c`
  (built-in pages).
- Engine in `kernel/src/web/`: `dom.c` (tolerant HTML parser), `css.c`
  (selectors, cascade incl. `@layer`, `var()`, `calc()`, media queries),
  `layout.c` (block/inline/flex/grid/table/float/absolute layout to a
  display list), `paint.c` (shared painter), `http.c` (HTTP/1.1 with
  gzip/deflate, cookies, redirects, POST).
- Images in `kernel/src/image/`: PNG, GIF, JPEG (baseline + progressive),
  WebP (lossy, lossless, alpha) and SVG (`svg.c`, integer-only rasterizer).
- Tests: `tests/test_web.c` (host, incl. fuzzing), `tests/web_render.c`
  (renders saved pages to images), `tests/qemu_web_tour.py` (live sites).
- HTTPS identity (NET-02): `crypto/x509.c` + `x509_chain.c` check every
  certificate chain against the built-in roots (`crypto/roots_data.c`),
  the host name and dates; `net/tls.c` verifies CertificateVerify. A
  failure shows "This connection is not private" (reason, Go Back, Visit
  anyway = per-host exception until restart); the address bar shows a lock
  (verified) or a crossed lock + "Not secure" (warning/exception); the lock
  opens page info with the certificate, issuer and root. Terminal `curl`
  refuses bad certificates unless `-k`. Live test: `tests/qemu_https.py`.
- No JavaScript engine yet; TLS 1.2-only sites need NET-03; revocation
  (OCSP/CRL) is not checked.
