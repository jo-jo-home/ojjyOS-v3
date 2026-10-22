# 16 — Desktop shell

Menu bar, Dock, desktop, Launchpad, Spotlight, Mission Control, Control
Center, Notification Center, login and setup, window organisation and the
small system UIs (HUDs, force quit, share sheet, emoji picker…).

Today all of this lives inside `kernel/src/ui/compositor.c`. P0 cards finish
the current desktop track (`DESKTOP_PLAN.md`); from P2 each shell piece
becomes its own user process (or a WindowServer module) using ojjyKit,
talking to WindowServer through the surface protocol.

Blend reminder (`01-vision.md`): macOS layout and feel first, Windows
conveniences as options (Snap Layouts, Alt-Tab thumbnails, taskbar mode,
clipboard history), Linux power options (tiling mode, keyboard-driven
everything).

---

## P0 — finish the current desktop track (Track D)

### SHL-02 — Notification banners (DESKTOP_PLAN D2)
- Phase: P0 · Size: M · Status: todo
- Depends: —
- Files: DESKTOP_PLAN.md D2, kernel/src/ui/compositor.c

**Context.** `notifications_push()` exists without real banners.
**Goal.** As specified in `DESKTOP_PLAN.md` D2 (queue, glass banners,
auto-dismiss, posts from apps, Notification Center list).
**Done when.** D2 acceptance in DESKTOP_PLAN.md.
**Tests.** `tests/qemu_desktop_features.py` (new, per D2).
**Pitfalls.** Keep `notifications_push()` signature (Track N calls it).
**Refs.** DESKTOP_PLAN.md.

### SHL-03 — Window snapping (DESKTOP_PLAN D3)
- Phase: P0 · Size: M · Status: todo
- Depends: —
- Files: DESKTOP_PLAN.md D3, kernel/src/ui/compositor.c

**Goal.** As D3: edges = halves, top = maximise, glass preview, restore
on drag-away, double-click title zooms.
**Done when.** D3 acceptance.
**Tests.** QMP drag test asserting frames.
**Pitfalls.** —
**Refs.** DESKTOP_PLAN.md.

### SHL-04 — Live resize smoothness (DESKTOP_PLAN D4)
- Phase: P0 · Size: M · Status: todo
- Depends: —
- Files: DESKTOP_PLAN.md D4

**Goal.** As D4: continuous relayout during resize with measured frame
times; window layer caching so only changed windows redraw.
**Done when.** D4 acceptance (numbers in the log, no tearing).
**Tests.** `qemu_perf.py` resize scenario under HVF.
**Pitfalls.** Measure on a quiet host (BETTER_OS_PLAN §5).
**Refs.** DESKTOP_PLAN.md.

---

## Shell processes (P2+)

### SHL-01 — Menu bar
- Phase: P2 · Size: M · Status: todo
- Depends: WS-02, UIK-20
- Files: services/menubar/ (from compositor.c)

**Goal.** Menu bar process: system menu (ojjy logo: About This ojjyOS,
System Settings, App Store, Recent Items, Force Quit, Sleep, Restart,
Shut Down, Lock Screen, Log Out), active app's menus (published by apps
through UIK-20), status items API (menu extras: Wi-Fi, battery, sound,
Bluetooth, Focus, user name, clock, Control Center, Spotlight), glass or
solid per appearance, auto-hide in full screen, per-display menu bars,
overflow handling when menus collide with status items.
**Done when.** Visual parity with today's menu bar; apps' menus appear;
third-party status items work.
**Tests.** UI tour tests; menu interaction automation.
**Pitfalls.** Keyboard navigation (Ctrl-F2) and accessibility.
**Refs.** DS-02.

### SHL-05 — Dock
- Phase: P2 · Size: M · Status: todo
- Depends: WS-02, UIK-23, ANI-01, SHL-27
- Files: services/dock/

**Goal.** Pinned apps and running indicators, magnification with springs,
minimise-to-Dock (genie/scale), stacks (Downloads, folders as fan/grid),
Trash with full/empty state, badges and bounce attention, recent apps
section, positions (bottom/left/right), auto-hide, drag to add/reorder/
remove ("poof"), right-click menus (Options: Keep in Dock, Open at Login,
Show in Finder, Quit), glass material.
**Done when.** Feature list complete; Dock settings pane works.
**Tests.** UI automation tests.
**Pitfalls.** Smooth magnification at 60 fps with many icons (cache
scaled icons).
**Refs.** DS-12.

### SHL-06 — Taskbar mode (Windows-style option)
- Phase: P4 · Size: M · Status: todo
- Depends: SHL-05, SHL-07
- Files: services/dock/taskbar.c

**Goal.** Optional bottom taskbar replacing the Dock: Start button
(opens Launchpad in "Start menu" layout with pinned + recommended),
window buttons with thumbnails on hover, system tray (status items move
from the menu bar), clock; menu bar optionally hidden (app menus move
into windows' title bars).
**Done when.** Toggle in Settings > Desktop & Dock switches modes live.
**Tests.** UI tests in both modes.
**Pitfalls.** Keep both modes first-class; don't break shortcuts.
**Refs.** —

### SHL-07 — Launchpad and app library
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-23, SHL-27
- Files: services/launchpad/

**Goal.** Full-screen glass grid of all apps with pages, folders
(drag icon onto icon), search, jiggle mode to reorder/delete
(uninstall for Store/ojjypkg apps), Start-menu layout variant used by
SHL-06.
**Done when.** Parity with today's Launchpad plus folders and uninstall.
**Tests.** UI automation tests.
**Pitfalls.** —
**Refs.** —

### SHL-08 — Spotlight v2
- Phase: P3 · Size: L · Status: todo
- Depends: SVC-30, UIK-17, SHL-27
- Files: services/spotlight/ (UI), services/searchd (index, SVC-30)

**Goal.** Cmd-Space glass search bar: apps, files (names and contents
via searchd), settings panes, contacts, mail, calendar events, web
suggestions (optional), calculator and unit/currency conversion,
dictionary, actions ("Shut Down", "New Note"), quick previews on the
right, keyboard-first navigation, ranking by usage.
**Done when.** Typing a word inside a TextEdit document finds it within
200 ms; "2+2" shows 4; "10 km in miles" converts.
**Tests.** Live tests with seeded files.
**Pitfalls.** Sandbox: results respect file permissions.
**Refs.** —

### SHL-09 — Mission Control and Spaces
- Phase: P4 · Size: L · Status: todo
- Depends: WS-03, WS-08, ANI-01
- Files: services/windowserver/spaces.c, services/missioncontrol/

**Goal.** Virtual desktops (Spaces) with add/remove/reorder, per-display
or global, apps assigned to Spaces, full-screen apps as their own Space,
Mission Control overview (all windows tiled, Spaces bar), App Exposé
(windows of one app), three-finger swipes and Ctrl-arrows, Windows-style
Win-Tab equivalent.
**Done when.** Four Spaces with smooth sliding and window drag between
Spaces in Mission Control.
**Tests.** UI automation tests.
**Pitfalls.** Window thumbnails need surface snapshots (cheap scaling).
**Refs.** —

### SHL-10 — App switcher
- Phase: P2 · Size: S · Status: todo
- Depends: WS-04, SHL-27
- Files: services/windowserver/switcher.c

**Goal.** Cmd-Tab app switcher (icons, glass), with Cmd-` for windows of
the same app; optional Alt-Tab window switcher with live thumbnails
(Windows style); Q to quit, H to hide while switching.
**Done when.** Both switchers work and are configurable.
**Tests.** QMP key tests.
**Pitfalls.** —
**Refs.** —

### SHL-11 — Control Center v2
- Phase: P3 · Size: M · Status: todo
- Depends: SHL-01, UIK-14
- Files: services/controlcenter/

**Goal.** Glass modules: Wi-Fi, Bluetooth, AirDrop-like sharing, Focus,
Display brightness + Night Shift + Dark Mode, Sound volume + output
device, Now Playing (media keys), Screen Mirroring, Accessibility
shortcuts, battery; module API for third parties; customization (add/
remove/reorder modules, pin to menu bar).
**Done when.** All modules functional where hardware exists; honest
"no hardware" states in VMs.
**Tests.** UI automation tests.
**Pitfalls.** —
**Refs.** —

### SHL-12 — Notification Center and widgets
- Phase: P4 · Size: L · Status: todo
- Depends: SVC-11, SHL-02, UIK-01
- Files: services/notificationcenter/, lib/ojjykit/widgets/

**Goal.** Click the clock: notification history grouped by app with
actions (reply, mark done), and widgets (Clock, Calendar, Weather,
Reminders, Notes, Battery, System Monitor, Photos, Stocks-like with
web data) in small/medium/large sizes; widget framework with timeline
updates (low power), widgets placeable on the desktop too.
**Done when.** Widgets refresh on schedule; notification actions work.
**Tests.** UI tests; widget timeline host tests.
**Pitfalls.** Widgets must not run continuously (battery).
**Refs.** —

### SHL-13 — Focus modes (Do Not Disturb)
- Phase: P4 · Size: S · Status: todo
- Depends: SVC-11
- Files: services/notifyd/focus.c, Settings > Focus

**Goal.** Focus modes (Do Not Disturb, Work, Sleep) with allowed apps,
schedules, menu bar indicator, share status with apps.
**Done when.** Banners suppressed during Focus, delivered to history.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### SHL-14 — Desktop
- Phase: P2 · Size: M · Status: todo
- Depends: WS-02, UIK-23, FS-08
- Files: services/desktop/ (from compositor.c)

**Goal.** Desktop window under all others: `~/Desktop` icons (grid,
snap, arrange by, label sizes), mounted volumes, drag and drop, stacks
(group by kind), wallpaper rendering (static/dynamic/time-of-day, per
display, per Space), right-click menu (New Folder, Get Info, Change
Wallpaper, Use Stacks, Show View Options), widgets layer, click on
wallpaper to reveal desktop (Tahoe behaviour, optional).
**Done when.** Parity with today plus stacks and live updates via FS-08.
**Tests.** UI tests.
**Pitfalls.** Wallpaper decoding off the main thread.
**Refs.** —

### SHL-15 — Login window, lock screen, user switching
- Phase: P2 · Size: M · Status: todo
- Depends: SVC-04, UIK-17
- Files: services/loginwindow/

**Goal.** Login window process (user list or name+password, Touch-ID-like
fast unlock later), lock screen with clock, notifications preview
setting, Restart/Shut Down, fast user switching (keep sessions running),
guest user option, password hints, auto-login option (off by default).
**Done when.** Two users logged in simultaneously, switch between them.
**Tests.** Live tests (cold login exists today — keep it).
**Pitfalls.** Secure input; brute-force delays after failed attempts.
**Refs.** —

### SHL-16 — Setup Assistant v2
- Phase: P5 · Size: M · Status: todo
- Depends: SHL-15, I18N-01, NET-11
- Files: services/setupassistant/

**Goal.** First-boot flow: welcome (animated "hello" in many languages),
language, region, keyboard, network (Wi-Fi), migration (import from
another ojjyOS backup), create account, appearance (light/dark/auto),
privacy/analytics (off), FileVault-like encryption offer, done.
**Done when.** Full flow under VZ in < 2 minutes; every step skippable
where sensible.
**Tests.** Live test (existing setup tests extended).
**Pitfalls.** Keep the first-account bootstrap rule (S1 hardening).
**Refs.** —

### SHL-17 — Window organisation modes (tiling, Stage-like)
- Phase: P6 · Size: M · Status: todo
- Depends: SHL-09
- Files: services/windowserver/tiling.c

**Goal.** Optional tiling mode (Linux-style: automatic master/stack and
grid layouts, keyboard moves/resizes, gaps) and a "stage" mode (current
app centre, recent apps as thumbnails on the left); per-Space setting.
**Done when.** Both modes usable entirely from the keyboard.
**Tests.** UI tests.
**Pitfalls.** Apps with min sizes in tiles.
**Refs.** —

### SHL-18 — Snap Layouts (Windows 11-style grid)
- Phase: P4 · Size: S · Status: todo
- Depends: SHL-03, WS-03
- Files: services/windowserver/snaplayouts.c

**Goal.** Hovering the green zoom button shows layout choices (halves,
thirds, quarters); keyboard shortcuts (Ctrl-Opt-arrows); snapped groups
restore together.
**Done when.** All layouts work on 1 and 2 displays.
**Tests.** UI tests.
**Pitfalls.** —
**Refs.** —

### SHL-19 — Hot corners and shortcut customisation
- Phase: P4 · Size: S · Status: todo
- Depends: WS-04, SVC-10
- Files: services/windowserver/hotcorners.c, Settings > Keyboard

**Goal.** Configurable hot corners (Mission Control, desktop, lock,
Notification Center, Quick Note) and a shortcut editor for global and
per-app menu shortcuts.
**Done when.** Settings changes take effect immediately.
**Tests.** UI tests.
**Pitfalls.** Conflicting shortcuts warning.
**Refs.** —

### SHL-20 — Clipboard history
- Phase: P4 · Size: S · Status: todo
- Depends: SVC-12
- Files: services/pasteboardd/history.c

**Goal.** Windows-style clipboard history (Cmd-Shift-V / Win-V), pinned
items, excluded for secure/password content, clear on demand, off by
default for privacy prompts.
**Done when.** Last 25 items pasteable; secure fields never recorded.
**Tests.** Live tests.
**Pitfalls.** Privacy.
**Refs.** —

### SHL-21 — Quick Look
- Phase: P3 · Size: M · Status: todo
- Depends: UIK-28, GFX-03
- Files: services/quicklookd/

**Goal.** Space in Finder/Desktop shows a glass preview panel for
images, text, code (highlighted), PDFs, audio/video, archives (listing),
fonts; arrow keys to step; plugin API for new types; thumbnails service
for Finder icons.
**Done when.** Previews for 10 file types.
**Tests.** UI tests with sample files.
**Pitfalls.** Previews run sandboxed (untrusted files).
**Refs.** —

### SHL-24 — Volume, brightness and status HUDs
- Phase: P3 · Size: S · Status: todo
- Depends: SHL-11
- Files: services/controlcenter/hud.c

**Goal.** Glass HUD overlays for volume, brightness, keyboard backlight,
caps lock, airplane mode, with media keys.
**Done when.** Media keys show HUDs.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SHL-25 — Force Quit and hang detection
- Phase: P2 · Size: S · Status: todo
- Depends: WS-02, PROC-01
- Files: services/windowserver/hang.c, services/forcequit/

**Goal.** Apps that don't answer WindowServer pings for 2 s get the busy
cursor over their windows and "(Not Responding)" in Force Quit
(Cmd-Opt-Esc) and Activity Monitor; Force Quit kills them; relaunch
offer.
**Done when.** A deliberately hung test app is detected and killed.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SHL-26 — Share sheet
- Phase: P6 · Size: S · Status: todo
- Depends: UIK-28, SHL-40
- Files: lib/ojjykit/share.c

**Goal.** Share button/menu: Copy, Mail, Messages-like chat, Notes,
Reminders, ojjyDrop (SHL-40), Save to Files; extension point for apps.
**Done when.** Sharing a photo to Notes works.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SHL-27 — App registry and file associations ("Launch Services")
- Phase: P2 · Size: M · Status: todo
- Depends: SVC-01, ELF-07
- Files: services/appregd/

**Goal.** Registry of installed apps (bundles, packages, ojjyWin and
Linux app shortcuts), their icons, document types and URL schemes;
default app per type ("Open With" > Change All), `open` command, safe
launching (quarantine prompt for downloaded apps, SEC-22).
**Done when.** Double-clicking any document opens the right app;
Open With menu lists alternatives.
**Tests.** Live tests.
**Pitfalls.** Re-scan on install/uninstall events.
**Refs.** —

### SHL-28 — ojjy menu, power actions and session end
- Phase: P2 · Size: S · Status: todo
- Depends: SHL-01, SVC-04
- Files: services/menubar/system_menu.c

**Goal.** Restart/Shut Down/Log Out with "reopen windows" checkbox,
apps asked to save (and given time), countdown dialog, Sleep, Lock.
**Done when.** Unsaved TextEdit document prompts before shutdown.
**Tests.** Live tests.
**Pitfalls.** Apps that refuse to quit: offer Force Quit.
**Refs.** —

### SHL-29 — Login items and background apps
- Phase: P4 · Size: S · Status: todo
- Depends: SVC-01, SHL-27
- Files: Settings > General > Login Items

**Goal.** Apps that open at login; list of background items with
toggles (transparent about what runs).
**Done when.** Setting persists and applies at next login.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SHL-30 — Emoji and symbols picker
- Phase: P3 · Size: S · Status: todo
- Depends: TXT-09, UIK-23
- Files: services/charpicker/

**Goal.** Ctrl-Cmd-Space / Win-. popover with categories, search,
recent, skin tones; full character viewer.
**Done when.** Inserting emoji into any text field works.
**Tests.** UI test.
**Pitfalls.** —
**Refs.** —

### SHL-40 — ojjyDrop (nearby file sharing)
- Phase: P6 · Size: M · Status: todo
- Depends: NET-13, NET-15
- Files: services/ojjydropd/

**Goal.** Send files between ojjyOS machines on the same network:
discovery via mDNS, TLS with pinned per-device keys, accept/decline
prompt, progress, received into Downloads; visibility setting (Off,
Contacts, Everyone for 10 minutes).
**Done when.** Two VMs exchange a 100 MB file.
**Tests.** Two-VM test.
**Pitfalls.** Never auto-accept from unknown devices.
**Refs.** —
