# Renderer, Look & Animation Plan (2026-10-18)

Jonas, after testing the virtio-gpu full-screen build:

> "if i open some things the windows slowly get bigger then stop but when they
> are getting bigger it gets very laggy ... icons and just everything in general
> is very pixely and needs to be smooth so i cant see pixels. focus on making the
> renderer ... i need better looking icons and ui ... u need animations ... the
> font is ugly, the background is pixelated, everything is ugly"
> then: "plan it out in detail first then implement one by one"

This file is the plan **and** the live log. Each phase ends with verification
and a local commit. Nothing is pushed. Jonas's own VM may be running from
`build/aarch64/ojjyos3.img`: never stop it, never overwrite that locked disk.
Development images are built with `OJJY_BUILD_DIR=build/dev-aarch64` and all live
tests use private copies; the real image is reinstalled only after his VM is off.

## Diagnosis (measured / read from code)

| Symptom | Root cause |
|---|---|
| Opening a window "slowly gets bigger, very laggy" | Open animation advances a fixed `+40/1000` per *frame* (25 frames). Each frame redraws the whole scene; every window costs ~5 per-pixel passes through `fb_get_pixel`/`fb_put_pixel` + `point_in_rounded_rect` (3 shadow layers each the full window area, glass fill sampling the blur cache, titlebar). Slow frames => the animation stretches over seconds. |
| Everything pixelated | No anti-aliasing anywhere: rounded corners, circles, wave edges are hard 0/1 coverage; cursor is a 12x8 bitmap doubled; app icons are **32x32** drawn up to 48 px with nearest-neighbour; wallpapers are 1024-wide `.raw` scaled with nearest-neighbour; then QEMU scales 1440x900 up again to the Retina screen. |
| Ugly font | One 8x16 monospace cell font (JetBrains Mono) used for *all* UI text; no proportional UI face, no sizes/weights (bold = smeared). |
| Emulation is slow overall | TCG (CPU emulation). Tried HVF (hardware virtualization): GICv2 is unsupported by HVF; with GICv3 the kernel boots through timer + virtio-gpu, then QEMU asserts `isv` = an MMIO access whose instruction has no syndrome (e.g. `ldp/stp`, pre/post-index) that HVF cannot emulate. Fixable in our code. |

## Phases (implemented in this order, one at a time)

### Phase 1 — Make it fast (fixes the lag)
1. **Frame timing receipts**: count frames and per-frame milliseconds
   (`[PERF]` serial lines on demand + a `perf` Terminal command); new live test
   `tests/qemu_perf.py` measures window-open animation duration and frame times
   before/after every phase.
2. **Fast pixel paths**: row-span rounded-rect fill (corner inset computed once
   per row, direct pointer writes, no per-pixel function calls); glass fill
   copies/blends spans from the blur cache; one soft precomputed shadow instead
   of three full-window blends (only the ring outside the window is touched).
3. **Time-based animations**: every animation runs on elapsed milliseconds with
   easing (ease-out cubic / spring-ish overshoot), ~220 ms opens, so a slow frame
   skips ahead instead of stretching the animation.
4. **Cheap open/close animation**: render the window once into an offscreen
   layer, then animate only that layer (scale + fade) over the static scene.
5. **Damage rectangles**: recompose and flush only what changed (cursor already
   does this); idle frames stay at 0 work.
6. **HVF (hardware virtualization)**: find each `isv` MMIO access (GICv3,
   virtio, PL011/PL031 …) and force single-register volatile accessors; GICv3
   path verified; `run-qemu.sh` uses `-accel hvf -cpu host gic-version=3` when
   it works, TCG otherwise (`OJJY_ACCEL=tcg`). Expected 10x+ faster everything.

### Phase 2 — Smooth rendering (no visible pixels)
1. Anti-aliased primitives: coverage-based rounded rects, circles (traffic
   lights, switches), strokes, gradients with ordered dithering (no banding).
2. Soft Gaussian-like shadows (precomputed falloff table, blended per span).
3. New wallpaper: rendered at the exact screen size at boot (smooth gradient
   sky + anti-aliased layered waves + dithering, Tahoe blue), cached; image
   wallpapers scaled with bilinear filtering instead of nearest-neighbour.
4. Anti-aliased macOS-style cursor (alpha arrow with soft shadow), rendered
   from a 4x supersampled shape at build time.
5. **HiDPI**: if HVF makes it fast enough, render at 2x (2880x1800 guest,
   UI scale 2) so text and edges are Retina-sharp; otherwise stay at 1x.

### Phase 3 — Real fonts
1. Add **Inter** (SIL OFL 1.1, by Rasmus Andersson) for UI text; keep
   JetBrains Mono for Terminal, code and `<pre>`.
2. `tools/mkfont.py` grows a proportional atlas generator: sizes 11/13/15/20/28
   (and x2 for HiDPI), Regular + Semibold, 4x supersampled coverage, per-glyph
   advance, bearing and simple kerning pairs; gamma-correct blending in kernel.
3. Text API: `text_draw(x, y, str, style, color)`, `text_width(...)`,
   ellipsis truncation; migrate menu bar, titles, dock labels, buttons, Finder,
   Settings, Spotlight, Control Center, login screen, desktop icon labels.

### Phase 4 — New icons and UI redesign (keep the blue Tahoe theme)
1. New icon set generated at build time (`tools/mkicons.py`, Pillow): rounded
   "squircle" app icons with gradients, highlights and soft shadows (Tahoe
   style), 128 px masters downsampled to the sizes used (mip levels, bilinear
   sampling) for all 11 apps + folder/file/disk/trash.
2. UI pass: translucent menu bar with proper spacing; windows with 12 px AA
   corners, unified title bars, refined traffic lights with hover glyphs;
   glass dock with running dots; Finder sidebar with icons and selection pills;
   consistent buttons, text fields, switches, list rows; Spotlight, Control
   Center, context menus and login screen restyled.

### Phase 5 — Animations
Easing library + animation driver; window open (scale 0.92→1 + fade) and close
(scale/fade out); minimize to the dock and restore; dock magnification on hover
and launch bounce; menus/context menus fade + slide; Control Center, Spotlight
and Launchpad spring in; Mission Control zoom; hover/press feedback on buttons;
login → desktop cross-fade. "Reduce Motion" keeps turning them off.

### Phase 6 — "Native" Mac app instead of QEMU (asked 2026-10-18)
Jonas: "can we make the vm work natively and smooth instead of using qemu too".
- CPU: already native via HVF (Phase 1.6); QEMU now only supplies devices and
  the window.
- Next step up: a small Swift `ojjyOS.app` on Apple's Virtualization.framework
  (same hypervisor, native Retina window/full screen, no QEMU). Kernel work it
  needs: PCIe ECAM enumeration + virtio-pci transport (VZ devices are PCI, not
  virtio-mmio), virtio-gpu over PCI, USB xHCI + HID keyboard/pointer (VZ has no
  virtio-input), virtio-console instead of PL011, no PL031 (time from the host
  or ACPI), ACPI/DT-driven memory map, virtual timer. Do after Phases 2-5.
- Bare metal on the Mac (no macOS): rejected — needs Asahi's m1n1 chain,
  reduced boot security, and Apple AIC/DART/DCP/SPI-HID/NVMe-ANS drivers.

### Verification for every phase
Host suite (+ sanitizers when C helpers change), both architecture builds with
0 warnings, `qemu_smoke.py` (ARM virtio-gpu + ramfb fallback, x86), app and
browser live tests, `qemu_perf.py` numbers, screenshots reviewed by eye.
Results are appended to the log below.

## Log

- 2026-10-18: plan written; diagnosis above. HVF probe: GICv2 unsupported,
  GICv3 reaches virtio-gpu then `assert(isv)` in QEMU `hvf_handle_exception`.
- Phase 1.1 done: `[PERF]` serial receipts + `tests/qemu_perf.py`. Baseline
  (TCG, 1440x900, `build/perf/p0-baseline`): window opens 3.1-8.2 s
  (25 frames, slower with every open window), drag 345 ms/frame (~3 fps).
- Phase 1.2 done: `kernel/src/gfx.{h,c}` (span renderer, AA rounded rects,
  SDF soft shadow, dithered gradients, bilinear/area-average RGBA scaling,
  two-multiply blend) + `tests/test_gfx.c` (24 checks, ASan/UBSan clean).
  Compositor rounded rects/shadows/glass use it; glass is a cached pre-tinted
  blur. Result (`build/perf/p1-gfx`): opens 0.8 s (now capped by the 30 fps
  frame budget), drag **31 ms/frame** (11x faster).
- Phase 1.3 done: animations advance by elapsed time with ease-out cubic
  (window open 240 ms, overlays 200 ms), frame cap 30 -> 60 fps. Opens now a
  constant ~230 ms (`build/perf/p1-time2`); TCG still only fits 8-12 frames
  in that time (~30 ms/frame), which HVF should fix.
- Phase 1.6 done — **HVF works**. Root causes: (1) MMIO through volatile
  pointers let the compiler emit post-increment/pair forms with ISV=0, which
  HVF can't emulate (`assert(isv)`); all ARM drivers now use
  `arch/aarch64/mmio.h` (single LDR/STR asm). (2) Under HVF the CPU's
  ID_AA64PFR0.GIC reads 0, so the GIC driver picked GICv2; it now treats an
  Apple core (MIDR implementer 0x61) as GICv3 (probing GICD PIDR2 at 0xFFE8
  faults on GICv2 — tried, reverted). `run-qemu.sh` defaults to HVF
  (`OJJY_ACCEL=tcg` to force emulation); tests take `OJJY_ACCEL=hvf`.
  HVF receipts (`build/perf/p1-hvf2`): frame avg **7 ms** (TCG 31 ms), opens
  16 frames/240 ms (60 fps). `build/hvf-live/`: smoke --protected (EL0 fault
  classes, timer limits), browser (local HTTPS 21 ms vs 641 ms), apps check,
  network UI all PASS under HVF; ARM TCG smoke and x86 smoke also PASS.
- Phases 1.4 (layer-based open animation) and 1.5 (damage rectangles) are
  folded into Phase 5 / deferred: at 7 ms per full frame they are no longer
  needed to remove the lag.
- Phase 2 (smooth rendering) done, except 2x HiDPI which moves after fonts:
  * AA primitives via gfx.c (rounded rects, circles, strokes, dithered
    gradients, soft SDF shadows).
  * `kernel/src/ui/wallpaper.c`: procedural Tahoe light/dark wallpaper drawn
    at the real screen size (sky gradient, sun glow, 4 AA wave sheets with rim
    light and cast shadows, ordered dither). The old `.raw` Tahoe files were
    640x480 stretched with nearest-neighbour (the visible stair-steps). Other
    image wallpapers now use bilinear + dither. Host preview:
    `tools/preview/wallpaper_preview.c`.
  * AA pointer: `tools/mkcursor.py` -> `kernel/src/cursor_image.c` (22x30
    RGBA, 8x supersampled arrow with outline and soft shadow).
  * Receipts: `build/perf/p2-wall` (HVF 7 ms frames), `build/p2-live/`
    smoke (HVF) + pointer test (all corners exact, ~2.5 ms QMP-to-scanout, no
    full repaint on motion). Pointer test now reads the cursor image source.
- Phase 2.5 / 3a done — HiDPI + font infrastructure:
  * Framebuffer stores PHYSICAL pixels, API stays LOGICAL; scale 2 on
    >= 2560x1440 (`fb_ui_scale()`). gfx.c scales all inputs; 2x pointer
    (`cursor_rgba2x`), 16x32 JetBrains Mono (`tools/mkfont2x.py` ->
    `font2x.c`), wallpaper/blur/glass caches at physical size, launchpad
    frosted glass via gfx. Backbuffer now PMM-allocated (BSS 26 -> 17 MB).
  * Inter (SIL OFL, `fonts/Inter-*.ttf`, licence `fonts/Inter-OFL-LICENSE.txt`)
    -> `tools/mkuifont.py` -> `kernel/src/uifont_data.c` (9 styles x 1x/2x,
    660 KB). API `kernel/src/uifont.{h,c}`: text_draw/_fit/_width, UTF-8,
    1/64 px advances, ellipsis. Preview: `tools/preview/text_preview.c`.
  * run-qemu.sh: HVF -> 2880x1800 (2x), TCG -> 1440x900.
    `build/perf/p3-hidpi`: 2x frames avg 23 ms (drag ~43 fps), opens 240 ms.
  * Bug found by HVF: virtio-blk waited a fixed spin count, which HVF's fast
    CPU exhausted before QEMU finished a write ("Timeout at sector 69226",
    save failed). Disk + GPU waits now use counter-based ms deadlines
    (`virtio_now_ms`). 3 parallel HVF smoke --protected runs PASS; TCG smoke,
    pointer, 2x apps check, x86 smoke PASS (`build/p3-live/`).
  * Next: move UI text to Inter component by component (Phase 3b), together
    with the Phase 4 redesign of each component.
- Phase 4.1 done: new icon set `tools/mkicons.py` -> `kernel/src/ui/icons_data.c`
  (15 icons: 11 apps + folder/file/disk/trash; vector shapes at 4x, squircle,
  gradients, gloss, soft shadows; 128x128 premultiplied-Lanczos). Drawn via
  `gfx_draw_rgba_scaled` (area-average shrink, bilinear grow) so 1x and 2x
  are both smooth; apps map by bundle id, bundle .raw icons remain a fallback.
  `build/perf/p4-icons` (2x HVF): drag 24 ms/frame.
- Phase 4.2a done — desktop chrome redesigned with Inter (`build/design/t2`,
  tour script `tests/qemu_design_tour.py`, works at 1x/2x):
  menu bar (blurred glass, bold app name, measured title layout shared with
  hit-testing, real AA Wi-Fi fan via new `gfx_arc_up`, Control Center glyph,
  "Sun Oct 18  11:58 AM" clock); dropdown/context menus (glass, hover pills,
  right-aligned shortcut column with the command symbol); windows (title-bar
  sheen, centred titles with ellipsis, traffic lights with rims and hover
  symbols, hairline edge, focused/unfocused shadows, no pixel resize grip);
  dock (magnification that spreads neighbours, shared click layout, running
  dots, name tooltip, eased launch bounce); desktop icons (white shadowed
  labels, selection pills); login/setup (frosted wallpaper, date + 72 px
  clock, glass card, focus-ring fields, gradient buttons, round avatar; all
  click rects unchanged); Control Center, Spotlight (icons in results),
  Mission Control, Launchpad labels, Cmd+Tab switcher.
  Bugs found: UiFontFace metrics were int8 (2x clock ascent 140 wrapped);
  first traffic-light hover check forced one full repaint (pointer test).
  Live: TCG + HVF(--protected) smoke, pointer, apps, browser, network UI PASS.
- Phase 4.2b done — app contents: Finder (sidebar icons, list rows with file
  icons, accent selection, Kind/Size columns with KB/MB, big-icon preview,
  drawn chevrons, search field with glyph), Settings (coloured page glyphs,
  headline titles, iOS switches), Calculator (gradient LCD, large readout,
  orange operators, ÷ × − ± ⌫ glyphs added to the font), Calendar, Activity
  Monitor, About (real icon + type hierarchy), Preview, TextEdit chrome,
  browser toolbar (drawn back/forward/reload/home, Safari-style centred host
  with lock, focus ring, Inter status line). 101 app labels converted to
  Inter mechanically (geometry unchanged); Terminal/TextEdit text and HTML
  page text stay monospace for now (page text needs the HTML layout engine to
  measure Inter: tracked as Phase 4.3).
  Bug fixed: the 9th app silently failed to open (8-window cap) while the
  menu bar still switched to it; cap is now 12 (> 11 apps) and the menu bar
  only switches after the window exists.
  Live: TCG + HVF(--protected) smoke, pointer, apps, browser, files PASS;
  tour `build/design/t4`.
- Phase 5 done — animations (`tests/qemu_anim_check.py`, `build/anim/a1`):
  * Layer system: `fb_redirect()` draws a window into an off-screen layer;
    `gfx_blit_scaled()` (bilinear, AA rounded mask, alpha; host-tested) puts
    it back scaled/faded. Windows always lay out at real size now.
  * Window open: 94% -> 100%, rises 8 px, fades in (ease-out, 240 ms).
  * Close: "ghost" snapshot shrinks and fades (190 ms) after the real window
    is already destroyed. Minimise: ghost flies into the app's Dock icon
    (ease-in-out, 340 ms); restore replays the open animation.
  * Login -> desktop: full-screen ghost of the last login frame fades out.
  * Menus / context menu: fade + 6 px slide (140 ms). Dock magnification
    eases in/out (~150/200 ms). Launchpad icons enter staggered (80% -> 100%).
  * Reduce Motion turns all of it off. Live: TCG + HVF(--protected) smoke,
    pointer, apps, browser, files, network UI PASS; 2x perf drag 29 ms avg
    with 7 VMs running in parallel.
