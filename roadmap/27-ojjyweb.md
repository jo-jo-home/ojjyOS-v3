# 27 — ojjyWeb (ChromeOS-style browser) — outline, to be refined

Jonas: "i should also have a browser thats the same as chromeos but called
ojjyWeb. we will work on the ojjyWeb additional information and refining
plan later." This file is therefore an **outline**: the cards set
direction and dependencies so the rest of the roadmap can refer to them;
a dedicated planning session with Jonas will expand them (split into many
smaller cards, add the ChromeOS-specific features he wants).

Starting point (2026-10-19, `BROWSER_CHROME_PLAN.md`): "ojjyOS Web" has an
original engine (`kernel/src/web/`: DOM, CSS cascade with layers and
variables, block/inline/flex/grid/table/float/absolute layout, painter),
images (PNG, GIF, JPEG, WebP, SVG), gzip, cookies, tabs/omnibox/bookmarks/
history/downloads/find/zoom — but **no JavaScript**, runs inside the
kernel, single process.

"Same as ChromeOS" interpreted (to confirm with Jonas): Chrome-like UI and
features, deep shell integration (web apps as Dock apps, browser as the
center of the experience if the user wants), fast, auto-updating,
sandboxed, profiles and sync — on our own engine (real Chrome is not
possible: `BROWSER_CHROME_PLAN.md` §0).

---

### WEB-00 — Planning session with Jonas
- Phase: P2 · Size: S · Status: todo
- Depends: —
- Files: roadmap/27-ojjyweb.md, BROWSER_CHROME_PLAN.md

**Goal.** Interview Jonas (what "same as ChromeOS" means for him: UI,
features, sites he needs, priority of JavaScript vs other features),
then rewrite this file into detailed cards (target: 60+ cards like the
other areas).
**Done when.** This file no longer says "outline".
**Tests.** `roadmap.py check` passes.
**Pitfalls.** —
**Refs.** —

### WEB-01 — ojjyWeb in user space
- Phase: P2 · Size: L · Status: todo
- Depends: NET-10, UIK-01, GFX-03, TXT-04
- Files: apps/ojjyWeb/, lib/libweb/ (from kernel/src/web/, kernel/src/ui/browser*.c)

**Goal.** Move engine and shell out of the kernel (pairs with
APP-PORT-11), rename to ojjyWeb, use sockets/libhttp/libimage/libtext,
float-based layout and the real font engine (no more prerendered
`webfont_data.c`).
**Done when.** Web tour passes from the user-space app; kernel web code
deleted.
**Tests.** `qemu_web_tour.py`, `tests/web_render.c` (host).
**Pitfalls.** Keep host renderer tests green throughout.
**Refs.** BROWSER_CHROME_PLAN.md.

### WEB-02 — Engine as an embeddable library
- Phase: P4 · Size: M · Status: todo
- Depends: WEB-01
- Files: lib/libweb/

**Goal.** Clean engine API (load, navigate, render to surface, input,
accessibility tree, find) used by ojjyWeb, the web view control
(UIK-40), Mail, Help, Store.
**Done when.** Two apps embed it.
**Tests.** Embedding tests.
**Pitfalls.** —
**Refs.** —

### WEB-03 — Multi-process architecture and sandboxing
- Phase: P5 · Size: L · Status: todo
- Depends: WEB-02, SEC-21
- Files: apps/ojjyWeb/

**Goal.** Browser process (UI), renderer process per site (sandboxed:
no files, no network except via the network process), network process,
GPU/compositor helper; crash of one tab doesn't kill others ("Aw, snap"
page).
**Done when.** Killing a renderer shows the crash page only in its tabs.
**Tests.** Live tests.
**Pitfalls.** IPC performance for scrolling.
**Refs.** —

### WEB-10 — Chrome-like shell features
- Phase: P4 · Size: L · Status: todo
- Depends: WEB-01
- Files: apps/ojjyWeb/ui/

**Goal.** (Outline) profiles, tab groups, vertical tabs option, tab
search, reading list, side panel, password manager integration
(APP-PASSWORDS-01), autofill, translate (later), PDF viewer (GFX-08),
download manager, history journeys, settings parity — refined in WEB-00.
**Done when.** To be specified.
**Tests.** UI automation.
**Pitfalls.** —
**Refs.** —

### WEB-20 — JavaScript engine ("ojjyScript")
- Phase: P5 · Size: XL · Status: todo
- Depends: WEB-01, USR-13
- Files: lib/libjs/

**Goal.** Original ECMAScript engine: parser, bytecode compiler,
interpreter, garbage collector (generational), built-ins (Object, Array,
String, RegExp, Promise, Map/Set, JSON, Date, Intl subset), modules,
async/await; later a baseline JIT (needs SEC-21 JIT entitlement).
Must be split before work (expected 20+ cards).
**Done when.** Passes a large part of test262 (subset) and runs common
site scripts.
**Tests.** test262 (as external test data), own tests, fuzzing.
**Pitfalls.** The biggest single component of the browser; plan carefully.
**Refs.** ECMA-262.

### WEB-21 — DOM bindings and Web APIs
- Phase: P6 · Size: XL · Status: todo
- Depends: WEB-20
- Files: lib/libweb/bindings/

**Goal.** DOM/events, timers, fetch/XHR, storage (localStorage,
IndexedDB later), history API, URL, canvas 2D, workers, WebSocket
(NET-20), media elements, clipboard, notifications (permission-gated) —
split before work.
**Done when.** Modern sites (YouTube, Reddit, Google search) are usable.
**Tests.** Web Platform Tests subset.
**Pitfalls.** —
**Refs.** WHATWG standards.

### WEB-22 — Rendering completeness
- Phase: P6 · Size: L · Status: todo
- Depends: WEB-01, GFX-04
- Files: lib/libweb/layout/, paint/

**Goal.** Transforms, transitions/animations, filters, sticky
positioning, columns, writing modes, form controls fidelity, GPU
compositing of layers, `<canvas>`, web fonts (`@font-face` with TXT-*).
**Done when.** To be specified in WEB-00.
**Tests.** WPT rendering subset + host renders.
**Pitfalls.** —
**Refs.** CSS specifications.

### WEB-23 — Media in the browser
- Phase: P6 · Size: M · Status: todo
- Depends: WEB-21, MED-01, MED-40
- Files: lib/libweb/media/

**Goal.** `<video>`/`<audio>`, Media Source Extensions, WebVTT captions,
picture-in-picture.
**Done when.** YouTube videos play (non-DRM).
**Tests.** Live tests.
**Pitfalls.** DRM is a non-goal.
**Refs.** —

### WEB-24 — WebAssembly
- Phase: P7 · Size: L · Status: todo
- Depends: WEB-20
- Files: lib/libjs/wasm/

**Goal.** Wasm decoding, validation, interpreter then JIT, JS API.
**Done when.** Wasm demos and spec tests pass (subset).
**Tests.** Wasm spec test suite (external data).
**Pitfalls.** —
**Refs.** WebAssembly Core Spec.

### WEB-25 — Web compatibility program
- Phase: P6 · Size: M · Status: todo
- Depends: WEB-21
- Files: tests/web/compat/

**Goal.** Tracked list of top sites with status, automated tour with
screenshots and console error counts (extends `qemu_web_tour.py`).
**Done when.** Weekly compat report.
**Tests.** The tour.
**Pitfalls.** —
**Refs.** —

### WEB-26 — Extensions (WebExtensions subset)
- Phase: P8 · Size: L · Status: todo
- Depends: WEB-21
- Files: apps/ojjyWeb/extensions/

**Goal.** Manifest V3 subset (content scripts, popup, storage, tabs,
declarativeNetRequest for ad blocking).
**Done when.** An open-source ad blocker extension works.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### WEB-27 — Installable web apps (ChromeOS-style)
- Phase: P6 · Size: M · Status: todo
- Depends: WEB-21, SHL-27
- Files: apps/ojjyWeb/pwa/

**Goal.** Install sites/PWAs as apps (own window without browser chrome,
Dock/Launchpad icon from the manifest, offline via service workers
later, notifications through notifyd).
**Done when.** A PWA installs and launches from the Dock.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** W3C Web App Manifest.

### WEB-28 — Web security
- Phase: P5 · Size: L · Status: todo
- Depends: WEB-03, NET-02
- Files: lib/libweb/security/

**Goal.** Same-origin policy everywhere, CORS, CSP, mixed-content
blocking, HSTS, cookie SameSite enforcement, safe-browsing style
blocklist (optional), certificate error interstitials, permission
prompts per site.
**Done when.** Security test pages behave like Chrome's.
**Tests.** WPT security subset.
**Pitfalls.** —
**Refs.** Fetch standard, CSP Level 3.

### WEB-29 — Developer tools
- Phase: P7 · Size: L · Status: todo
- Depends: WEB-21
- Files: apps/ojjyWeb/devtools/

**Goal.** Elements inspector (DOM + computed styles), console, network
log, sources/debugger later, performance timeline.
**Done when.** Inspecting a page's styles works.
**Tests.** UI tests.
**Pitfalls.** —
**Refs.** —
