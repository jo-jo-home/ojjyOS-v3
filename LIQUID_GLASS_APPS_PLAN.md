# Liquid Glass + Better Apps Plan (2026-10-18)

Jonas: "i want liquid glass like tahoe and also improve all the apps. make a
detailed plan for all then implement them." Earlier in the same thread he
also said yes to: notifications, window snapping, live window resizing and
Terminal themes.

Work happens in the `ojjyOS-v3-ui` worktree (branch `ui-polish-2`) so it
never touches the other AI's native-VM job; every finished step is
fast-forwarded into `main`. Each step: build both arches (0 warnings), host
suite, live suites, screenshots in light AND dark at 2x, local commit.

## What "Liquid Glass" means here (macOS Tahoe 26 look)

| Property | Old ojjyOS glass | Liquid Glass |
|---|---|---|
| Background | blurred *wallpaper only*, heavy tint | the *actual pixels behind* (windows too), light blur, saturation boost |
| Edges | flat hairline | **refraction**: the backdrop bends inward near the rim like a lens |
| Light | none | **specular rim** brightest on the top edge, faint on the bottom; soft inner glow |
| Tint | fixed frosted fill | thin adaptive tint (white in light mode, smoke in dark) |
| Shapes | radius 10-16 | larger, concentric radii (panels 22, controls = capsules) |
| Chrome | opaque bars | transparent menu bar, floating glass Dock / panels / controls |

## Phase LG — the material

- **LG1** `gfx_liquid_glass()` in `kernel/src/gfx.c`: reads the region behind
  the shape from the current frame, box-blurs it (2 passes, scratch buffer
  supplied by the compositor, no BSS), boosts saturation, then per pixel:
  signed distance to the rounded rect; inside an edge band the sample point
  is displaced along the edge normal (refraction falloff (1-t)^2); thin tint;
  specular rim = light direction (top) · edge normal; bottom inner shade;
  anti-aliased edge. Host tests: interior equals tinted blurred backdrop,
  rim brighter at top than bottom, outside pixels untouched, clipping safe
  (ASan). Perf receipt via `[PERF]`.
- **LG2** Apply to floating chrome: Dock (+ magnified icons sit on glass),
  menu bar (transparent glass strip), dropdown/context menus, Control Center
  (+ tiles as inner glass), Spotlight, app switcher, Dock tooltips, login
  card, Mission Control cards. Reduce Transparency keeps the solid fallback.
- **LG3** Windows: glass body stays the cached wallpaper blur (cheap, full
  window), but gains the Liquid Glass rim (refraction band + specular
  highlight) and a clearer title area; radius 16. Concentric inner panels.
- **LG4** Controls: glass capsules for toolbar buttons (Finder back/forward/
  view, browser toolbar), segmented controls, push buttons, switches.

## Phase S — system features

- **S1 Notifications**: `notify(icon, title, body)` queue; glass banner
  slides in top-right, auto-dismiss 4 s, click to dismiss; used for: file
  saved (TextEdit/Notes), Wi-Fi on/off/connected, Trash emptied, wallpaper
  changed, page download finished, low memory.
- **S2 Window snapping**: drag a window to the left/right edge -> half
  screen, to the top -> maximise; translucent glass preview of the target
  while dragging; double-click title bar = zoom.
- **S3 Live resize**: verify windows re-layout continuously while the
  resize grip is dragged (and at 60 fps under HVF); fix whatever lags.
- **S4 Terminal themes**: Tahoe Dark (default), Light, Ocean, Classic Green;
  coloured prompt (user@host accent, path blue), `ls` colours (folders
  blue, apps purple), `theme <name>` command + Settings > Terminal row.

## Phase A — every app

| App | Improvements |
|---|---|
| Finder | breadcrumb path bar, item count + free space status bar, larger icon view with labels, sidebar sections (Favorites / Locations), sort by name/kind/size (click headers), glass toolbar |
| Terminal | themes (S4), blinking caret, scrollback indicator, `clear`, history with Up/Down if missing |
| TextEdit | soft word wrap (ROADMAP), Cmd+S notification, word/char count in status |
| Notes | sidebar list of notes (~/Notes/*.txt), New Note button, auto-save + notification |
| Calendar | today circle in accent, event colour chips, glass header, mini-month polish |
| Calculator | key press highlight animation, larger display, history line |
| Activity Monitor | live CPU/frame-time sparkline graph, memory bar, zebra process table with header, segmented tabs |
| Settings | glass sidebar, restyled Network and Users pages (cards, new fields/buttons), new General page (name, version, Retina mode, uptime) |
| Web | glass toolbar, start page tiles (favourites as icon tiles), loading bar polish |
| About | larger hero icon, version card, credits |
| Preview | already redone; add "Set as wallpaper" buttons |
| Login | Liquid Glass card + glass buttons |

## Phase W — real Tahoe wallpapers (Jonas: "add the official tahoe backgrounds")

Sources he named: 512pixels.net (Light/Dark Tahoe + Tahoe Beach Dawn/Day/
Dusk/Night, 6K PNG) and basicappleguy.com (11 Tahoe aerial stills, 6K HEIC).
These are **Apple's images**: they are downloaded to a git-ignored local
folder (`wallpapers/local/`) and baked only into Jonas's own disk image at
build time — never committed, never pushed.

- **W1** Kernel baseline-JPEG decoder (`kernel/src/image/jpeg.c`): Huffman,
  integer IDCT, 4:4:4 / 4:2:2 / 4:2:0, restart markers; bounded and safe on
  bad input. Host tests decode Pillow-made fixtures and compare pixels (small
  tolerance), plus fuzzed corrupt files under ASan.
- **W2** `tools/import_wallpapers.py`: `--fetch` downloads the listed files
  into `wallpapers/local/` (macOS `sips` converts HEIC); the build converts
  every image there to baseline JPEG 2880x1800 (q 88) + a 320x200 thumbnail
  and puts them in `/Library/Desktop Pictures/` of the system image.
- **W3** Wallpaper library: the compositor lists `/Library/Desktop Pictures`
  plus the procedural Tahoe Light/Dark; decodes the chosen JPEG at boot or on
  selection, scales it to the screen (bilinear + dither), caches blur/glass.
  The old 4 MB BSS wallpaper buffer goes (runtime allocation).
- **W4** Settings > Wallpaper becomes a scrolling thumbnail grid with names;
  the choice is saved per user and restored at login.

## Phase C — customisation (Jonas: "customizability for themes/ui/background in settings")

Settings > Appearance gains:
- Appearance: Light / Dark / Auto (dark 19:00-07:00 by the clock).
- Accent colour: Blue (default, Tahoe), Purple, Pink, Red, Orange, Yellow,
  Green, Graphite — everything that uses the accent follows it.
- Liquid Glass: Clear / Tinted (Tahoe 26.1 option) + the existing Reduce
  Transparency.
- Menu bar: transparent glass / solid.
- Window corners: Standard / Round; Dock: size + magnification (exists).
- Terminal theme (S4) row.
All saved in settings and applied live.

## Status (2026-10-18)

| Step | State |
|---|---|
| LG1, LG2, W1, W2, W3, W4, C | done (see Log) |
| LG3, LG4, S1, S2, S3, S4, A, leftovers, finish | not started — detailed instructions in `HANDOFF_UI.md` §5 |

## Order of work

LG1 -> LG2 -> W1 -> W2 -> W3 -> W4 -> C -> LG3 -> LG4 -> S1 -> S2 -> S3 -> S4 -> A (Finder, Terminal,
TextEdit, Notes, Calendar, Calculator, Activity Monitor, Settings, Web,
About, Preview, Login). Commit after each; log below.

## Log
- C done (2026-10-18, on `ui-polish-3` off integrated `main`): Settings >
  Appearance rebuilt — Appearance segmented (Light/Dark/Auto; Auto = dark
  19:00–07:00 by the RTC, checked once a minute from the main loop and
  immediately on selection, pairing wallpapers), 8 accent swatches (Blue
  default keeps the original hand-tuned tokens pixel-unchanged; Purple,
  Pink, Red, Orange, Yellow, Green, Graphite derive light/dark variants
  from one base via `theme_set_accent()`), Liquid Glass Clear/Tinted
  (tint_alpha × 1.5), Menu Bar Glass/Solid, Window Corners Standard/Round
  (16), Terminal Theme row (Tahoe/Light/Ocean/Green; S4 wires the actual
  terminal colours) plus the existing Reduce Transparency/Motion and
  24-Hour Clock switches, moved to rows 6–8. Persisted as appearance=,
  accent=, glass=, menubar=, corners=, termtheme= in system.conf (legacy
  dark= still honoured on load). Every change logs `[UI] Appearance
  key=value`; settings_load logs a one-line summary at boot. New live test
  `tests/qemu_appearance.py`: clicks every control, asserts each serial
  line, verifies the Auto clock flip (time-of-day aware), syncs, cold-boots
  and asserts the persisted summary — PASS (`build/appearance-live-2`).
  Accent switch visibly changes the desktop (1,285/25,000 sampled pixels
  between Blue and Green+Tinted). Host suite 15/15, both arches 0 warnings.
  Evidence: `build/appearance-{arm,x86,host,live-2}.log` + suites below.
- LG1 done: `gfx_liquid_glass()` + `gfx_set_scratch()` (live-backdrop copy,
  half-res 2x box blur, saturation, rim refraction (1-t)^2 along the edge
  normal, tint, specular rim lit from above, AA edge, fast interior path).
  11 new host checks (35 in test_gfx, ASan/UBSan clean).
- LG2 done: all floating chrome uses it via `ui_glass_fade` (Dock, menus,
  context menu, Control Center, Spotlight, tooltips, Cmd+Tab, Mission Control
  cards, login card) and the menu bar is a near-clear glass strip; menus
  visibly refract the windows under them. Logo uses text colour in light
  mode (blue-on-blue was unreadable). Cost measured: 28 ms -> 33 ms per drag
  frame at 2x (build/expperf vs build/lg2perf3).
- W1 done: kernel/src/image/jpeg.c baseline JPEG decoder (Huffman, integer IDCT, 1/3 components, 4:4:4/4:2:2/4:2:0, restart markers, progressive rejected). tests/test_jpeg.c: 6 Pillow vectors (tests/gen_jpeg_vectors.py) match, truncation at every length + 4000 corruption rounds under ASan/UBSan. Chroma is upsampled by repetition, so wallpapers are stored 4:4:4 (exact).
- W2 done: `tools/import_wallpapers.py --fetch` downloads 17 pictures (6 Tahoe from 512pixels, 11 aerials from basicappleguy; HEIC via `sips`) into git-ignored `wallpapers/local/`; mkimg.sh converts them (2880x1800 cover-crop, baseline 4:4:4 q88 + 320x200 thumbs, 16 MiB total, cached) into a pack at **LBA 102400** in the data partition tail. `finalize_image.py` now preserves only LBA 67584..102399 (user data) and takes the pack from the new build; test fresh-data wipes stop at 102400 too. Without local pictures the region is empty and only the built-in waves show. Kernel decoder vs Pillow on all 34 real JPEGs: max diff 3.
- W3 done: `kernel/src/image/wallpack.c` (validated table + CRC per blob; host test with writer-made pack + 4000 fuzzed tables). Compositor: choices = 2 built-in waves + pack; decode into a pmm buffer (HVF 115-210 ms, TCG ~0.5-0.7 s), cover-scaled to the screen (Intel 16:9 crops, no stretch). 4 MB BSS `wallpaper_data` and the 1.2 MB Preview raw buffer are gone. Default: Tahoe Light/Dark by appearance. Light/Dark toggle swaps paired pictures (Light/Dark, Day/Night, Dawn/Dusk). Fixed: every boot forced Light mode (cmd_ui(NULL)); saved appearance now survives reboot.
- W4 done: Settings (now 700x460) > Wallpaper is a scrolling 3-column thumbnail grid with names, selection ring and scroll indicator; choice saved in system.conf (`wallpaper=`; system-wide, not per user). Preview is a picture viewer (prev/next, "Set as Desktop Picture"). Evidence: build/w3-live-2 (tests/qemu_wallpapers.py: pack found, grid, scroll, Dark pairing, Preview, cold boot keeps choice), build/w3-{tcg,hvf,x86} smoke PASS, host + ASan/UBSan suites pass, both builds 0 warnings. Leftover: the 640x480 `.raw` wallpapers in sysroot are unused except by Spotlight's file list.
