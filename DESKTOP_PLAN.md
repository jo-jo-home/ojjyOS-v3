# DESKTOP_PLAN.md — Track D: finish the Tahoe desktop

Track D of `PARALLEL_PLAN.md` (worktree `ojjyOS-kernel/ojjyOS-v3-ui`,
branch `desktop-polish`). Detailed per-step notes, function names and line
numbers stay in `HANDOFF_UI.md` §5; this file is the ordered checklist with
acceptance tests, adjusted for the parallel round (the Web app and all
networking/image-decoding work moved to Track N, `NET_WEB_PLAN.md`).

Order: D0 -> D1 ... -> D9. Commit + publish to `main` after each green step;
log it at the bottom.

## D0 — Hygiene (small, first)
- Remove iCloud duplicates that can ship in the OS image
  (`sysroot/System/Programs/* [0-9].elf`) — check `scripts/build-programs.sh`
  output names first, delete only the `* N.elf` copies, and make
  `scripts/mkfs.sh` refuse/skip files matching `* [0-9].*` when packing.
- Drop the unused 640x480 `.raw` wallpapers from `mkfs.sh`; Spotlight lists
  desktop pictures instead (choosing one sets it); "Change Wallpaper..." in
  the desktop menu opens Settings > Wallpaper.
- Acceptance: kernel image smaller (record `llvm-size` before/after), Spotlight
  "tahoe" shows pictures, all suites green.

## D1 — LG4 glass controls
Glass capsules for Finder toolbar buttons, segmented controls, push buttons,
switches; concentric inner panels (inner radius = outer − padding) across
Settings/Finder/About. Cheap variant (tint + specular rim, no backdrop blur)
for small controls. Expose the helpers in `ui/draw.h` so Track N can use them
in the browser toolbar. Acceptance: light + dark 2x screenshots; drag frame
≤ ~36 ms at 2x on a quiet host (`qemu_perf.py`).

## D2 — S1 notifications
Queue (icon, title, body, time) behind the existing `notifications_push()`
(keep its signature — Track N calls it) plus `notifications_post(icon,
title, body)`; glass banner top-right, slide in, auto-dismiss 4 s, click to
dismiss, Reduce Motion = fade; optional Notification Center list from the
clock. Posts: TextEdit/Notes saved, Wi-Fi on/off/connected, Trash emptied,
wallpaper changed, low memory (+ Track N's downloads/clock). Serial log
`[UI] Notification '<title>'`. New live test `tests/qemu_desktop_features.py`.

## D3 — S2 window snapping
Drag to left/right edge = half screen, top = maximise, glass preview while
dragging, double-click title = zoom, drag away restores the previous size,
animated (Reduce Motion respected). Test: drag via QMP, assert logged frame.

## D4 — S3 live resize
Windows re-layout continuously while resizing; measure frame time during a
resize drag at 2x under HVF; fix slow paths (layer cache invalidation,
text re-layout). Acceptance: numbers in the log, no visual tearing.

## D5 — S4 Terminal themes
Tahoe Dark (default), Light, Ocean, Classic Green wired to the existing
`termtheme=` setting; per-span colours in the Terminal line model; coloured
prompt (user@host accent, path blue), `ls` colours, `theme <name>` command,
blinking caret, scrollback indicator. Host test for the span model if split
into a testable unit.

## D6 — Apps, part 1: Finder, TextEdit, Notes
Finder: breadcrumb path bar, status bar (item count, free space), icon view
with labels, sidebar sections (Favorites incl. **Downloads**, Locations),
sort by column, glass toolbar. TextEdit: soft word wrap, word/char count,
save notification. Notes: sidebar list of `~/Notes/*.txt`, New Note,
autosave.

## D7 — Apps, part 2: Calendar, Calculator, Activity Monitor, About
Calendar: accent today circle, event chips, glass header. Calculator: key
press highlight, bigger display, history line. Activity Monitor: CPU /
frame-time sparkline, memory bar, zebra table, segmented tabs. About: hero
icon, version card, credits to Jonas Lee.

## D8 — Apps, part 3: Settings, Login, Preview
Settings: glass sidebar, restyled Network and Users pages, new General page
(name, version, Retina mode, uptime). Login: Liquid Glass card and buttons.
Preview: if Track N's `image.h` is on `main`, open `.png/.jpg` files from
Finder (double-click) with zoom-to-fit; otherwise skip and note it.

## D9 — Finish
Docs (`APPS.md`, `DESKTOP.md`, `README.md`, `VISUALS.md`, in-OS "What's new"
page in `scripts/mkfs.sh` including Track N's features), `TODO.md`,
`AGENTS.md` LATEST block. Full matrix on the branch rebased onto latest
`main`. If Track N is also finished: final delivery of installed images and
`build/ojjyOS.app` in the main folder only when no VM is running (else hand
that to whoever finishes last). Summary for Jonas.

## Log
(one entry per finished step: what, evidence directory, commit)
- D0 done: `tools/mkojfs.c` skips iCloud conflict copies (`name N.ext`, logs SKIP) so they can never ship in the OS image; the three unused 640x480 `.raw` wallpapers are gone (git rm + `mkfs.sh` deletes the generated folder) — kernel text 6.57 MB -> 2.88 MB, embedded FS 3.85 MB -> 166 KB. Spotlight: desktop pictures are results ("Desktop Picture", thumbnail; Enter/click applies), file results now OPEN (shared `open_path()` with Terminal `open`; before, choosing a file did nothing), home files labelled "Document"; fixed `search_index_init` returning early (skipping Documents/Desktop) when `/System/Wallpapers` was missing. Desktop menu gains "Change Wallpaper…" -> Settings > Wallpaper (`CTX_ITEMS` 8). `test_fs` nested-dir check now uses /System/Library/Icons. Evidence: build/d0-{arm,x86}.log 0 warnings, build/d0-tests.log + d0-san.log pass, build/d0-wp (qemu_wallpapers.py incl. new Spotlight + menu steps) PASS, build/d0-{hvf,tcg,x86} smoke PASS (other track may have been running QEMU concurrently).
- D1 done (LG4): glass controls — `draw_glass_control()` (translucent fill + specular rim + hairline, optional shadow; solid with Reduce Transparency), capsule `draw_push_button()` (primary = accent glass), Tahoe switch with wide glass knob, capsule segmented controls with raised glass pill, glass Finder toolbar (back/forward in one capsule, view pill, search capsule), Calendar nav, Preview bar, Settings > Users buttons, login/setup buttons. Exported `ui_glass_control()` / `ui_push_button()` in `ui/draw.h` for Track N's browser toolbar. Perf: drawing glass directly cost 0.48 ms/control (host CPU-time bench: shadow 0.20, stroke 0.11, rim 0.10) — so controls are now cached sprites: `gfx_sprite_extract()` renders once on black + white and recovers exact straight alpha, `gfx_blit_argb()` blends it (0.029 ms/control, same as the old flat controls; cache = 64 entries in a 8 MiB PMM pool, reset when full). Cached vs direct pixels: max diff 3 (host test + live screenshot diff). VM frame timings could not be trusted this session (host busy: Minecraft ~1 core + Track N), A/B runs varied 56-233 ms for the SAME build. Tests: test_gfx +3 checks (sprite alpha, sprite == direct within 3 levels, clip). Evidence: build/d1-{arm,x86}.log 0 warnings, d1-tests/d1-san pass, design tours at 2x build/d1-tour2x{,-dark-c}, smokes build/d1-{hvf,x86s,tcg3} PASS, appearance live test build/d1-appearance PASS. Found + fixed a test race (separate `hook:` commit): the smoke watchdog check could read the previous program's `ms=1` line under slow TCG.
