# 14 — Graphics: GPU, WindowServer, drawing, text, animation

Today: everything (window manager, composition, every app) is drawn by the
kernel in `ui/compositor.c` with CPU code in `gfx.c` (no floating point, no
SIMD — the kernel is compiled `-mgeneral-regs-only`), fonts are pre-rendered
bitmaps at fixed sizes (`uifont_data.c`, `webfont_data.c`).

Target (D-010, D-011, D-012): a user-space **WindowServer** composites
shared-memory or GPU surfaces from app processes; apps draw with
**ojjyDraw** and text from a real font engine; Liquid Glass effects run in
the compositor.

**Key platform fact (verify in GPU-00 before relying on it):** Apple's
Virtualization.framework offered GPU acceleration only to macOS guests as of
the last check; other guests get a 2D virtio-gpu. Our primary platform may
therefore have **no GPU acceleration**. Consequences built into this plan:
1. The CPU renderer is a first-class, fast path: moving composition to user
   space unlocks **NEON/SSE SIMD and floating point** and multiple cores —
   that alone is a large speed-up over today's kernel code.
2. GPU backends are added where available (QEMU virgl/Venus builds, real
   PCs), behind the same APIs.

---

## GPU

### GPU-00 — Research: which GPU paths exist on our platforms
- Phase: P2 · Size: S · Status: todo
- Depends: —
- Files: docs/research/gpu-paths.md (new)

**Goal.** A dated research note answering: does VZ (current macOS) offer
virtio-gpu 3D / Venus / paravirtualized GPU to non-macOS guests? Which
QEMU builds on macOS support `virtio-gpu-gl` (virgl via ANGLE) or Venus
(Vulkan via MoltenVK)? What does a real Intel/AMD PC require? Recommend
the GPU-02 backend order; record as decision D-022.
**Done when.** Note committed with sources and a recommendation.
**Tests.** Small experiments (boot QEMU with each device, log offered
features).
**Pitfalls.** Facts change with macOS releases — date everything.
**Refs.** Apple Virtualization docs; virtio spec §5.7 (3D features).

### GPU-01 — Kernel GPU interface
- Phase: P4 · Size: M · Status: todo
- Depends: GPU-00, MEM-11, SYS-08
- Files: kernel/drivers/gpu/core.c, lib/libgpu/kernel_api.h

**Goal.** Driver-independent kernel API: GPU contexts per process, buffer
objects (vm_object-backed), command submission, fences/timeline semaphores
waitable with SYS-08, memory accounting, reset/hang recovery.
**Done when.** A CPU-backend "null GPU" driver passes the API tests.
**Tests.** Host tests of fence logic; live tests.
**Pitfalls.** A hung GPU must never hang the kernel (timeouts + reset).
**Refs.** —

### GPU-02 — virtio-gpu 3D backend (virgl or Venus, per GPU-00)
- Phase: P4 · Size: L · Status: todo
- Depends: GPU-01, DRV-VIRTIO-01
- Files: kernel/drivers/virtio/gpu3d.c, lib/libgpu/virtio/

**Goal.** 3D contexts, resource creation, command streams per the chosen
protocol, blob resources for zero-copy, used by ojjyGPU.
**Done when.** A spinning textured cube demo runs accelerated under the
QEMU build identified by GPU-00.
**Tests.** QEMU test with frame-time measurement.
**Pitfalls.** Capsets differ between hosts; fall back to CPU cleanly.
**Refs.** Virtio spec §5.7; virglrenderer/Venus protocol docs.

### GPU-03 — ojjyGPU library (Vulkan-shaped, small)
- Phase: P4 · Size: L · Status: todo
- Depends: GPU-01
- Files: lib/libgpu/

**Goal.** Devices, queues, buffers, images, samplers, render passes,
pipelines with pre-compiled shaders, descriptor sets, command buffers,
swapchains bound to WindowServer surfaces. Two backends: GPU-02 and the
CPU backend (GPU-05).
**Done when.** WindowServer and ojjyDraw's GPU path run on both backends
with identical output (pixel tests within tolerance).
**Tests.** Host-runnable CPU backend tests; image comparisons.
**Pitfalls.** Keep the API small; don't clone all of Vulkan.
**Refs.** Vulkan 1.3 spec (concepts).

### GPU-04 — Shader toolchain
- Phase: P4 · Size: M · Status: todo
- Depends: GPU-03
- Files: tools/shaders/, lib/libgpu/shaders/

**Goal.** Shaders written once (a small GLSL-like subset) and compiled at
build time to SPIR-V/virgl TGSI for GPU backends and to C/NEON kernels for
the CPU backend (or hand-written equivalents with shared tests).
**Done when.** Liquid Glass blur/refraction shaders exist for both paths.
**Tests.** Pixel-equivalence tests between backends.
**Pitfalls.** Host compilers (e.g. glslang) are build tools only; never
shipped in the OS.
**Refs.** SPIR-V spec.

### GPU-05 — CPU rendering backend with SIMD and threads
- Phase: P4 · Size: L · Status: todo
- Depends: GPU-03, WS-06
- Files: lib/libgpu/cpu/

**Goal.** Tile-based software rasterizer (NEON on ARM, SSE4/AVX2 on x86)
for the ojjyGPU API using all CPU cores: textured quads, gradients, blur,
blend modes — enough for compositor and 2D apps; basic 3D for simple games.
**Done when.** 2880×1800 full-screen composite with 3 glass layers ≤ 8 ms
on 4 cores under VZ.
**Tests.** Benchmark program; pixel tests.
**Pitfalls.** Memory bandwidth is the limit; damage tracking (WS-05)
matters more than raw speed.
**Refs.** —

### GPU-40 — Real GPU acceleration (Intel Xe), split before work
- Phase: P9 · Size: XL · Status: todo
- Depends: DRV-DISP-04, GPU-03
- Files: kernel/drivers/gpu/intel/

**Goal.** 3D/compute submission on Intel integrated GPUs (GuC/execlists,
GTT, render engine), driving ojjyGPU. Split into cards after studying the
public PRMs.
**Done when.** WindowServer composites on the GPU on a real Intel laptop.
**Tests.** Hardware records.
**Pitfalls.** Multi-year effort on its own; only start when P9 needs it.
**Refs.** Intel Graphics PRMs.

### GPU-41 — Vulkan and OpenGL for ported/compat apps
- Phase: P8 · Size: L · Status: todo
- Depends: GPU-03, LNX-30
- Files: compat/gpu/

**Goal.** A Vulkan loader + ICD implemented on ojjyGPU (subset), and an
OpenGL ES 2/3 implementation on top for ported apps and games; used by the
Linux and Windows layers (D3D translation in WIN-60).
**Done when.** A simple open-source GLES game runs.
**Tests.** Conformance subset tests (original test programs).
**Pitfalls.** Scope control: implement what real apps need.
**Refs.** Khronos specs.

---

## WindowServer (user-space compositor)

### WS-01 — WindowServer process and framebuffer ownership
- Phase: P2 · Size: L · Status: todo
- Depends: PROC-02, IPC-05, DRV-IN-01, DRV-DISP-02
- Files: services/windowserver/ (new), kernel/src/ui/compositor.c

**Context.** The compositor is kernel code inside an 8,700-line file
(split by QA-20 first).
**Goal.** A user process that maps the scanout buffer (or virtio-gpu
resources via a display API), receives the input stream, composites
windows and runs the menu bar/Dock/desktop for now (shell pieces move to
separate processes later: SHL-*). The kernel keeps only a minimal
console + panic screen.
**Steps.**
1. Display API: kernel exposes displays (modes, framebuffer/resource
   handles, flush, vsync event) through DRV-DISP-01's interface.
2. Port the compositor modules (from QA-20) into the process, using
   floating point/SIMD where useful.
3. Input arrives over a port; cursor overlay handled by WindowServer.
4. Keep the kernel-hosted UI as a fallback (`safe=1`) until WS-18.
**Done when.** Desktop runs from WindowServer with all existing live UI
tests passing; killing WindowServer restarts it (SVC-01) within 1 s.
**Tests.** All `qemu_*` UI suites, `vz_smoke.py`.
**Pitfalls.** Latency: input → WindowServer → frame must stay < 1 frame.
**Refs.** D-010.

### WS-02 — Surface protocol
- Phase: P2 · Size: M · Status: todo
- Depends: WS-01, IPC-07
- Files: services/windowserver/protocol.ojidl, lib/ojjykit/ws_client.c

**Goal.** Typed protocol: create/destroy window (kind: normal, panel,
menu, popover, sheet, tooltip, desktop, dock), set title/frame/min/max
size, attach buffer (shared memory, double/triple buffered, format and
scale), damage regions, commit, frame callbacks (vsync-aligned),
visibility/occlusion events, key/pointer/scroll/gesture events, focus,
clipboard and drag-and-drop hooks.
**Done when.** A test app process opens a window and animates at 60 fps
with frame callbacks.
**Tests.** Live test app; fuzzed protocol decoders (ojidl).
**Pitfalls.** Never block WindowServer on a slow client (async, drop
frames).
**Refs.** Wayland protocol (concepts only, for comparison).

### WS-03 — Window management policy
- Phase: P2 · Size: M · Status: todo
- Depends: WS-02
- Files: services/windowserver/wm/

**Goal.** Stacking order, focus (click-to-focus, key window vs main
window), move/resize with live resize, minimize/zoom/full screen,
window levels (floating panels, menus above), modal sheets attached to
parents, cascading placement of new windows, restore positions per app.
**Done when.** Behaviour parity with today's kernel compositor plus
multiple windows per app.
**Tests.** UI tour tests.
**Pitfalls.** Keep macOS semantics (DS-02 interaction spec).
**Refs.** —

### WS-04 — Input routing and global shortcuts
- Phase: P2 · Size: M · Status: todo
- Depends: WS-02
- Files: services/windowserver/input/

**Goal.** Keyboard focus, pointer hit-testing with window shapes,
implicit grabs during drags, global shortcut registry (Cmd-Space,
Cmd-Tab, screenshots, Mission Control) handled before apps, secure input
mode (password fields block key loggers/shortcut snooping).
**Done when.** Shortcuts work regardless of focused app; password field
blocks global event taps.
**Tests.** Live tests with QMP key injection.
**Pitfalls.** Key repeat stays in one place (input core).
**Refs.** —

### WS-05 — Damage tracking and frame pacing
- Phase: P2 · Size: M · Status: todo
- Depends: WS-01
- Files: services/windowserver/compose.c

**Goal.** Only recomposite damaged regions; per-display frame clock
(60/120 Hz) with vsync from virtio-gpu flush completion or a timer;
skip frames when nothing changed (idle = 0 CPU).
**Done when.** Typing in TextEdit recomposites only the text rect; idle
CPU near 0%.
**Tests.** Counter probes; perf suite.
**Pitfalls.** Glass layers depend on what is beneath them: damage must
expand through translucent layers.
**Refs.** —

### WS-06 — SIMD multi-core compositing and blur
- Phase: P2 · Size: M · Status: todo
- Depends: WS-01
- Files: services/windowserver/simd/

**Goal.** NEON/SSE kernels for blend, scale, rounded-rect clip, shadow,
and separable Gaussian/box blur; work split across CPUs by tiles.
**Done when.** Window drag with glass Dock and menu bar: frame ≤ 8 ms at
2× under VZ (today ~30–50 ms in kernel code).
**Tests.** Pixel-equivalence vs the old `gfx.c` path; benchmarks.
**Pitfalls.** Alignment; denormals; keep results deterministic for tests.
**Refs.** Arm NEON intrinsics reference.

### WS-07 — Liquid Glass effects in the compositor
- Phase: P4 · Size: L · Status: todo
- Depends: WS-06, DS-01
- Files: services/windowserver/glass/

**Context.** `gfx_liquid_glass()` approximates glass with cached blur and
rims (LG1–LG3).
**Goal.** Real-time Liquid Glass as specified in DS-01: backdrop blur of
live content, lens-like refraction at edges (displacement by a normal
map), specular highlights responding to motion, adaptive tint for
legibility (darker over bright backgrounds), Clear/Tinted variants,
Reduce Transparency fallback; available to apps via surface "materials"
so apps don't render glass themselves.
**Done when.** Side-by-side screenshots with macOS Tahoe reference
images (taken by Jonas) look comparable; 60 fps maintained.
**Tests.** Pixel tests of the shader math; perf suite.
**Pitfalls.** Cost: blur at reduced resolution, cache when backdrop static.
**Refs.** DS-01.

### WS-08 — Window chrome effects: shadows, corners, transitions
- Phase: P2 · Size: M · Status: todo
- Depends: WS-05
- Files: services/windowserver/effects/

**Goal.** Soft dynamic shadows (larger for the key window), continuous
("squircle") rounded corners, open/close/minimize ("genie" or scale)
animations, Space switching slides, full-screen transitions, Reduce Motion
fades.
**Done when.** All window transitions animate at 60 fps.
**Tests.** Animation check suite (`qemu_anim_check.py` style).
**Pitfalls.** —
**Refs.** DS-12.

### WS-09 — Multiple displays
- Phase: P4 · Size: M · Status: todo
- Depends: DRV-DISP-01, WS-03
- Files: services/windowserver/displays.c

**Goal.** Several displays with arrangement, per-display scale (1× next
to 2×), windows moving between displays, menu bar per display, mirroring,
Settings > Displays arrangement editor.
**Done when.** QEMU with two virtio-gpu scanouts shows an extended desktop.
**Tests.** QEMU two-head test.
**Pitfalls.** A window spanning two scales.
**Refs.** —

### WS-10 — Gesture and pointer processing in WindowServer
- Phase: P3 · Size: M · Status: todo
- Depends: WS-04, DRV-IN-01, ANI-01
- Files: services/windowserver/gestures.c

**Goal.** Central processing of multi-touch/trackpad/scroll input before
routing: scroll phases (began/changed/ended/momentum) delivered to apps,
pinch/rotate/swipe recognition hooks used by DRV-IN-04 gestures,
pointer acceleration curves (relative devices), tablet mode for absolute
devices (VMs), coalescing without adding latency.
**Done when.** Apps receive phased scroll events; momentum scrolling works
with precise-scroll devices.
**Tests.** Recorded input traces replayed in host tests.
**Pitfalls.** Keep the pointer-gap metric (`ptr_gap_max_ms`) as good as
today.
**Refs.** —

### WS-11 — Cursors
- Phase: P2 · Size: S · Status: todo
- Depends: WS-01
- Files: services/windowserver/cursor.c, tools/mkcursor.py

**Goal.** Cursor set (arrow, I-beam, hand, resize ×8, crosshair, busy
spinner, not-allowed, drag copy/link), per-app requests, size setting
(accessibility), hardware cursor plane on virtio-gpu.
**Done when.** Cursors change over text fields, links and window edges.
**Tests.** Pointer suite + screenshots.
**Pitfalls.** Keep the 2 px hotspot convention from current tests.
**Refs.** —

### WS-12 — Screenshots and screen recording
- Phase: P3 · Size: M · Status: todo
- Depends: WS-02, SEC-12
- Files: services/windowserver/capture.c, apps/Screenshot

**Goal.** Cmd-Shift-3 (full), Cmd-Shift-4 (area/window with shadow),
Cmd-Shift-5 (toolbar incl. recording) saving PNG to Desktop with a
floating thumbnail; recording to video (MED-20); capture API gated by the
Screen Recording permission.
**Done when.** All three shortcuts work; recording produces a playable file.
**Tests.** Live tests validating saved PNG dimensions.
**Pitfalls.** Exclude password fields? (macOS doesn't; secure input
windows can be excluded via a flag).
**Refs.** —

### WS-13 — Colour management and Night Shift
- Phase: P4 · Size: M · Status: todo
- Depends: WS-05
- Files: services/windowserver/color.c

**Goal.** Surfaces tagged sRGB/Display P3, compositor converts to the
display profile (ICC from EDID or a file), Night Shift warm schedule,
colour filters for accessibility (A11Y-05).
**Done when.** P3 test image looks correct; Night Shift tints at sunset.
**Tests.** Pixel tests of conversion matrices.
**Pitfalls.** Performance: LUTs.
**Refs.** ICC.1 spec.

### WS-15 — Full-screen games and exclusive presentation
- Phase: P6 · Size: S · Status: todo
- Depends: WS-03, GPU-03
- Files: services/windowserver/fullscreen.c

**Goal.** Direct scanout of a full-screen app's buffer (no composition)
when nothing overlaps; low-latency mode for games.
**Done when.** A full-screen game has lower frame time than windowed.
**Tests.** Perf test.
**Pitfalls.** Notifications must still show (fallback to composition).
**Refs.** —

### WS-16 — Surviving WindowServer restarts
- Phase: P5 · Size: M · Status: todo
- Depends: WS-02, SVC-01
- Files: lib/ojjykit/ws_client.c

**Goal.** If WindowServer crashes and restarts, apps reconnect and
re-create their windows (state kept client-side) instead of dying.
**Done when.** Killing WindowServer leaves apps running with their windows.
**Tests.** Live crash test.
**Pitfalls.** Order windows and focus restored sensibly.
**Refs.** —

### WS-18 — Delete the kernel-hosted UI
- Phase: P2 · Size: M · Status: todo
- Depends: WS-03, WS-04, APP-PORT-12
- Files: kernel/src/ui/, kernel/src/gfx.c (kernel copy)

**Goal.** Once every app runs as a process, remove the kernel compositor
and app code; the kernel keeps a text console and the panic screen.
**Done when.** `kernel/src/ui/` is gone; kernel text size drops sharply
(record `llvm-size` before/after); all suites pass.
**Tests.** Full matrix.
**Pitfalls.** Recovery/safe mode must still have a UI (minimal
WindowServer + shell from the initrd).
**Refs.** —

### WS-20 — Virtual displays and remote display API
- Phase: P9 · Size: S · Status: todo
- Depends: WS-12
- Files: services/windowserver/virtual.c

**Goal.** Headless/virtual displays and a stream API for screen sharing
(NET-31) and AirPlay-like casting later.
**Done when.** NET-31 uses it.
**Tests.** Live test.
**Pitfalls.** Permissions.
**Refs.** —

---

## 2D drawing (ojjyDraw) and images

### GFX-01 — ojjyDraw API
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libdraw/ (new), docs/api/ojjydraw.md

**Goal.** Immediate-mode 2D API for apps and ojjyKit: canvas on a surface,
transforms, clip paths, paths (lines, quads, cubics, arcs), fill rules,
strokes (width, caps, joins, dashes), solid/linear/radial/conic gradients,
images with filtering, shadows, blur, blend modes, text runs (via TXT),
opacity layers; float coordinates (user space allows FP).
**Done when.** API documented; demo app draws the full feature set.
**Tests.** Pixel tests against reference PNGs.
**Pitfalls.** Design for both CPU and GPU backends from day one.
**Refs.** HTML Canvas 2D / PDF imaging model (concepts).

### GFX-02 — ojjyDraw CPU rasterizer
- Phase: P2 · Size: L · Status: todo
- Depends: GFX-01
- Files: lib/libdraw/cpu/

**Goal.** Analytic-coverage anti-aliased path rasterizer (sparse scanline
or tile-based) with NEON/SSE span blending, gradient and image shaders,
float math; reuse lessons from `image/svg.c` and `gfx.c`.
**Done when.** Renders the GFX-01 demo and the SVG test suite faster than
the kernel `svg.c` (benchmark).
**Tests.** Pixel tests; fuzzed path inputs (NaN, huge coordinates).
**Pitfalls.** Numerical robustness (degenerate curves).
**Refs.** —

### GFX-03 — Image library (`libimage`) in user space
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libimage/ (from kernel/src/image/)

**Goal.** Move PNG/GIF/JPEG/WebP/SVG decoders to a user-space library
(they are attack surface; decode in sandboxed processes), add encoders
(PNG, JPEG) for screenshots and Photos, animated GIF/WebP/APNG frames,
EXIF orientation, ICC tags, high-quality scaling (mipmaps + Lanczos).
**Done when.** Browser, Preview, Photos, wallpaper code use `libimage`;
kernel copies removed after WS-18.
**Tests.** Existing image vector tests move with the code.
**Pitfalls.** Keep the vector tests pixel-exact.
**Refs.** PNG spec, JPEG (ITU T.81), WebP RFC 9649.

### GFX-04 — ojjyDraw GPU backend
- Phase: P4 · Size: M · Status: todo
- Depends: GFX-02, GPU-03
- Files: lib/libdraw/gpu/

**Goal.** Paths tessellated or rendered with stencil-and-cover/analytic
coverage on ojjyGPU; text from glyph atlases.
**Done when.** Same pixel tests pass within tolerance on GPU backends.
**Tests.** Cross-backend pixel tests.
**Pitfalls.** —
**Refs.** —

### GFX-06 — HEIC and AVIF still images
- Phase: P7 · Size: L · Status: todo
- Depends: GFX-03, MED-22, MED-23
- Files: lib/libimage/heif/

**Goal.** Decode iPhone HEIC photos and AVIF web images using the HEVC
and AV1 intra decoders.
**Done when.** HEIC photos open in Photos/Preview; AVIF images show in
ojjyWeb.
**Tests.** Conformance sample images.
**Pitfalls.** HEVC patent landscape: decoding for personal use; record
decision.
**Refs.** ISO/IEC 23008-12 (HEIF), AVIF spec.

### GFX-08 — PDF rendering engine
- Phase: P6 · Size: L · Status: todo
- Depends: GFX-02, TXT-10
- Files: lib/libpdf/

**Goal.** Parse PDF 1.7/2.0 (xref, object streams, filters: Flate, LZW,
DCT, JPX later), content streams (paths, text with embedded TrueType/
CFF/Type1 fonts, images, shadings, transparency groups), annotations
display, text extraction for search/copy, page thumbnails; used by
Preview, ojjyWeb and printing.
**Done when.** A corpus of 50 varied PDFs renders legibly; fuzzing finds
no crashes.
**Tests.** Corpus pixel comparisons (loose), heavy fuzzing.
**Pitfalls.** PDF is hostile input: sandboxed process only.
**Refs.** ISO 32000-2.

---

## Text and fonts

### TXT-01 — OpenType font parser
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libtext/otf/

**Goal.** Tables: head, hhea, maxp, cmap (formats 4, 12, 14), hmtx, glyf,
loca, name, OS/2, post, kern, GPOS/GSUB headers; validation of every
offset; font collections (TTC).
**Done when.** Inter, JetBrains Mono, Noto Sans load and report metrics.
**Tests.** Host tests; fuzzing with mutated fonts.
**Pitfalls.** Malicious fonts are a classic exploit vector: sandbox +
fuzz.
**Refs.** OpenType spec 1.9.

### TXT-02 — Glyph rasterizer (any size, HiDPI)
- Phase: P2 · Size: M · Status: todo
- Depends: TXT-01, GFX-02
- Files: lib/libtext/raster.c

**Goal.** Quadratic-outline rasterization with exact-area coverage,
gamma-correct blending, optional stem darkening at 1×, no hinting
(HiDPI-first like macOS), subpixel positioning (4 horizontal phases).
**Done when.** Text at any size matches the quality of today's
pre-rendered Inter (screenshots); `uifont_data.c` and `webfont_data.c`
(~130k generated lines) can be deleted after migration.
**Tests.** Pixel tests vs reference renders; performance test (glyphs/s).
**Pitfalls.** Contours with overlaps (non-zero rule).
**Refs.** —

### TXT-03 — Glyph cache and atlases
- Phase: P2 · Size: S · Status: todo
- Depends: TXT-02
- Files: lib/libtext/cache.c

**Goal.** LRU glyph bitmap cache per (font, size, subpixel phase, scale),
GPU atlas upload hooks, shared across threads.
**Done when.** Scrolling a long page re-rasterizes no glyphs (counter).
**Tests.** Host tests.
**Pitfalls.** Memory cap.
**Refs.** —

### TXT-04 — Text shaping (simple scripts)
- Phase: P2 · Size: M · Status: todo
- Depends: TXT-01
- Files: lib/libtext/shape.c

**Goal.** Original shaping engine: cmap mapping, GSUB ligatures (liga,
calt), GPOS kerning and mark positioning for Latin/Greek/Cyrillic,
OpenType feature selection (tnum for clocks, ss0x), clusters for caret
movement.
**Done when.** "fi"/"ffi" ligatures and kerning pairs render correctly;
tabular numbers in the menu bar clock.
**Tests.** Golden shaping tests (glyph IDs + advances) for sample strings.
**Pitfalls.** Lookup type coverage — implement what Inter/Noto use.
**Refs.** OpenType GSUB/GPOS chapters.

### TXT-05 — Complex scripts (Arabic, Hebrew, Indic, Thai)
- Phase: P6 · Size: L · Status: todo
- Depends: TXT-04, TXT-06
- Files: lib/libtext/shape_complex/

**Goal.** Arabic joining and marks, Hebrew marks, Indic reordering
(Devanagari first), Thai/Lao line breaking dictionaries.
**Done when.** Sample sentences match reference renderings from the Mac
(glyph sequences compared).
**Tests.** Golden tests.
**Pitfalls.** Script-specific state machines are large — one script per
sub-step.
**Refs.** Microsoft OpenType script development specs (public).

### TXT-06 — Bidirectional text (UAX #9)
- Phase: P6 · Size: M · Status: todo
- Depends: TXT-13
- Files: lib/libtext/bidi.c

**Goal.** Unicode Bidirectional Algorithm with paragraph levels, mirroring,
used by text layout, editors and ojjyWeb.
**Done when.** Passes the official BidiTest/BidiCharacterTest files.
**Tests.** Unicode conformance files.
**Pitfalls.** —
**Refs.** UAX #9.

### TXT-07 — Line breaking, hyphenation, justification
- Phase: P2 · Size: M · Status: todo
- Depends: TXT-13
- Files: lib/libtext/linebreak.c

**Goal.** UAX #14 line breaking, optional hyphenation (pattern dictionaries
for English first), justification with word spacing, ellipsis truncation
(head/middle/tail).
**Done when.** Passes LineBreakTest.txt; labels truncate with "…" correctly.
**Tests.** Unicode conformance file.
**Pitfalls.** —
**Refs.** UAX #14.

### TXT-08 — Font management and fallback (`fontd`)
- Phase: P3 · Size: M · Status: todo
- Depends: TXT-01, SVC-01
- Files: services/fontd/

**Goal.** System font registry (families, styles, weights, coverage
bitmaps), per-script fallback chains (Inter → Noto Sans → Noto CJK →
Noto Emoji), user-installed fonts in `~/Library/Fonts`, activation for
apps, Font Book app back end.
**Done when.** Mixed English/Japanese/emoji text renders without tofu
boxes (with fonts installed).
**Tests.** Live test with mixed text.
**Pitfalls.** CJK fonts are large: load on demand, ship as optional
packages.
**Refs.** —

### TXT-09 — Colour emoji
- Phase: P3 · Size: M · Status: todo
- Depends: TXT-02
- Files: lib/libtext/color.c

**Goal.** COLRv0/v1 (layered vector colour glyphs) and CBDT bitmap emoji,
ZWJ sequences and skin tones via shaping, emoji picker (SHL-30).
**Done when.** Noto Color Emoji (OFL) renders sequences correctly.
**Tests.** Golden tests.
**Pitfalls.** COLRv1 gradients and transforms.
**Refs.** OpenType COLR/CPAL, CBDT.

### TXT-10 — CFF/CFF2 outlines and variable fonts
- Phase: P3 · Size: M · Status: todo
- Depends: TXT-02
- Files: lib/libtext/cff.c, lib/libtext/var.c

**Goal.** PostScript (CFF/CFF2) outlines and variable font axes (fvar,
gvar, avar, HVAR) so Inter's variable version gives every weight.
**Done when.** Weight slider demo animates smoothly through Inter weights.
**Tests.** Golden outline tests.
**Pitfalls.** Charstring interpreter must bound stack/recursion.
**Refs.** CFF2 spec; OpenType font variations overview.

### TXT-11 — Text layout engine for editors
- Phase: P2 · Size: L · Status: todo
- Depends: TXT-04, TXT-07
- Files: lib/libtext/layout/

**Goal.** Paragraph/line layout with styles (font, size, colour,
underline, links), caret positioning by grapheme clusters, selection
rectangles, hit testing, incremental relayout for large documents,
tab stops, used by text fields, TextEdit, Notes, Code, Terminal.
**Done when.** TextEdit edits a 5 MB file with smooth typing (< 5 ms per
keystroke).
**Tests.** Host tests (caret movement, selection), performance test.
**Pitfalls.** Grapheme clusters and emoji sequences in caret logic.
**Refs.** UAX #29.

### TXT-13 — Unicode data library (`libunicode`)
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libunicode/, tools/genunicode.py

**Goal.** Generated tables from Unicode Character Database: general
category, case mapping/folding, normalization (NFC/NFD/NFKC), grapheme/
word/sentence segmentation (UAX #29), East Asian width, emoji properties,
script property; compact two-stage tables.
**Done when.** Passes NormalizationTest and GraphemeBreakTest.
**Tests.** Unicode conformance files.
**Pitfalls.** Keep tables compact (< 300 KB).
**Refs.** Unicode 16 UCD, UAX #15, #29.

---

## Animation

### ANI-01 — Animation engine (springs, curves, timelines)
- Phase: P2 · Size: M · Status: todo
- Depends: WS-02
- Files: lib/ojjykit/animation/, services/windowserver/animation.c

**Goal.** Spring physics (mass/stiffness/damping or response/damping-ratio
parameters like SwiftUI), cubic Bézier curves, keyframes, interruptible
animations that retarget smoothly mid-flight, driven by display frame
callbacks; shared presets from DS-12.
**Done when.** Dock magnification and window open/close use springs and
can be interrupted without jumps.
**Tests.** Host tests of spring integration (stable at any frame rate).
**Pitfalls.** Frame-rate independence; stop animations when not visible.
**Refs.** DS-12.

### ANI-02 — Implicit view animations in ojjyKit
- Phase: P4 · Size: M · Status: todo
- Depends: ANI-01, UIK-01
- Files: lib/ojjykit/animation/implicit.c

**Goal.** Changing a view property inside an animation block animates it
(frame, opacity, colour, corner radius, glass intensity); layout changes
animate (list insert/delete, expanding sections).
**Done when.** Settings sidebar selection and Finder list changes animate.
**Tests.** Host tests of interpolation; UI snapshot sequences.
**Pitfalls.** Performance with many views — batch per frame.
**Refs.** —

### ANI-03 — Performance HUD and frame diagnostics
- Phase: P2 · Size: S · Status: todo
- Depends: WS-05
- Files: services/windowserver/hud.c

**Goal.** Toggleable overlay (Cmd-Opt-Shift-F or `defaults` key) showing
FPS, frame time graph, damaged region flashes, CPU per stage — like
macOS Quartz Debug.
**Done when.** HUD works and is used in perf reports.
**Tests.** Screenshot test.
**Pitfalls.** HUD must not perturb timing much.
**Refs.** —
