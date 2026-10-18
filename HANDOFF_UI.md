# HANDOFF_UI.md — Liquid Glass, wallpapers, customisation and app polish

Written 2026-10-18 when Jonas said: "stop. write down context into project in
detail, then write down what else still needs to be done". The job is NOT
finished. This file is the complete brief for the AI that continues it. Read
`AGENTS.md` (project rules) first, then this file, then
`LIQUID_GLASS_APPS_PLAN.md` (the plan and its log). The project-wide to-do
list is `TODO.md`.

---

## 1. What Jonas asked for (his words, in order)

1. "i want liquid glass like tahoe and also improve all the apps. make a
   detailed plan for all then implement them." Earlier in the same thread he
   also said yes to: **notifications, window snapping, live window resizing,
   Terminal themes**.
2. "also add the official tahoe backgrounds and customizability for
   themes/ui/background in settings" with two links:
   - https://basicappleguy.com/basicappleblog/tahoe-aerial-wallpapers
   - https://512pixels.net/2025/06/macos-tahoe-beta-wallpaper/
3. "stop. write down context ... should be easy for other ais to see what
   needs to be done if i tell them to do what is needed in the project."

His working style: detailed plan first, then execute everything end to end
without check-ins, then a plain-English summary of exactly what changed.
Never push. Commit locally as Jonas (see §4).

---

## 2. Where the work lives

| Thing | Value |
|---|---|
| Worktree for this job | `~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3-ui` |
| Branch | `ui-polish-2` |
| `main` | fast-forwarded to `ui-polish-2` after every finished step (currently equal) |
| Main folder | `.../ojjyOS-kernel/ojjyOS-v3` — checked out on **`native-vm`** by ANOTHER AI job (native Mac app). Do not edit, build in, commit to, or reset that folder or branch. |

Why a worktree: the native-VM job has uncommitted work in the main folder.
Keep working in `ojjyOS-v3-ui`. To publish a finished step to `main` without
checking `main` out anywhere, run inside the worktree:

```bash
git fetch -q . ui-polish-2:main      # fast-forward only; fails if main moved
```

If that ever fails because `main` gained commits, rebase `ui-polish-2` onto
`main` (or merge `main` in), rebuild and retest, then fast-forward again.

Overlap with the native-VM job: it edits `kernel/src/arch/aarch64/*`,
`kernel/Makefile`, `kernel/src/serial.h`, `tests/test_input.c`,
`tests/test_virtqueue.c`, `vmapp/`. This job edits `kernel/src/ui/*`,
`kernel/src/gfx.*`, `kernel/src/image/*`, scripts and docs. The only shared
file so far is `kernel/Makefile` (both add source lines — trivial merge).

---

## 3. Done so far (all committed, all verified)

| Step | Commit | What |
|---|---|---|
| LG1 | `f5f9afe` | `gfx_liquid_glass()` material in `kernel/src/gfx.c` |
| LG2 | `f5f9afe` | Dock, menus, menu bar, Control Center, Spotlight, tooltips, Cmd+Tab, Mission Control cards, login card use it |
| W1 | `ea69750` | Baseline JPEG decoder `kernel/src/image/jpeg.c` |
| W2-W4 | `d32722d` | Tahoe wallpaper pack, Settings > Wallpaper grid, Preview viewer, saved choice, boot-forces-Light bug fixed |

Earlier in this branch (before Liquid Glass): browser text in Inter, dark-mode
browser, real Settings pages for Wallpaper/Dock/Keyboard/Mouse, TextEdit and
Notes caret + paper look (`b8eede0` .. `efc8b91`).

### 3.1 Liquid Glass (LG1/LG2)

- `kernel/src/gfx.h`: `GfxGlass { tint, tint_alpha, blur, refract, band,
  specular, saturate, alpha }`, `gfx_set_scratch()`, `gfx_liquid_glass()`.
  It copies the CURRENT FRAME behind the shape (windows included), blurs at
  half resolution (2x box), boosts saturation, bends the backdrop along the
  edge normal inside a rim band ((1-t)^2 falloff), adds a thin tint, a
  specular rim lit from above, an inner bottom shade and an AA edge. Interior
  pixels use a fast path. Scratch memory comes from the compositor
  (`glass_scratch`, PMM) — never BSS.
- `kernel/src/ui/compositor.c`: `glass_params()` (line ~734) maps the old
  frosted "tint" to Liquid Glass parameters; `ui_glass_fade()` /
  `ui_glass()` draw it; Reduce Transparency keeps the solid fallback. The
  menu bar is `ui_glass(0,0,w,H,0,150)`. Logo in light mode uses text colour.
- Cost: drag frame at 2x went 28 ms -> 33 ms (`tests/qemu_perf.py`).
- Tests: `tests/test_gfx.c` (35 checks incl. 11 for glass).

### 3.2 Wallpapers (W1-W4)

**Licensing rule (do not break):** the Tahoe pictures are Apple's. They live
only in git-ignored `wallpapers/local/` and are written only into locally built
disk images. Never `git add` anything under `wallpapers/local/`, never copy
them into `sysroot/`, never commit `.jpg/.png/.heic/.pack` files, never push.

Pipeline:

1. `python3 -I tools/import_wallpapers.py --fetch` downloads 17 originals into
   `wallpapers/local/originals/` with `curl` (python.org's Python here has no
   CA bundle — urllib fails with CERTIFICATE_VERIFY_FAILED). 6 Tahoe PNGs from
   media.512pixels.net (6016 px; Light/Dark are square), 11 aerial `*_1_Mac.heic`
   from basicappleguy.com/s/ (the blog links `/s/<Name>_<n>_Mac.heic`; `_2/_3`
   variants exist and are not used). `CATALOG` in the script = names, kind
   (light/dark/any) and display order.
2. `scripts/mkimg.sh` (every build) runs `--pack`: HEIC -> PNG via macOS
   `sips`, cover-crop to 2880x1800 (LANCZOS), baseline JPEG **4:4:4** q88 +
   320x200 thumbnail; cached in `wallpapers/local/cache/`; pack written to
   `wallpapers/local/wallpapers.pack` (~16 MiB) and `dd`'d into the image at
   **LBA 102400**. No originals => region stays zero => only built-in waves.
3. Disk contract (keep in sync in 4 places: `kernel/src/image/wallpack.h`
   `WALLPACK_LBA`, `tools/finalize_image.py` `PACK_FIRST`, `scripts/mkimg.sh`
   `PACK_FIRST`, the `102400` in `tests/qemu_*.py` fresh-data wipes):
   - LBA 67584..102399 = user data (persistence slots at 67584 and 83968, 8 MiB each, rest spare).
   - LBA 102400..262110 = read-only picture pack (belongs to the build).
   - `finalize_image.py` now preserves ONLY 67584..102399 from the old disk and
     takes the pack from the new build (test: `test_update_takes_wallpaper_pack_from_candidate`).
4. Pack format (`tools/import_wallpapers.py` docstring is the spec): 16 KiB
   header: `"OJWP"`, version 1, count, CRC-32 of the entry table; 128-byte
   entries (name[64], kind, width, height, image sector/bytes/crc, thumb
   sector/bytes/crc); blobs sector-aligned after the header.
5. Kernel reader `kernel/src/image/wallpack.c`: `wallpack_parse()` (pure,
   host-tested), `wallpack_load/count/entry/find/read()`; reads in 64-sector
   chunks; checks each blob's CRC. Host test `tests/test_wallpack.c` parses a
   pack made by the real Python writer (`scripts/test.sh` generates it) and
   fuzzes 4000 sealed hostile tables.
6. Decoder `kernel/src/image/jpeg.c`: baseline/extended-sequential Huffman,
   8-bit, 1 or 3 components, sampling up to 2x2, DRI/RST; progressive and
   arithmetic rejected; integer IDCT with int64 accumulation; chroma upsampled
   by repetition (that is why wallpapers are 4:4:4). `tests/test_jpeg.c`:
   Pillow vectors, every truncation, 4000 corruptions. On all 34 real
   wallpaper JPEGs the max difference vs Pillow is 3.
7. Compositor (`kernel/src/ui/compositor.c`, search `Desktop pictures`):
   - choices: `0` "ojjyOS Waves Light", `1` "ojjyOS Waves Dark" (procedural,
     `ui/wallpaper.c`), `2..` pack entries. Helpers:
     `wallpaper_choice_count/name/kind`, `wallpaper_find`,
     `wallpaper_counterpart` (Light<->Dark, Day<->Night, Dawn<->Dusk),
     `wallpaper_decode` (buffers sized once for the largest picture, PMM),
     `wallpaper_show` (logs `[UI] Desktop picture '<name>' WxH (N ms)`),
     `wallpaper_choose` (saves + switches appearance for light/dark pictures),
     `toggle_appearance` (every Light/Dark switch goes through it),
     `wallpaper_retint` (window glass tint after a theme change).
   - `wallpaper_scale_cover()` in `ui/wallpaper.c` crops to the screen aspect
     (no stretch; Intel 1920x1080 crops) then bilinear + dither.
   - Removed: 4 MB BSS `wallpaper_data`, `.raw` loader, Preview's 1.2 MB raw buffer.
   - Settings window is now 700x460. Settings > Wallpaper =
     `draw_wallpaper_page()` / `wallpaper_page_click()` / `wallpaper_grid()`:
     3-column 144x90 thumbnails, names, accent selection ring, scroll with the
     wheel (`SettingsStateUi.wp_scroll`, clamped in the handler and when drawn),
     overlay scroll indicator.
   - Preview app = picture viewer (`draw_preview_window`, `preview_click`):
     full-resolution image, ‹ › and "Set as Desktop Picture".
   - Saved as `wallpaper=<name>` in `/Library/Preferences/system.conf`
     (system-wide, not per user; buffer now 512 bytes; `parse_string()` in
     `ui/services.c`). Default: Tahoe Light/Dark by appearance if the pack
     exists, else the waves.
   - Bug fixed: `main.c cmd_ui(NULL)` at boot forced Light mode every boot.
     Now `ui dark` / `ui light` toggle; plain `ui` keeps the saved appearance.
8. Timings: decode + cache rebuild 115-210 ms under HVF, 0.5-0.7 s under TCG.

Evidence: `build/w3-live-2/` (live test), `build/w3-{tcg,hvf,x86}/` smoke
PASS, `build/w3-tests*.log` host + ASan/UBSan, both builds 0 warnings.

---

## 4. How to work (rules that apply to every step)

```bash
W="~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3-ui"; cd "$W"
# dev builds — NEVER build into build/aarch64 or build/x86_64 while Jonas's VM may use them
OJJY_BUILD_DIR="$W/build/dev-aarch64" ARCH=aarch64 bash scripts/build.sh > build/dev-build.log 2>&1
OJJY_BUILD_DIR="$W/build/dev-x86_64"  ARCH=x86_64  bash scripts/build.sh > build/dev-build-x86.log 2>&1
grep -E "warning:|error:" build/dev-build*.log      # must be empty
bash scripts/test.sh                                 # host suite
HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 OJJY_TEST_DIR="$W/build/tests-san" bash scripts/test.sh
# live (each run needs a NEW output dir; tests copy the disk, never touch the source image)
OJJY_ACCEL=hvf python3 -B tests/qemu_smoke.py --arch aarch64 --image build/dev-aarch64/ojjyos3.img --output build/<step>-hvf --fresh-data --protected
python3 -B tests/qemu_smoke.py --arch aarch64 --image build/dev-aarch64/ojjyos3.img --output build/<step>-tcg --fresh-data --protected
python3 -B tests/qemu_smoke.py --arch x86_64 --image build/dev-x86_64/ojjyos3.img --output build/<step>-x86 --fresh-data
OJJY_ACCEL=hvf python3 -B tests/qemu_wallpapers.py --image build/dev-aarch64/ojjyos3.img --output build/<step>-wp
OJJY_ACCEL=hvf python3 -B tests/qemu_design_tour.py --image build/dev-aarch64/ojjyos3.img --output build/<step>-tour [--dark]
# then look at the screenshots yourself (light AND dark, 2x) before committing
git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit -m "...

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git fetch -q . ui-polish-2:main
```

- Stage files by name; never `git add -A` (the repo root holds Jonas's 9.7 MB
  `ojjyos-v3-bugs-macos-features-custom-kernel.json`, never commit it).
- Big buffers come from `pmm_alloc_bytes()` (there is no free-bytes API; keep
  buffers and reuse them). Nothing large in BSS or on the kernel stack.
- Coordinates in UI code are LOGICAL (1440x900 layout); the framebuffer is
  physical (2x under HVF). `fb_ui_scale()` gives the factor. Images blitted
  with `gfx_blit_scaled()` take physical source sizes.
- Every step: log a line in `LIQUID_GLASS_APPS_PLAN.md` ## Log with evidence
  paths, commit, fast-forward `main`.
- Keep the blue Tahoe theme as the default; credit Jonas Lee; no AI names in
  the product.
- Downloaded files are untrusted: process them with `python3 -I`.

---

## 5. What still needs to be done (in this order)

Each item lists the files to touch and what "done" means. Mark it in the
plan log when finished.

### C — Appearance customisation (Settings > Appearance) — DONE (2026-10-18)

Implemented on `ui-polish-3` (see `LIQUID_GLASS_APPS_PLAN.md` log): all six
rows plus the moved switches, Auto clock flipping, accent table, persistence
keys, boot-summary log line and the live test `tests/qemu_appearance.py`.
The remaining items below are still open, in this order.

Jonas asked for "customizability for themes/ui/background in settings".
Add to `SettingsState` (`ui/services.h`) and save/load in `ui/services.c`
(follow the `wallpaper=` pattern; keys must not be substrings of each other —
the loader uses `str_find`, e.g. prefer `accent=`, `glass=`, `menubar=`,
`corners=`, `appearance=`, `termtheme=`):

1. **Appearance: Light / Dark / Auto.** Auto = dark between 19:00 and 07:00
   (`rtc_read_time`). Check once a minute in the main loop / clock update and
   call `toggle_appearance()` when it should flip (paired wallpapers follow).
   UI: segmented control (`draw_segmented`/`segmented_hit`, `seg_x`) replacing
   the Dark Mode switch row. Keep `dark=` for compatibility.
2. **Accent colour:** Blue (default — the Tahoe theme), Purple, Pink, Red,
   Orange, Yellow, Green, Graphite. `ui/theme.c` has two static
   `ThemeTokens` (`theme_light()`, `theme_dark()`); make `accent` and
   `accent_soft` come from the chosen accent (e.g. a non-const copy updated on
   change). Draw a row of 8 coloured circles with a ring on the selected one.
   Everything that uses `theme->accent` follows automatically; verify Settings
   sidebar selection, switches, segmented controls, Finder selection, Dock
   indicators, browser link/loading colours (`ui/browser.c`), Calendar today.
3. **Liquid Glass: Clear / Tinted** (Tahoe 26.1 option): Tinted = higher
   `tint_alpha` and less transparency in `glass_params()`. Reduce
   Transparency switch stays.
4. **Menu bar: Glass / Solid** (`draw_menu_bar`, line ~2672).
5. **Window corners: Standard / Round** (window radius used by
   `draw_window`; LG3 below wants 16 for Round).
6. **Terminal theme** row (data comes from S4; add the row now, wire in S4).

All apply live (mark_dirty + `wallpaper_retint()` where colours change) and
survive a cold boot. Settings > Appearance will need more rows than
`APPEARANCE_ROW_Y(i)` (40 + 30*i) fits in 460 px — reuse the grid page's
scroll approach or use two columns. Add a live test (extend
`tests/qemu_wallpapers.py` or new `tests/qemu_appearance.py`) that changes
accent + glass + corners, screenshots, reboots and checks they persisted
(serial log lines like `[UI] Appearance ...` make this easy).

### LG3 — Window glass rims — DONE (2026-10-18)

`gfx_round_rect_rim()` (specular band only, ~perimeter cost) over the
cached-glass body + hairline in `draw_window`; clearer title sheen; radius
16 via the C corners setting. See `LIQUID_GLASS_APPS_PLAN.md` log. A
systematic concentric-inner-panel pass folds into LG4. The remaining items
below are still open, in this order.

Windows keep their cheap cached wallpaper-blur body (`wp_glass`), but gain the
Liquid Glass rim (refraction band + specular highlight) and a clearer title
area; radius 16 (or the C corner setting); concentric inner panels (inner
radius = outer - padding). `draw_window` (~line 2276) and
`draw_window_animated` (~2563). Measure with `tests/qemu_perf.py`; a drag
frame at 2x must stay within ~10% of 33 ms. Check `tests/qemu_anim_check.py`.

### LG4 — Glass controls

Glass capsules for toolbar buttons (Finder back/forward/view, browser
toolbar in `ui/browser.c`), segmented controls, push buttons, switches
(`draw_switch` ~797, `draw_segmented` ~1102). Small shapes: use a cheap
variant (no backdrop blur, just tint + specular rim) if the full material is
too slow.

### S1 — Notifications

`notifications_push()` in `ui/services.c` only stores a string today. Build a
queue of `(icon, title, body, time)`; glass banner slides in at top-right,
auto-dismiss after 4 s, click dismisses; Reduce Motion = fade only. Post on:
file saved (TextEdit/Notes Cmd+S), Wi-Fi on/off/connected, Trash emptied,
wallpaper changed, page/download finished, low memory. A Notification Center
list (click the clock) is optional.

### S2 — Window snapping

Dragging a window (`dragging`/`drag_index` in compositor) to the left/right
screen edge = half screen, to the top = maximise; show a translucent glass
preview of the target while dragging; double-click title bar = zoom. Release
applies with an animation. Restore size when dragged away.

### S3 — Live resize

Verify windows re-layout continuously while the resize grip is dragged
(`resizing`/`resize_index`), at HVF frame rate; fix whatever lags (e.g. skip
the animation layer cache during resize). Measure with `qemu_perf.py`.

### S4 — Terminal themes

Themes: Tahoe Dark (default), Light, Ocean, Classic Green. Coloured prompt
(user@host in accent, path in blue — `terminal_make_prompt` ~4711), `ls`
colours (folders blue, apps purple — `terminal_ls` ~4776; needs per-span
colour in the terminal line model, `TerminalState` lines are plain char
arrays today), `theme <name>` command in `terminal_exec` (~4837), Settings row
from C. Blinking caret.

### A — Every app (table in `LIQUID_GLASS_APPS_PLAN.md` Phase A)

Finder (breadcrumb path bar, status bar with item count/free space, icon view,
sidebar sections, sort by column, glass toolbar); Terminal (S4, scrollback
indicator, `clear`); TextEdit (soft word wrap, word/char count, save
notification); Notes (sidebar list of `~/Notes/*.txt`, New Note, autosave);
Calendar (accent today circle, event chips, glass header); Calculator (key
press highlight, bigger display, history line); Activity Monitor (CPU/frame
time sparkline, memory bar, zebra table, segmented tabs); Settings (glass
sidebar, restyled Network/Users, new General page: name, version, Retina
mode, uptime); Web (glass toolbar, start page tiles); About (hero icon,
version card, credits to Jonas Lee); Preview (done: viewer + Set as Desktop
Picture; optional: open real image files from Finder — JPEG decoder exists);
Login (Liquid Glass card + glass buttons). One commit per app or small group;
screenshot light + dark for each.

### Finish

1. Update docs: `APPS.md`, `DESKTOP.md`, `README.md` for everything new;
   `AGENTS.md` LATEST block.
2. Rebuild the INSTALLED images (`ARCH=aarch64 bash scripts/build.sh`, same
   for x86_64 — default build dir) only when no VM uses them:
   `lsof build/aarch64/ojjyos3.img` must be empty and no `qemu-system`
   process may have it open. `finalize_image.py` refuses locked disks and
   keeps a backup; it preserves accounts/files and installs the new pack.
   Do this from the worktree only if the main folder's job is finished;
   otherwise tell Jonas the dev images are in `ojjyOS-v3-ui/build/dev-*`.
3. Remove the worktree only after `main` contains everything:
   `git worktree remove ../ojjyOS-v3-ui` (from the main folder) — and only
   if the native-VM AI is not using it. Ask nothing; if unsure, leave it.
4. Plain-English summary to Jonas.

### Small leftovers found during this job

- `scripts/mkfs.sh` still generates three 640x480 `.raw` wallpapers into
  `/System/Wallpapers` (~3.6 MB in the kernel's embedded filesystem). Only
  Spotlight's file index uses them now (`ui/services.c` ~228). Remove them
  and make Spotlight list the desktop pictures instead (search "Tahoe" ->
  sets the wallpaper).
- Desktop right-click "Change Wallpaper..." should open Settings > Wallpaper.
- Wallpaper choice is system-wide; per-user would mean reloading settings at
  login (`users.c` login path) — Jonas's machine is effectively single-user,
  so this is optional.
- `wallpaper_decode` runs synchronously (~0.15 s HVF / ~0.6 s TCG): fine for
  clicks; a cross-fade between old and new picture would look nicer.
- `jpeg.c` could be reused for `<img>` in the browser (baseline only;
  progressive JPEGs are common on the web, so add progressive first) and for
  opening `.jpg` files in Preview.
