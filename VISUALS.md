# ojjyOS Tahoe UI + Compositor Spec

This document defines the Tahoe Light visual language, glass materials, compositor architecture, and wallpaper plan for M2/M3.

Reference: Apple's macOS Tahoe Light wallpaper. It is Apple's picture, so it is not in the repo
(removed 2026-10-27; a local copy lives outside git in `~/Developer/ojjyOS-kernel/apple-reference/`).

## 1) Visual Spec (Tahoe-like)

### Palette (from the Tahoe Light reference)

- Tahoe Sky Top: `#0F84CC` - upper sky gradient, headers, spotlight tint.
- Tahoe Sky Mid: `#5EB0E1` - mid sky, window highlights.
- Horizon Cream: `#F0E0CA` - horizon glow, light glass haze.
- Glass Aqua: `#84CDC8` - glass sheen, highlight band.
- Surf Teal: `#11A5CA` - accent buttons, active indicators.
- Wave Blue: `#0F72C5` - mid-depth wave, dock accents.
- Deep Ocean: `#0B3F8C` - depth shadows, window outline.
- Midnight Navy: `#0A1C4A` - deepest depth, dark separators.

### Light mode rules

- Glass uses cool blue tint with warm horizon haze.
- Shadows are soft blue-black, never pure black.
- Text stays dark slate for contrast and calmness.
- Highlights follow the wave direction: top-left to bottom-right.

### Dark mode rules

- Base is midnight/ink blue with teal highlights.
- Glass shifts to steel-blue tint, with higher blur and lower opacity.
- Shadows deepen to black, but edges stay soft.
- Accent remains the same hue family for continuity.

### Typography plan (open fonts)

- UI sans: "Source Sans 3" (primary) or "Inter" as fallback.
- Display: "Space Grotesk" for large titles.
- Mono/system: "IBM Plex Mono" for developer text.

### Icon style rules

- Rounded square (squircle) base.
- Simple, single-weight glyphs with even stroke (1.6-2.0 px at 32 px).
- Minimal gradients; use glass tint + subtle inner glow.
- Use 2-3 tones from the palette for each icon.

## 2) Compositor Architecture

### Surface and layer tree

- Root layer: wallpaper
- Window layer: ordered list of windows (z-order)
- Overlay layer: dock, cursor, transient UI (spotlight, notifications)

### Damage tracking

- MVP: full-frame redraw at 30 fps.
- Phase 2: rectangle-based damage lists (per window + cursor).

### Z-order and clipping

- Windows stored in an array; last is topmost.
- On click, window is reinserted to end (front).
- Each window is clipped to a rounded rect mask.

### Rounded corners + shadows

- Rounded rect mask per window.
- Shadows drawn as 3 expanding rounded rect layers with decreasing alpha.
- Shadow color tied to theme tokens.

### Blur pipeline (software)

- Simple multi-sample blur from wallpaper buffer.
- Sample radius based on token: small/medium/large.
- Performance knobs:
  - sample step size (2-3 pixels)
  - update rate (30 fps)
  - lower blur radius for background windows

### Animation system

- Window open: fade + scale from 0.9 to 1.0 over ~250 ms.
- Window close: reverse (future).
- Dock bounce: short 5-frame cycle on open.
- Easing: cubic ease-out (fixed-point).
- Interruptible: new target state overrides current.

## 3) Code Deliverables (MVP)

- Compositor loop: `kernel/src/ui/compositor.c`
- Window management API: `kernel/src/ui/compositor.h`
- Blur implementation: `blur_sample()` in compositor
- Shadow + rounded clip: `draw_shadow()` and `point_in_rounded_rect()`
- Cursor rendering: `draw_cursor()` in compositor
- Theme tokens: `kernel/src/ui/theme.h` and `kernel/src/ui/theme.c`
- Demo glass panel: `Tahoe Glass` window in UI demo

## 4) Acceptance Tests

1. Start UI demo
```
ui
```
Expected: A glass window appears with blurred wallpaper behind it.

2. Window corners
Expected: Rounded corners are clearly visible.

3. Shadows
Expected: Soft shadow around the window.

4. Animation smoothness
Expected: Opening animation and dock bounce do not stutter excessively in VirtualBox.

## 5) Wallpapers (current, 2026-10-18)

- Built-in: "ojjyOS Waves Light/Dark", drawn procedurally at the real screen
  size by `kernel/src/ui/wallpaper.c` (`wallpaper_render_tahoe`).
- Official macOS Tahoe pictures (Apple's images, personal use only):
  `python3 tools/import_wallpapers.py --fetch` downloads them into git-ignored
  `wallpapers/local/`; each build converts them to baseline 4:4:4 JPEG
  2880x1800 + 320x200 thumbnails and writes a pack into the disk image at
  LBA 102400. Never commit or push them.
- The kernel decodes the chosen picture (`kernel/src/image/jpeg.c`), cover-
  scales it to the screen (`wallpaper_scale_cover`, crops instead of
  stretching) and builds the blur/glass caches from it.
- Selection: Settings > Wallpaper grid or Preview; saved as `wallpaper=` in
  `/Library/Preferences/system.conf`.
- The old `.raw` files in `/System/Wallpapers` (from `tools/mkwallpaper`) are
  no longer used by the desktop.
