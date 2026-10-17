# ojjyOS Desktop Experience Spec (Tahoe)

This document defines the macOS Tahoe-like desktop behaviors built on the compositor.

## 1) Behavior Specifications (Detailed)

### First-Boot Account Setup Assistant & Tahoe Lock Screen

- Triggered automatically on first boot (`users_count() == 0`), when the session is locked (`!users_is_authenticated()`), or when **+ New Account** is clicked.
- **Setup Assistant:**
  - Centered glass card (440×340 px) over dimmed Tahoe wallpaper with live clock at top and `ojjyOS Tahoe • Created by Jonas Lee` footer.
  - 4 fields (`Full Name`, `Username`, `Password`, `Confirm`) with `Tab` field cycling, masked `*` password entry, **Create Account & Sign In** button, and **Cancel** button (when accounts already exist).
  - Hashes password with a random 32-bit salt + FIPS 180-4 SHA-256 (`kernel/src/users.c`), persists to `/Library/Accounts/users.db`, and provisions `/Users/<username>/Desktop` and `/Users/<username>/Documents`.
- **Lock / Login Screen:**
  - Centered glass card (360×280 px) with user avatar initials badge, Full Name, `@username • Admin` handle, masked password field, **Sign In** button (`Enter`), **Switch User** button (`Tab`), and **+ New Account** button.
  - Keyboard shortcut `Ctrl+Cmd+Q` locks the screen from anywhere on the desktop.

### Menu Bar & Dropdown Menus

- Location: top edge, height 28 px, translucent glass material.
- Left zone:
  - `ojjy` system menu pill (opens dropdown: `About ojjyOS`, `System Settings...`, `Activity Monitor`, `Lock Screen`, `Switch User / Log Out...`, `Restart...`, `Shut Down...`).
  - Active app name (bold/primary text).
  - Interactive dropdown menus:
    - `File`: `New Finder Window`, `New Text Document`, `Open Home Folder`, `Save Document (Cmd+S)`, `Close Window (Cmd+Q)`.
    - `Edit`: `Copy (Cmd+C)`, `Cut (Cmd+X)`, `Paste (Cmd+V)`, `Clear Selection`.
    - `View`: `Toggle Dark / Light`, `Toggle Transparency`, `Mission Control (Cmd+M)`, `Launchpad (Cmd+L)`.
    - `Window`: `Minimize Window`, `Zoom / Maximize`, `Calculator`, `Activity Monitor`.
- Right zone:
  - Active username pill (clicking opens `Settings → Users & Accounts`).
  - `Control` button + Time (`HH:MM`, 12/24-hour) — clicking toggles Control Center.

### Desktop Right-Click Context Menu

- Right-clicking anywhere on the desktop opens a 7-item Tahoe glass context menu:
  1. `New Folder on Desktop`
  2. `New Text Note`
  3. `Open Home in Finder`
  4. `Open Terminal Here`
  5. `Open Calculator`
  6. `Toggle Dark / Light Mode`
  7. `Lock Screen`

### Dock

- Location: bottom center, floating, rounded glass base.
- Icon layout:
  - Base size 36 px, max size 56 px when magnified.
  - Spacing: 12 px.
  - Running indicator: 8x3 px dot under icon.
- Hover magnification math:
  - Magnification based on distance to cursor center.
  - If within radius R=90 px:
    - size = base + (max - base) * (1 - dist^2 / R^2)
- Bounce animation:
  - 5-frame cycle: [0, -6, -10, -6, 0] px offset.
  - Triggered for ~600 ms on launch.
- Interaction:
  - Click icon to launch or unminimize app.
  - Hover shows magnification.

### Spotlight

- Hotkey: Super + Space.
- Visual:
  - Centered bar, 520x58 px, rounded glass.
  - Results list below with 28 px row height.
- Search behavior:
  - App name substring + prefix match.
  - Files indexed from `/System/Wallpapers` and the active user's `Documents` and `Desktop` folders.
- Navigation:
  - Up/Down to select.
  - Enter to launch selected app.
  - Escape closes.

### Launchpad

- Hotkey: Super + L.
- Visual:
  - Full-screen blurred wallpaper with translucent veil.
  - App grid 4x3 with 56 px icons and centered labels.
- Interaction:
  - Click an icon to launch.
  - Click empty area to exit.

### Control Center

- Hotkey: Super + C.
- Visual:
  - Panel top-right, 260x220 px.
- Toggles & Actions:
  - Wi-Fi (`(no radio)`)
  - Bluetooth (`(no radio)`)
  - Appearance (`Dark` / `Light`)
  - Transparency (`Reduced` / `Glass`)
  - **Restart** and **Shut Down** power buttons

### App Switcher

- Hotkey: Super + Tab.
- Visual:
  - Centered tray with up to 8 running app icons.
- Interaction:
  - Each press cycles selection; releasing `Super` (`Cmd`) or clicking an icon activates the app.
  - Escape closes.

### Mission Control

- Hotkey: Super + M.
- Visual:
  - Full-screen glass overlay with live window cards (app icon, title, dimensions, active/minimized badge).
- Interaction:
  - Click a card to unminimize and focus that window.
  - Escape exits.

## 2) Event Handling & Shortcuts

- Super + Space: Toggle Spotlight.
- Super + L: Toggle Launchpad.
- Super + C: Toggle Control Center.
- Super + M: Toggle Mission Control.
- Super + Tab: App switcher (cycles running apps; release Super to activate).
- Ctrl + Super + Q: Lock Screen immediately.
- Super + Q: Quit active app and close its window.
- Escape: Close active menu or overlay (`Shift+Escape` exits desktop to console).

## 3) System Services

- User Accounts Service (`kernel/src/users.c`): salted SHA-256 credentials, session authentication, per-user home provisioning, persisted in `/Library/Accounts/users.db`.
- App Registry: enumerates `/Applications` bundles at startup, loads icons, tracks running state.
- Search Index: indexes app names, `/System/Wallpapers`, and `/Users/<user>/Documents` + `Desktop` for Spotlight.
- Settings Service: stores dark mode, 24h time, reduced transparency, reduced motion, Dock size/magnification, mouse speed, shortcuts in `/Library/Preferences/system.conf`.

## 4) Code Map

- Compositor & Apps: `kernel/src/ui/compositor.c`
- User Accounts & SHA-256 Auth: `kernel/src/users.c`, `kernel/src/users.h`
- Theme tokens: `kernel/src/ui/theme.c`
- Services: `kernel/src/ui/services.c`
- Entry integration: `kernel/src/main.c`
