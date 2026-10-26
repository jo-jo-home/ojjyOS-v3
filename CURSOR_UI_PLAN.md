# Cursor repair, then desktop cleanup

Created by Jonas Lee. Active request supersedes scheduler work, which has not
started. Keep custom kernel, blue Tahoe styling, accounts and saved documents.

## Investigation / baseline

- User reports pointer offset and lag, then asks for UI cleanup afterward.
- Reproduced using the delivered `7499306` ARM64 image on a private disk:
  QMP absolute (100,100) -> visible guest tip (512,582); (200,150) -> (712,682);
  returning to (100,100) after crossing the edge -> (0,0). PNG evidence:
  `build/cursor-before/`. Pixel template detects the actual rendered sprite.
- Driver drops first absolute axis and converts later positions into deltas;
  compositor then applies relative mouse-speed gain (default 2). Absolute
  positions are lost, and clamping makes the offset history-dependent.
- Every motion sets scene dirty; compositor redraws all glass/window content
  on a 33 ms scene clock and copies the full frame. Pointer-only motion should
  instead restore/draw a tiny overlay without rebuilding the scene.
- User's Cocoa QEMU is running on the live ARM image. Never stop it or replace
  its locked disk. Build/test a separate candidate and install only when unlocked.

## Plan

1. Keep absolute and relative pointer events distinct end-to-end. Normalize
   device min/max, clamp safely, publish coherent coordinates at SYN_REPORT;
   preserve click ordering and relative mouse-speed behavior.
2. Keep the backbuffer cursor-free. Restore old 16x24 cursor bounds from it,
   draw the new sprite directly to scanout, and redraw that overlay after full
   presents. Pointer updates must not wait for the 33 ms scene budget. Repaint
   scene only for affected hover state, dragging/resizing, or other real changes.
3. Regression tests: first move, corners, out-of-range/nonzero-min axes, repeated
   paths, coalescing/click ordering, PS/2 relative gain, clipped overlay restore,
   odd/padded pitches and framebuffer target transitions. Live pixel alignment
   and cursor movement latency, setup clicks and desktop interactions.
4. UI cleanup afterward: remove boxed glyph backgrounds, correct titlebar/layout
   collisions, improve contrast and calmer blue surfaces. Preserve attribution,
   ASCII branding, fonts and reduced-motion/transparency preferences. Verify
   setup, Finder and Terminal at the actual guest resolutions.
5. Run both builds, host/sanitized tests, private live input/storage/protected-app
   regression suites. Record evidence/limitations throughout, commit locally only.

## Status (historical log; see DONE below)

Investigation complete for coordinate drift. Implementation (steps 1-3) exists
in the working tree (driver absolute path, input.c absolute events, framebuffer
overlay, compositor dirty rules, tests/test_framebuffer.c, tests/qemu_pointer.py).
Session 2026-10-17 (Claude): host suite passes (incl. 11 framebuffer + new input
asserts); ARM candidate built to `build/cand/aarch64` with zero warnings.
Live run `build/cursor-after-1`: all 9 absolute positions/corners pixel-aligned
(offset bug fixed). Repaint check FAILED: setup screen did 9 full presents for
9 cursor moves. Diagnosed via frame-counter probe (`build/cursor-probe-1`): the
~2.5 s boot fade presents ~32 frames, then idle; moves add overlay updates only.
The test sampled its baseline mid-fade. Fixed test (`settle()` waits for an idle
scene). Run `build/cursor-after-2`: PASS - 9 positions/corners pixel-exact,
QMP->scanout 1.5-8 ms, 0 full frames for setup moves and 40 desktop moves,
setup clicks, login, Spotlight->Terminal `run hello`. Step 1-3 DONE (ARM).
Next: Intel build check, then step 4 UI cleanup.
Jonas asked: keep writing context into the project while working, and after
everything needed is done, hunt for further bugs/improvements.
Scope boundary is the repository because driver, compositor, framebuffer,
tests and runner/build safety are involved; no scheduler/syscall/storage-format
changes. Freeze owner for this investigation: `16ba0e336c4ea974f77f6c301acd36e1`.

### UI cleanup progress (step 4)

- Text: in backbuffer (UI) mode `fb_draw_char` now blends glyphs into the real
  surface instead of filling a guessed `bg` box (all callers paint their own
  backgrounds first). Console/direct scanout still uses solid bg.
  `fb_draw_string` decodes UTF-8: dashes -> '-', bullet/middle dot -> small
  disc, other multibyte -> single '?'. (Was "???" in setup/login footer.)
- Theme: `glass_aqua` was teal #84CDC8 -> calm sky blue #AAD2EE (dark #487CAC).
- Window chrome: 12px round traffic lights (grey when unfocused), title at +70,
  shared TL_X/hit_traffic_light() for draw + click.
- BUG FIXED: Finder back/forward/view buttons lived inside the titlebar but the
  titlebar click branch ran first, so they were unreachable. Now a unified
  toolbar after the title (finder_toolbar_x()) handled in the titlebar branch.
  Live verified: clicking the view toggle switches List -> Icons.
- Finder list columns are width-relative (no spill into Preview), long names
  ellipsized, sizes >5 digits shown in K; icon labels centred and truncated.
- Desktop disk icon no longer draws a second "ojjyHD" inside the icon.
- New live test: `tests/qemu_ui_tour.py` (private disk). Evidence build/ui-4.
- Menu titles flow after the app name via menu_title_x() (draw, dropdowns,
  hit-tests share it). Live: "About ojjyOS" no longer overlaps File; the File
  dropdown opens under its title.
- Settings > Appearance: full-width rows with pill switches; whole row
  clicks (APPEARANCE_ROW_Y shared). Live: 24-Hour Clock row toggles clock.
- About: wave banner moved right of the title block (was drawn over it).

## DONE (2026-10-17) - verification receipts

- Builds: both arches zero warnings (`build/install-{aarch64,x86_64}.log`).
- Host: 1078 C assertions (118 fs/ramfs, 79 input, 633 persist, 10 virtqueue,
  21 AML, 206 user, 11 framebuffer) + 9 Python, normal and ASan/UBSan
  (`build/cand/{host,sanitized}-tests.log`).
- Live pointer `build/cursor-after-2`, UI tour `build/ui-5`, full smoke with
  --protected on both arches `build/smoke-cursorui-{arm,intel}` PASS.
- Installed into build/<arch>/ojjyos3.img; data partitions hash-identical to
  the fresh backups (aarch64 ac520036..., x86_64 2910f9e4...).
- Limitations: Intel still uses relative PS/2 mouse (gain applies; host and
  guest pointers may diverge there - not changed). run-qemu.sh now hides the
  host cursor on ARM (one pointer). Manual Cocoa feel not measured by hand.
