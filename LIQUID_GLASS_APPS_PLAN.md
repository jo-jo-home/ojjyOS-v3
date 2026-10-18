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

## Order of work

LG1 -> LG2 -> LG3 -> LG4 -> S1 -> S2 -> S3 -> S4 -> A (Finder, Terminal,
TextEdit, Notes, Calendar, Calculator, Activity Monitor, Settings, Web,
About, Preview, Login). Commit after each; log below.

## Log
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
