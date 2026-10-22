# 15 — Design system (Liquid Glass) and the ojjyKit toolkit

The look of ojjyOS is macOS Tahoe's **Liquid Glass** with the blue Tahoe
accent by default (AGENTS.md: keep the blue theme; user-selectable accents
already exist). This file holds (1) the **design specification** cards
(`DS-*`) that define how everything must look and move, and (2) the
**ojjyKit** toolkit cards (`UIK-*`), one per building block/control, so
every app looks consistent without drawing its own controls.

Today: controls are drawn ad hoc inside `ui/compositor.c` with helpers in
`ui/draw.h` (`ui_glass_control`, `ui_push_button`, switches, segmented
controls) and `gfx.c` (`gfx_liquid_glass`, `gfx_round_rect_rim`).

Rules for every DS/UIK card:
- Values come from **tokens** (DS-03/04/05), never hard-coded numbers.
- Every control supports: light/dark, all 8 accents, Clear/Tinted glass,
  Reduce Transparency, Reduce Motion, Increase Contrast, larger text sizes,
  keyboard focus, accessibility labels, disabled state, right-to-left
  layout (I18N-05).
- Each control card ends with screenshots of all states in
  `build/<card>-gallery/` and a UI snapshot test (UIK-50).

---

## Design specification

### DS-01 — Liquid Glass material specification
- Phase: P1 · Size: M · Status: todo
- Depends: —
- Files: docs/design/liquid-glass.md (new), kernel/src/gfx.c (current implementation notes)

**Context.** LG1–LG4 implemented an approximation (cached blur, tint,
specular rim, hairline). Before rebuilding glass in WindowServer (WS-07)
and ojjyKit (UIK-53), the target needs to be written down precisely.
**Goal.** A spec with diagrams and numbers: layer stack (backdrop blur →
refraction/lensing at edges → tint → specular highlight → hairline →
shadow), parameters per variant (Regular/Clear/Tinted; sizes: small
control, toolbar, panel, sheet, Dock, menu bar), adaptive behaviour
(content-aware luminance: darken/lighten tint over bright/dark
backgrounds; scroll-edge effects), interaction states (press: glass
"flex"/brighten; hover), concentric corner rule, accessibility
fallbacks (Reduce Transparency = frosted solid; Increase Contrast =
borders), performance budget per layer.
**Steps.**
1. Collect reference screenshots from macOS Tahoe (Jonas's Mac) for each
   surface type; store locally only (Apple imagery stays out of git), and
   describe them in words + measured numbers in the doc.
2. Define parameters and formulas; validate with a prototype in the
   current kernel `gfx.c` or the host renderer.
**Done when.** `docs/design/liquid-glass.md` reviewed by Jonas; prototype
screenshots attached (our own rendering only).
**Tests.** Prototype pixel tests (deterministic formulas).
**Pitfalls.** Don't commit Apple screenshots.
**Refs.** Apple Human Interface Guidelines (public web pages, for
behaviour descriptions).

### DS-02 — Interaction guidelines (ojjyOS HIG)
- Phase: P1 · Size: M · Status: todo
- Depends: —
- Files: docs/design/hig.md (new)

**Goal.** Written rules for windows (key/main, sheets, panels), menus
(bar, context, shortcuts, ellipsis convention), toolbars, sidebars,
settings layout, alerts wording, drag and drop, selection, undo,
keyboard shortcuts table (macOS-first with Windows-style alternatives
like Alt-Tab, Win-key = Cmd option), Windows-like Snap behaviour,
error message tone ("plain English, say how to fix").
**Done when.** Doc exists; DS-15 checklist references it.
**Tests.** —
**Pitfalls.** Keep it short enough to be read by AI sessions (< 30 pages).
**Refs.** Apple HIG and Microsoft Fluent guidelines (concepts only).

### DS-03 — Colour tokens
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: design/tokens/colors.toml (new), tools/gentokens.py, generated lib/ojjykit/tokens.h

**Goal.** Semantic colours for light/dark/increased contrast: label
(primary…quaternary), text, placeholder, separator, link, fills
(primary…quaternary), backgrounds (window, under-page, control, grouped),
system colours (blue, green, orange, red, purple, pink, teal, yellow,
gray, graphite) and the 8 accents with hover/pressed variants; glass
tints. One source file generates C headers for kernel UI (until WS-18)
and ojjyKit.
**Done when.** Current kernel UI reads the generated header; no
hard-coded RGB literals remain in new code.
**Tests.** Generator test; contrast checker (WCAG AA for labels on
backgrounds) runs in the suite.
**Pitfalls.** Keep blue Tahoe default exactly as today.
**Refs.** WCAG 2.2 contrast.

### DS-04 — Typography scale
- Phase: P1 · Size: S · Status: todo
- Depends: DS-03
- Files: design/tokens/type.toml

**Goal.** Text styles (Large Title 26, Title 1 22, Title 2 17, Title 3 15,
Headline 13 semibold, Body 13, Callout 12, Subheadline 11, Footnote 10,
Caption 10/10 — adjust after DS-01 measurements) for Inter, line heights,
tracking, monospaced styles (JetBrains Mono), and Dynamic Type scaling
steps (A11Y-03).
**Done when.** Tokens generated; current UI text styles mapped.
**Tests.** Generator test.
**Pitfalls.** Inter at small sizes needs slight positive tracking.
**Refs.** —

### DS-05 — Spacing, radii, control sizes
- Phase: P1 · Size: S · Status: todo
- Depends: DS-01
- Files: design/tokens/metrics.toml

**Goal.** 4-pt spacing grid, margins (window 20, group 16, row 10…),
corner radii (window 16 or round setting; control radii derived by the
concentric rule: inner = outer − inset), control heights (mini 16, small
20, regular 24/28, large 32, extra large 40), toolbar 52, sidebar widths,
hit-target minimums.
**Done when.** Tokens generated and used by UIK-03.
**Tests.** Generator test.
**Pitfalls.** The "Window Corners Standard/Round" setting must keep working.
**Refs.** —

### DS-06 — App icons (Liquid Glass layered icons)
- Phase: P2 · Size: M · Status: todo
- Depends: DS-03
- Files: design/icons/, tools/mkicons.py, sysroot/Applications/*.app

**Context.** Icons are generated bitmaps (`tools/mkicons.py` →
`ui/icons_data.c`).
**Goal.** Icon format: layered vector source (background + up to 3
foreground layers, SVG) rendered with glass highlights at any size, in
light, dark and tinted (accent monochrome) appearances; template + grid
(rounded square "squircle", safe areas); redraw all built-in app icons
in this style; `.app` bundles carry `AppIcon.ojicon` (multi-size PNG +
vector layers).
**Done when.** Dock, Launchpad, Finder show the new icons in all
appearances; Settings > Appearance offers icon style (Default, Dark,
Clear, Tinted).
**Tests.** Generator tests; screenshot gallery.
**Pitfalls.** Must not imitate Apple's actual icons (originals in the same
style).
**Refs.** DS-01.

### DS-07 — "ojjy Symbols" icon set
- Phase: P2 · Size: L · Status: todo
- Depends: DS-04, GFX-02
- Files: design/symbols/*.svg, tools/mksymbols.py, lib/ojjykit/symbols.c

**Goal.** An original vector symbol set (start with ~300: navigation,
files, editing, media, communication, devices, connectivity, weather,
system status) drawn on a common grid, with 9 weights aligned to text
weights and 3 scales, rendered as text-like glyphs (so they align with
labels), multicolour variants for some; replaces bitmap UI icons.
**Done when.** Toolbars/sidebars/menus use symbols; a Symbols browser in
the ojjyKit Gallery (UIK-54) shows all.
**Tests.** Generator validates every SVG; snapshot gallery.
**Pitfalls.** Don't copy SF Symbols shapes; draw originals.
**Refs.** —

### DS-08 — System sounds
- Phase: P3 · Size: S · Status: todo
- Depends: AUD-01
- Files: design/sounds/, /System/Library/Sounds

**Goal.** Original sounds (synthesised in-house, e.g. with a small
generator script): startup chime, alerts (8 selectable), Trash empty,
screenshot shutter, notification, volume tick, keyboard click option;
licence-clean; Settings > Sound picks alert sound.
**Done when.** Sounds play at the right events with the volume setting.
**Tests.** Live test that events trigger playback (audio log lines).
**Pitfalls.** Keep files tiny (Opus/ADPCM).
**Refs.** —

### DS-10 — Original ojjyOS wallpapers (dynamic)
- Phase: P2 · Size: M · Status: todo
- Depends: —
- Files: design/wallpapers/, tools/mkwallpaper*

**Context.** The 17 Tahoe pictures are Apple's and stay local
(`wallpapers/local/`, never committed); the shipped OS needs its own.
**Goal.** At least 8 original wallpapers in the blue Tahoe mood (procedural
or rendered by our own tools, or Jonas's own photos), each with light and
dark variants, plus "dynamic" ones that change with time of day; packed
like the current wallpaper pack.
**Done when.** A clean build without `wallpapers/local/` still has a
beautiful default; Settings shows both sets when the local one exists.
**Tests.** `qemu_wallpapers.py` on a build without local pictures.
**Pitfalls.** Licence notes for any assets used.
**Refs.** —

### DS-11 — Appearance modes, accents, contrast variants
- Phase: P2 · Size: S · Status: todo
- Depends: DS-03
- Files: design/tokens/, Settings > Appearance

**Goal.** Formal rules for Light/Dark/Auto (existing), accent-coloured
elements (selection, focus, primary buttons, toggles, sliders, links),
"multicolour" accent behaviour (apps choose their own), Increase
Contrast and Reduce Transparency variants per token, tinted glass rules.
**Done when.** Every token has values for each variant; tests verify.
**Tests.** Contrast checker across variants.
**Pitfalls.** —
**Refs.** —

### DS-12 — Motion specification
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: docs/design/motion.md, design/tokens/motion.toml

**Goal.** Named spring presets (snappy, smooth, bouncy, interactive drag
release) with parameters, durations for fades, which transitions use
which preset (window open, sheet, popover, menu, Dock magnify, Launchpad,
Mission Control, Space switch, notification banner), Reduce Motion
replacements (cross-fades), gesture-driven animations follow the finger.
**Done when.** Tokens exist; current animations mapped to presets.
**Tests.** —
**Pitfalls.** —
**Refs.** —

### DS-13 — App layout templates
- Phase: P2 · Size: S · Status: todo
- Depends: DS-02, DS-05
- Files: docs/design/templates.md

**Goal.** Standard layouts with measurements: sidebar + content (Finder,
Mail), sidebar + list + detail (Notes), settings (grouped form rows like
Tahoe System Settings), document window with toolbar, utility panel,
inspector, onboarding/welcome screen, empty states.
**Done when.** Doc with our own mock screenshots; app cards reference it.
**Tests.** —
**Pitfalls.** —
**Refs.** —

### DS-14 — Brand: logo, boot, About, credits
- Phase: P2 · Size: S · Status: todo
- Depends: DS-06
- Files: design/brand/, kernel/src/main.c (splash), About app

**Goal.** ojjyOS logo (vector) and wordmark, boot logo, About window hero,
"Created by Jonas Lee" placement rules everywhere (AGENTS.md §8), version
naming per release (4 "Foundation", 5 "Harbor"…), ASCII art kept for the
verbose boot and Terminal `neofetch`-like command.
**Done when.** Logo used in boot splash (BOOT-05), About, setup assistant.
**Tests.** Screenshot gallery.
**Pitfalls.** —
**Refs.** —

### DS-15 — Visual review checklist and release gallery
- Phase: P2 · Size: S · Status: todo
- Depends: DS-02, UIK-50
- Files: docs/design/review-checklist.md, tests/qemu_design_tour.py

**Goal.** Checklist (alignment, spacing tokens, concentric radii, text
truncation, dark mode, focus rings, hit targets, animation smoothness) and
an automated design tour producing a gallery of every app and panel in
light/dark at 2× per release, diffed against the previous release.
**Done when.** Gallery diff runs in the release checklist (QA-40).
**Tests.** The tour.
**Pitfalls.** Flaky pixels: diff with tolerance and review images.
**Refs.** —

---

## ojjyKit — the UI toolkit (D-012)

### UIK-01 — Core: application, run loop, windows, views
- Phase: P2 · Size: L · Status: todo
- Depends: WS-02, GFX-01, IPC-06
- Files: lib/ojjykit/ (new), docs/api/ojjykit.md

**Goal.** C API with refcounted objects: `OKApp` (lifecycle, menus,
dock icon, open-file requests), run loop (ports, timers, frame callbacks,
idle), `OKWindow` (WindowServer surface, title, toolbar area, full-size
content behind glass), `OKView` tree (frame, bounds, layer backing,
needs-display, hit testing, event handlers, cursor rects), drawing
through ojjyDraw at the window's scale.
**Done when.** A sample app with nested views, click handling and redraw
runs at 60 fps; API docs written.
**Tests.** Host-runnable view tree tests (layout/hit-testing without a
display); live sample app test.
**Pitfalls.** Avoid callback spaghetti: consistent ownership rules
documented (who retains what).
**Refs.** D-012.

### UIK-02 — Layout system
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-01
- Files: lib/ojjykit/layout/

**Goal.** Stack layouts (H/V/Z with spacing, alignment, distribution),
grid layout, flexible spacers, padding, min/ideal/max sizes, intrinsic
content size from text and images, layout priorities and compression,
automatic RTL mirroring, safe areas (under toolbars/glass).
**Done when.** Settings window layout is expressed without manual
coordinates and adapts to window size and text size.
**Tests.** Host layout tests with golden frames.
**Pitfalls.** Layout must be O(n) per pass; cache measurements.
**Refs.** —

### UIK-03 — Theme and materials
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-01, DS-03, DS-04, DS-05
- Files: lib/ojjykit/theme.c

**Goal.** Views resolve semantic colours/fonts/metrics from the current
appearance; live switching when Appearance changes; material backgrounds
(glass variants, sidebar, menu, HUD) requested from WindowServer.
**Done when.** Toggling dark mode updates a running sample app instantly.
**Tests.** Snapshot tests per appearance.
**Pitfalls.** —
**Refs.** —

### UIK-04 — Declarative UI description (`.ojui`)
- Phase: P4 · Size: M · Status: todo
- Depends: UIK-02
- Files: lib/ojjykit/markup/, tools/ojui

**Goal.** Optional declarative markup (or C builder DSL) describing view
hierarchies, bindings and actions, loaded at runtime with live reload in
debug builds — faster app writing for AI sessions and Jonas.
**Done when.** Two built-in apps use it for their main windows.
**Tests.** Parser tests (fuzzed).
**Pitfalls.** Keep it small; not a second programming language.
**Refs.** —

### UIK-05 — Accessibility tree
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-01
- Files: lib/ojjykit/a11y/

**Goal.** Every view exposes role, label, value, hint, actions, children,
frame; controls implement it automatically; tree served to the
accessibility service (A11Y-01) over IPC.
**Done when.** An inspector tool lists the tree of any app.
**Tests.** UI automation tests use the tree (UIK-50).
**Pitfalls.** Labels for icon-only buttons are mandatory (lint).
**Refs.** —

### UIK-06 — Responder chain, commands, undo
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-01
- Files: lib/ojjykit/responder.c, undo.c

**Goal.** Key events and commands (copy, paste, undo, select all, find)
travel focus → view → window → app; menu items validate against the
chain (greyed when unavailable); undo manager with grouping and
redo; keyboard shortcut mapping.
**Done when.** Edit menu items work in every text field automatically.
**Tests.** Host tests of chain routing.
**Pitfalls.** —
**Refs.** —

### UIK-07 — Observation and data binding
- Phase: P4 · Size: S · Status: todo
- Depends: UIK-01
- Files: lib/ojjykit/observe.c

**Goal.** Observable model objects with change notifications; bindings
from model properties to control values (two-way), batched per frame.
**Done when.** Settings panes bind directly to the settings store (SVC-10).
**Tests.** Host tests.
**Pitfalls.** Reference cycles.
**Refs.** —

### UIK-10 — Text label
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-02, TXT-04, TXT-07
- Files: lib/ojjykit/controls/label.c

**Goal.** Single/multi-line labels with text styles, colours, truncation
modes, line limit, selectable option, links, attributed runs, symbol
inline images.
**Done when.** Gallery shows all styles; truncation correct in RTL.
**Tests.** Snapshot tests.
**Pitfalls.** Baseline alignment in stacks.
**Refs.** —

### UIK-11 — Buttons
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-03, UIK-10
- Files: lib/ojjykit/controls/button.c

**Goal.** Push (default/accent, bordered, borderless, glass, destructive),
icon-only, capsule, help (?), disclosure, toggle-style; sizes; states
(hover, pressed with glass flex, focused, disabled); default button on
Return and cancel on Esc.
**Done when.** Matches DS-01/DS-05 in gallery screenshots.
**Tests.** Snapshot + interaction tests.
**Pitfalls.** Keep `ui_push_button` look parity during migration.
**Refs.** —

### UIK-12 — Switch and checkbox
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-11
- Files: lib/ojjykit/controls/toggle.c

**Goal.** Tahoe switch with wide glass knob (exists in kernel UI),
checkbox with mixed state; animated with springs; label click toggles.
**Done when.** Gallery + Settings use it.
**Tests.** Snapshot + interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-13 — Radio group
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-12
- Files: lib/ojjykit/controls/radio.c

**Goal.** Vertical/horizontal radio groups with arrow-key navigation.
**Done when.** Gallery.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-14 — Slider
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-11
- Files: lib/ojjykit/controls/slider.c

**Goal.** Continuous/discrete, tick marks, min/max images (volume
speaker icons), glass knob, live/continuous updates, keyboard steps,
vertical variant.
**Done when.** Control Center volume/brightness use it.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-15 — Stepper
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-11
- Files: lib/ojjykit/controls/stepper.c

**Goal.** Up/down stepper with auto-repeat, paired with a number field.
**Done when.** Gallery.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-16 — Segmented control
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-11
- Files: lib/ojjykit/controls/segmented.c

**Goal.** Capsule segmented control with sliding glass pill (exists in
kernel UI), single/multiple selection, icons or text, momentary mode.
**Done when.** Gallery; Finder view switcher uses it.
**Tests.** Snapshot + interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-17 — Text fields
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-10, TXT-11, I18N-03
- Files: lib/ojjykit/controls/textfield.c

**Goal.** Single-line field (placeholder, clear button, formatter for
numbers/dates, validation), secure field (secure input mode, no copy),
search field (magnifier, recent searches, tokens), token field; full
editing (selection, double/triple click, word/line navigation with
Opt/Cmd, emacs-style Ctrl shortcuts like macOS), IME support.
**Done when.** Login, Spotlight, Settings and browser fields use it.
**Tests.** Interaction tests incl. emoji and RTL text.
**Pitfalls.** Secure field must enable WS-04 secure input.
**Refs.** —

### UIK-18 — Multi-line text view
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-17, UIK-24
- Files: lib/ojjykit/controls/textview.c

**Goal.** Rich and plain text editing on TXT-11: soft wrap, styles,
lists, find/replace bar, spelling underline hook (I18N-07), large
documents, line numbers option (Code app).
**Done when.** TextEdit and Notes run on it.
**Tests.** Performance + interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-19 — Pop-up buttons, pull-downs, combo boxes
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-20
- Files: lib/ojjykit/controls/popup.c

**Goal.** Choice menus positioned over the selected item (macOS style),
pull-down action menus, combo box with typing + list.
**Done when.** Gallery; Settings uses pop-ups.
**Tests.** Interaction tests.
**Pitfalls.** Menus near screen edges.
**Refs.** —

### UIK-20 — Menus
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-06, WS-02
- Files: lib/ojjykit/menus/

**Goal.** Menu model (items, separators, submenus, checkmarks, icons,
shortcuts, alternates with Option key, dynamic validation), context menus
(right-click/Ctrl-click), app menus published to the menu bar (SHL-01),
glass menu panels, keyboard navigation and type-to-select.
**Done when.** Every app's menus come from this model.
**Tests.** Interaction tests.
**Pitfalls.** Menu bar ownership is in the shell; protocol via WS.
**Refs.** —

### UIK-21 — List and table views
- Phase: P2 · Size: L · Status: todo
- Depends: UIK-24
- Files: lib/ojjykit/controls/table.c

**Goal.** Virtualized rows (100k rows smooth), columns with resize/
reorder/sort, single/multi selection (Shift/Cmd), inline editing, row
actions on hover, swipe actions with trackpad, drag to reorder and drag
out, alternating rows option, sticky section headers.
**Done when.** Finder list view and Activity Monitor use it with 10k items.
**Tests.** Performance + interaction tests.
**Pitfalls.** Selection anchors with sorting.
**Refs.** —

### UIK-22 — Outline view and sidebar
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-21
- Files: lib/ojjykit/controls/outline.c

**Goal.** Hierarchical rows with disclosure, source-list sidebar style
(sections, icons, badges, glass sidebar material), drag targets.
**Done when.** Finder and Settings sidebars use it.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-23 — Collection (grid) view
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-24
- Files: lib/ojjykit/controls/collection.c

**Goal.** Virtualized grids with variable item sizes, rubber-band
selection, keyboard navigation, animated insert/delete; icon view, photo
grid, Launchpad pages.
**Done when.** Finder icon view and Photos grid use it.
**Tests.** Performance tests (10k thumbnails).
**Pitfalls.** —
**Refs.** —

### UIK-24 — Scroll view
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-01, ANI-01
- Files: lib/ojjykit/controls/scroll.c

**Goal.** Smooth scrolling with precise deltas, momentum and rubber
banding, overlay scrollbars that appear on scroll (always-show option),
scroll-to-top on menu-bar click option, zoom (pinch), scroll edge glass
effect under toolbars.
**Done when.** All scrolling feels like macOS on a trackpad (Jonas test).
**Tests.** Physics host tests; recorded gesture replay.
**Pitfalls.** —
**Refs.** —

### UIK-25 — Split views, sidebars, inspectors
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-02
- Files: lib/ojjykit/controls/split.c

**Goal.** Resizable dividers, collapsible sidebar (toolbar button,
Cmd-Ctrl-S), inspector panel on the right, remembered widths.
**Done when.** Finder, Mail, Notes use it.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-26 — Toolbars
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-11, UIK-16
- Files: lib/ojjykit/controls/toolbar.c

**Goal.** Unified title/toolbar with glass capsule groups (as in today's
Finder), flexible spaces, overflow menu, search field slot, title and
subtitle, customization sheet, toolbar style for full-size content.
**Done when.** Finder and TextEdit toolbars use it.
**Tests.** Snapshot tests at several widths.
**Pitfalls.** —
**Refs.** —

### UIK-27 — Tabs (tab views and window tabs)
- Phase: P4 · Size: M · Status: todo
- Depends: UIK-26, WS-03
- Files: lib/ojjykit/controls/tabs.c

**Goal.** Tab view control; window tabs merged into one window (Finder,
Terminal, TextEdit) with tab bar, drag out to new window, Cmd-T,
Ctrl-Tab.
**Done when.** Finder and Terminal support tabs.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-28 — Alerts, sheets, dialogs, popovers
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-11, WS-03
- Files: lib/ojjykit/controls/alert.c, popover.c

**Goal.** Alerts (icon, title, message, up to 3 buttons, suppression
checkbox), sheets attached to windows, app-modal dialogs, popovers with
arrows and glass, transient/semi-transient behaviour.
**Done when.** All confirmations in apps use them.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-29 — Open and save panels
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-22, UIK-21, UIK-28
- Files: lib/ojjykit/panels/file.c, services/powerboxd

**Goal.** Standard open/save panels (sidebar, browser, search, tags, new
folder, file types filter, preview), running out-of-process
(powerbox) so sandboxed apps only get access to what the user chose.
**Done when.** TextEdit open/save use it under a sandbox profile.
**Tests.** Interaction tests.
**Pitfalls.** Runs as a powerbox process from the start; the sandbox
file grants it hands out become enforced once SEC-11/SEC-21 land.
**Refs.** —

### UIK-30 — Colour picker
- Phase: P4 · Size: S · Status: todo
- Depends: UIK-28
- Files: lib/ojjykit/panels/color.c

**Goal.** Wheel, sliders (RGB/HSB), hex, eyedropper (screen sample with
permission), swatches; colour well control.
**Done when.** Gallery; used by Notes highlights later.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-31 — Font panel
- Phase: P4 · Size: S · Status: todo
- Depends: TXT-08, UIK-28
- Files: lib/ojjykit/panels/font.c

**Goal.** Family/style/size picker with preview.
**Done when.** TextEdit uses it.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-32 — Date and time pickers
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-17
- Files: lib/ojjykit/controls/datepicker.c

**Goal.** Text-style and graphical (calendar/clock) pickers, time zones;
locale-specific formats switch to I18N-04 when it lands.
**Done when.** Calendar event editor uses it.
**Tests.** Interaction tests.
**Pitfalls.** —
**Refs.** —

### UIK-33 — Progress indicators
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-03
- Files: lib/ojjykit/controls/progress.c

**Goal.** Determinate/indeterminate bars, spinners, circular progress;
animations stop when hidden.
**Done when.** Gallery; downloads use it.
**Tests.** Snapshot tests.
**Pitfalls.** —
**Refs.** —

### UIK-34 — Image views
- Phase: P2 · Size: S · Status: todo
- Depends: GFX-03
- Files: lib/ojjykit/controls/imageview.c

**Goal.** Scaling modes (fit, fill, centre), async loading with
placeholders, animated images, symbol images tinted by text colour.
**Done when.** Gallery.
**Tests.** Snapshot tests.
**Pitfalls.** —
**Refs.** —

### UIK-35 — Tooltips
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-10
- Files: lib/ojjykit/controls/tooltip.c

**Goal.** Delayed help tags on hover with glass, accessibility hint
mirroring.
**Done when.** Toolbar buttons show tooltips.
**Tests.** Interaction test.
**Pitfalls.** —
**Refs.** —

### UIK-36 — Badges, tags, chips
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-10
- Files: lib/ojjykit/controls/badge.c

**Goal.** Count badges (Dock, sidebar), coloured tag dots, removable
chips (search tokens).
**Done when.** Gallery.
**Tests.** Snapshot tests.
**Pitfalls.** —
**Refs.** —

### UIK-38 — Disclosure groups and forms
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-02
- Files: lib/ojjykit/controls/form.c

**Goal.** Grouped form rows like Tahoe System Settings (label left,
control right, footnotes), disclosure groups, sections.
**Done when.** Settings panes use it.
**Tests.** Snapshot tests.
**Pitfalls.** —
**Refs.** DS-13.

### UIK-40 — Web view
- Phase: P4 · Size: M · Status: todo
- Depends: UIK-01, WEB-02
- Files: lib/ojjykit/controls/webview.c

**Goal.** Embed the ojjyWeb engine in apps (Help viewer, Mail HTML,
Store pages) in a sandboxed renderer process.
**Done when.** Help viewer shows HTML help pages.
**Tests.** Live test.
**Pitfalls.** Mail must block remote content by default.
**Refs.** —

### UIK-41 — Media player view
- Phase: P6 · Size: S · Status: todo
- Depends: MED-01
- Files: lib/ojjykit/controls/player.c

**Goal.** Video surface with glass transport controls, scrubbing with
thumbnails, full screen, picture-in-picture.
**Done when.** Videos app and ojjyWeb `<video>` use it.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### UIK-43 — Drag and drop and pasteboard
- Phase: P3 · Size: M · Status: todo
- Depends: UIK-01, SVC-12
- Files: lib/ojjykit/dnd.c

**Goal.** Multi-type pasteboard items (text, rich text, images, files,
URLs), drag sessions across apps with live previews and spring-loaded
folders, promised files.
**Done when.** Drag a picture from Photos into Notes; files between
Finder windows.
**Tests.** Live test with synthetic drags.
**Pitfalls.** Sandboxed apps get temporary file access only.
**Refs.** —

### UIK-44 — Keyboard navigation and focus rings
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-06
- Files: lib/ojjykit/focus.c

**Goal.** Tab/Shift-Tab across all controls (option "Keyboard
navigation" in Settings), accent focus rings following shapes,
Space/Return activation.
**Done when.** Every Settings pane is usable without a mouse.
**Tests.** Automation test tabbing through panes.
**Pitfalls.** —
**Refs.** —

### UIK-45 — Localization support
- Phase: P4 · Size: M · Status: todo
- Depends: UIK-10, I18N-01
- Files: lib/ojjykit/l10n.c

**Goal.** String tables per language in bundles, plural rules, formatted
strings with argument order, automatic RTL mirroring of layouts and
symbols, pseudo-localization mode for testing.
**Done when.** One app fully localized to a second language (I18N-10).
**Tests.** Pseudo-loc snapshot test.
**Pitfalls.** —
**Refs.** CLDR plural rules.

### UIK-46 — Settings window template
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-22, UIK-38
- Files: lib/ojjykit/templates/settings.c

**Goal.** Reusable "app settings" window (Cmd-,) with sidebar or tab
layout and search.
**Done when.** Two apps use it.
**Tests.** Snapshot tests.
**Pitfalls.** —
**Refs.** —

### UIK-48 — Document architecture
- Phase: P3 · Size: M · Status: todo
- Depends: UIK-29, FS-15
- Files: lib/ojjykit/document.c

**Goal.** Document objects with open/save/autosave, versions (browse
previous versions using snapshots), recent documents menu, duplicate,
rename in title bar, unsaved indicator, revert.
**Done when.** TextEdit uses it with autosave and versions.
**Tests.** Live tests incl. crash during save.
**Pitfalls.** Atomic saves (write temp + rename).
**Refs.** —

### UIK-49 — State restoration
- Phase: P4 · Size: S · Status: todo
- Depends: UIK-48
- Files: lib/ojjykit/restore.c

**Goal.** Reopen windows (positions, tabs, scroll, selection) after
quit/restart ("Reopen windows when logging back in").
**Done when.** Finder windows come back after reboot.
**Tests.** Live test with reboot.
**Pitfalls.** Corrupt state must not crash apps on launch.
**Refs.** —

### UIK-50 — UI test framework
- Phase: P2 · Size: M · Status: todo
- Depends: UIK-05
- Files: lib/ojjykit/testing/, tests/ui/

**Goal.** Snapshot tests of views rendered headless on the host (CPU
backend), automation via the accessibility tree (find button by label,
click, type, assert), used by every UIK and APP card.
**Done when.** 20 control snapshot tests run in `scripts/test.sh`.
**Tests.** Itself.
**Pitfalls.** Deterministic rendering (fonts, no time-based animation in
snapshots).
**Refs.** —

### UIK-53 — Glass effect API for apps
- Phase: P4 · Size: M · Status: todo
- Depends: WS-07, UIK-03
- Files: lib/ojjykit/glass.c

**Goal.** Apps place glass shapes (capsules, circles, rounded rects) that
WindowServer renders; nearby glass shapes merge with liquid morphing
transitions (like "glass effect containers"), interactive glass reacts to
touches/clicks.
**Done when.** Gallery demo shows merging glass buttons animating.
**Tests.** Snapshot sequences.
**Pitfalls.** Performance with many shapes.
**Refs.** DS-01.

### UIK-54 — ojjyKit Gallery app
- Phase: P2 · Size: S · Status: todo
- Depends: UIK-11
- Files: apps/Gallery

**Goal.** Developer/design app listing every control in every state and
appearance, symbol browser, colour/typography tokens viewer; used for
design reviews and screenshot galleries.
**Done when.** Gallery covers all UIK cards done so far (kept updated).
**Tests.** Design tour includes it.
**Pitfalls.** —
**Refs.** —
