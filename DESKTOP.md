# ojjyOS Desktop Experience Spec (Tahoe)

This document defines the macOS Tahoe-like desktop behaviors built on the compositor.

## 1) Behavior Specifications (Detailed)

### Menu Bar

- Location: top edge, height 26 px, translucent glass material.
- Left zone:
  - Active app name (defaults to "Finder").
  - Future: menu items (File, Edit, View, Window, Help).
- Right zone:
  - Time (HH:MM, 24-hour).
  - Network placeholder text: "WiFi".
  - Volume placeholder text: "Vol".
- Interaction:
  - Click right status zone toggles Control Center.
  - Active app name updates on app launch or focus change.

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
  - Click icon to launch app.
  - Hover shows magnification.

### Spotlight

- Hotkey: Super + Space.
- Visual:
  - Centered bar, 520x58 px, rounded glass.
  - Results list below with 28 px row height.
- Search behavior:
  - App name substring + prefix match.
  - System files indexed from `/System/Wallpapers`.
  - Ranking: prefix (0), app substring (2), file prefix (1), file substring (3).
- Navigation:
  - Up/Down to select.
  - Enter to launch selected app.
  - Escape closes.

### Launchpad

- Hotkey: Super + L.
- Visual:
  - Full-screen blurred wallpaper with translucent veil.
  - App grid 4x3 with 56 px icons and labels.
- Interaction:
  - Click an icon to launch.
  - Click empty area to exit.

### Control Center

- Hotkey: Super + C.
- Visual:
  - Panel top-right, 260x220 px.
- Toggles:
  - Wi-Fi (stub)
  - Bluetooth (stub)
  - Appearance (Light/Dark)
- Sliders:
  - Volume (0-100)
  - Brightness (0-100)

### App Switcher (MVP)

- Hotkey: Super + Tab.
- Visual:
  - Centered tray with app icons.
- Interaction:
  - Each press cycles selection.
  - Enter launches selected app.
  - Escape closes.

### Mission Control (Phase 2)

- Hotkey: Super + M.
- Visual:
  - Full-screen glass overlay with window thumbnails.
- Interaction:
  - Click a thumbnail to focus.
  - Escape exits.

## 2) Event Handling & Shortcuts

- Super + Space: Toggle Spotlight.
- Super + L: Toggle Launchpad.
- Super + C: Toggle Control Center.
- Super + M: Toggle Mission Control (stub).
- Super + Tab: App switcher (cycles apps).
- Shortcuts can be disabled in Settings → Keyboard.
- Super + Q: Quit active app (marks app not running).
- Escape: Close active overlay; if none, return to shell.

Focus rules:
- Clicking a window brings it to front.
- Launching an app sets it active.
- Active app name reflects last launched or focused app.

## 3) System Services

- App Registry: enumerates `/Applications` bundles at startup, loads icons, tracks running state.
- Search Index: indexes app names and `/System/Wallpapers` files for Spotlight.
- Settings Service: stores dark mode, Wi-Fi, Bluetooth, volume, brightness.
- Notification Service: stores latest message (stub).

## 4) Code Map

- Compositor: `kernel/src/ui/compositor.c`
- Theme tokens: `kernel/src/ui/theme.c`
- Services: `kernel/src/ui/services.c`
- Entry integration: `kernel/src/main.c`

## 5) Acceptance Tests

- “Super+Space opens Spotlight”
- “Dock magnifies icons under cursor”
- “Launchpad shows installed apps with icons”
- “Menu bar updates with active app name”
