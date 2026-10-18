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
