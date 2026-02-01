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
  - Favorites: Applications, System, Users.
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
- Sidebar click sets path to `/Applications`, `/System`, `/Users`.
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
  - Appearance, Wallpaper, Dock & Menu Bar, Keyboard, Mouse/Trackpad, About.
- Main panel:
  - Appearance: Light/Dark toggle.
  - Wallpaper: Light/Dark thumbnails.
  - Dock & Menu: Dock size + magnification sliders.
  - Keyboard: Shortcuts toggle.
  - Mouse/Trackpad: Tracking speed slider.
  - About: version + author.

### Data model

- `SettingsState`:
  - `dark_mode`, `wifi_enabled`, `bluetooth_enabled`, `volume`, `brightness`, `dock_size`, `dock_magnify`, `mouse_speed`, `shortcuts_enabled`.

### Implementation

- Settings writes to `/Library/Preferences/system.conf` (RAMFS-backed).
- Appearance toggle updates compositor theme + wallpaper.
- Wallpaper page switches between Tahoe Light/Dark.

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
  - `help`, `ls`, `cd`, `pwd`, `cat`, `echo`, `touch`, `mkdir`, `open`, `clear`, `rm`, `mv`, `cp`.
- VFS integration for file and directory operations.
- Prompt format: `guest@ojjyos:<path> %`.
- Command history via Up/Down arrows.
- File ops: `rm`, `mv`, `cp`.

## TextEdit

### Layout

- Glass editor canvas with filename header.
- Status line at bottom (Open/Save status).
  - Dirty indicator + line/column display.

### Data model

- `TextEditState`:
  - `lines[]` text buffer.
  - `file_path` current file.
  - `status` string.

### Implementation

- Loads file contents via VFS.
- Save writes to RAMFS-backed `/Users/guest/Documents`.
- Super+O reopens last file opened from Finder.
- Super+S saves to RAMFS-backed file.
- Clipboard: Super+C/V/X copies/pastes/cuts current line.
- Editing: arrow navigation, line split/merge, insertion at cursor.
- Word navigation: Ctrl+Left/Right jumps by word.
- Selection: Shift+Arrow highlights selection.

## Menu Bar Integration

- Active app name updates on launch and focus.
- Menu bar remains global; app windows do not own menus yet.

## Acceptance Tests

- “Open Finder, browse folders, open a text file in TextEdit”
- “Change wallpaper in Settings and see update”
- “Dock shows running apps”
## Notes

### Layout

- Yellow note canvas, saved to `/Users/guest/Documents/Notes.txt`.

### Implementation

- Uses TextEdit buffer + save, preloads Notes.txt.
- Auto-saves on edits.

## Preview

### Layout

- Two wallpaper thumbnails with selection.

### Implementation

- Loads Tahoe Light/Dark thumbnails from `/System/Wallpapers`.
- Clicking a thumbnail updates the system wallpaper.

## Calendar

### Layout

- Header: month/year, navigation arrows, Today button, view switcher.
- Sidebar: calendars list + New Event.
- Month grid with weekday labels and event chips.
- Agenda pane with upcoming events.

### Data model

- `CalendarEvent`: date/time, title, location, all-day flag.
- `CalendarState`: current month/day, selected day, view mode, event list.

### Implementation

- Events stored in `/Users/guest/Documents/Calendar.txt` (RAMFS).
- Month grid uses RTC for current date and highlights today.
- Quick Add button creates a default event at 09:00 for the selected day.
- Time format uses Settings toggle (12/24).
- Edit mode: press `E` on selected event, type new title, Enter to save.
- Delete: press Delete to remove selected event.
- Edit fields: `E` title, `T` time, `L` location, `O` notes; Tab cycles fields.
- Invalid time shows inline error and keeps edit mode active.
