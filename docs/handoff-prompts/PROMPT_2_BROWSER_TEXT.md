You are working on ojjyOS v3, a hobby operating system written from scratch by Jonas Lee (custom kernel, no Linux/BSD code). You have no prior context; everything you need is in the repository. Work fully autonomously (do not ask questions) and finish the task end to end. Another AI may be working at the same time on branch native-vm (Apple Virtualization.framework app): do NOT touch kernel/src/arch/, kernel/src/main.c, boot/, vmapp/ or scripts/run-qemu.sh, and do not switch that branch.

PROJECT LOCATION
~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3   (Apple M2 Pro Mac, macOS 26, QEMU 11.1.2 via Homebrew, Homebrew LLVM at /opt/homebrew/opt/llvm, Python 3 with Pillow)

THE TASK: web pages in the built-in browser must use the proportional Inter font
The desktop UI was just redesigned with the Inter typeface (anti-aliased, 1x and 2x HiDPI), but web pages in the browser app still render in the old 8x16 monospace cell font, which looks dated and wastes space. Make page text render in Inter with correct proportional layout: line wrapping, links, bold, headings, code, forms and hit-testing must all use real text widths.

READ FIRST (completely)
1. AGENTS.md (project rules; ignore its START HERE block, that is the other AI's job), RENDERER_UI_PLAN.md (how the new renderer works; this task is its "Phase 4.3").
2. kernel/src/web/html.h and html.c - the one-pass HTML layout engine. Today it measures everything in fixed cells: WEB_CELL_W 8, WEB_CELL_H 16, WebRun has x, y, text offset, len (characters), style flags WS_BOLD/WS_LINK/WS_HEADING/WS_MUTED/WS_CODE/WS_ITALIC/WS_BUTTON/WS_FIELD and scale 1 or 2 (headings). Uses of cell widths: cell(), newline() (line_h), text_char() wrapping and tabs, pending_space, list indents (~line 544), table cells (~583), button and input box widths (~656, 675, 806, 833, 1006), doc height (1017), web_first_run_at and web_link_at hit tests (~1026-1036).
3. kernel/src/uifont.h and uifont.c - the Inter text API: text_draw_n(surface, x, y, str, max_bytes, style, color, alpha), text_width_n(str, max_bytes, style), text_line_height(style), text_ascent(style), text_y_center(). Styles: TEXT_SMALL(11px), TEXT_SMALL_BOLD, TEXT_BODY(13), TEXT_BODY_MEDIUM, TEXT_BODY_BOLD, TEXT_TITLE(15 SemiBold), TEXT_HEADLINE(20 Bold), TEXT_LARGE(28), TEXT_DISPLAY(clock digits only - never use for pages). Widths are in logical pixels; HiDPI is handled inside (text_set_density).
4. kernel/src/ui/browser.c - draws the display list (fb_draw_text_scaled for runs around line 617, draw_field for inputs/buttons, page_x/page_y, PAD, scrolling, the "[WEB] view x,y wxh page-origin ..." serial receipt that tests depend on).
5. tests/test_web.c (54 host checks of the layout engine) and tests/qemu_browser.py (live test; clicks links at positions computed from run positions logged by the kernel - read it to see what it relies on).
6. tools/mkuifont.py - generates kernel/src/uifont_data.c (character set ASCII + extras). Pages contain other characters (accented Latin, curly quotes, etc.).

DESIGN YOU MUST IMPLEMENT
A. Measurement callback, so html.c stays host-testable and independent of the framebuffer:
   - In html.h add: typedef int (*WebMeasureFn)(const char *s, int len_bytes, uint8_t style, uint8_t scale); and void html_set_measure(WebMeasureFn fn, int line_height_px, int code_cell_px); Default (NULL) = today's fixed 8x16 cells so every existing behaviour and test still works.
   - Every place that advances x or computes a width must call the measure function for the actual text (word widths, space width, tab stops, list markers, button/input widths derived from label text, table cell gaps). Line height for a line = max over the runs on it (normal text vs headings).
   - Wrapping: keep the current word-based algorithm but use measured word widths; break a single over-long word (URLs) by measuring prefixes.
   - WS_CODE runs (<code>, <pre>) stay monospace (JetBrains Mono, use FONT_WIDTH * scale) so code alignment is kept; <pre> must keep exact spacing.
B. Style mapping in browser.c (or a new kernel/src/ui/webfont.c): normal -> TEXT_BODY; WS_BOLD -> TEXT_BODY_BOLD; h1 (scale 2) -> TEXT_LARGE; h2/h3 headings -> TEXT_HEADLINE / TEXT_TITLE (look at how html.c marks heading level; extend WebRun with a heading level byte if needed); WS_MUTED -> TEXT_SMALL; links -> TEXT_BODY with accent colour + underline (keep current underline drawing, but it must span the measured width); italic has no Inter italic face - keep the current colour treatment.
C. Characters: the layout engine transliterates to ASCII today. Change it to keep UTF-8 for runs, and extend tools/mkuifont.py EXTRAS with Latin-1 letters (U+00C0-U+00FF), curly quotes (U+2018/2019/201C/201D), en/em dash, ellipsis, euro, trademark, nbsp; regenerate kernel/src/uifont_data.c with python3 tools/mkuifont.py kernel/src/uifont_data.c; anything still missing falls back to '?' (already handled in uifont.c lookup). Keep the kernel image small (check: llvm-size build/dev-aarch64/kernel.bin, the font data must not exceed ~1.2 MB).
D. Hit-testing (web_link_at, web_box_at, web_first_run_at, selection if any) must use the stored measured run width: add int16_t w and h to WebRun (filled during layout) instead of len * WEB_CELL_W.
E. Performance: measure each word once during layout (layout runs on page load and on window resize, not per frame). Drawing must only draw visible runs (already does).

TESTS YOU MUST ADD/KEEP
- tests/test_web.c: keep all existing checks passing with the default fixed measurer; add a fake proportional measurer (e.g. 'i'/'l' = 3 px, 'm'/'w' = 11 px, others 7 px, bold +1 px per char) and add checks: wrapping happens at the measured width, a long URL breaks inside the word, link hit-testing at the measured end of a link works and one pixel after it does not, <pre> keeps monospace columns, heading line height is larger, button width follows its label.
- Run sanitizers: HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 bash scripts/test.sh
- Live: build a dev image and run
  OJJY_BUILD_DIR=$PWD/build/dev-aarch64 ARCH=aarch64 bash scripts/build.sh
  python3 -B tests/qemu_browser.py --image build/dev-aarch64/ojjyos3.img --output build/web-inter-1
  OJJY_ACCEL=hvf OJJY_DISPLAY="virtio-gpu-device,xres=2880,yres=1800" python3 -B tests/qemu_browser.py --image build/dev-aarch64/ojjyos3.img --output build/web-inter-2x
  (each run needs a NEW output folder). If qemu_browser.py computes click positions from cell maths, update it to use the logged run positions; never weaken what it checks.
- Look at the screenshots yourself (the built-in start page, the local test page, a real site such as http://68k.news/ or https://en.wikipedia.org/wiki/Operating_system with --internet) and confirm: no overlapping words, no text past the right edge, links underlined exactly under their text, headings clearly bigger, code blocks monospaced.
- Also run tests/qemu_smoke.py (aarch64, --fresh-data) and tests/qemu_apps_check.py to be sure nothing else broke.

HARD RULES
- Work on a new branch from main: git checkout main && git checkout -b browser-inter
- Never git push. Commit locally with: git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit -m "..." ending with the line: Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
- Stage files explicitly (iCloud makes "file 2.o" duplicates; never commit ojjyos-v3-bugs-macos-features-custom-kernel.json).
- Both kernels (aarch64 and x86_64, build dirs build/dev-aarch64 and build/dev-x86_64) must build with 0 warnings and bash scripts/test.sh must pass before every commit.
- Do not overwrite build/aarch64/ojjyos3.img or build/x86_64/ojjyos3.img (Jonas's disks); use the dev build dirs only.
- Keep the blue Tahoe theme and "Created by Jonas Lee" credits; no AI names in the product.
- Commit in small steps (A, then D, then B, then C, then tests), each green.

WHEN DONE
- Append a "Phase 4.3 done" entry to RENDERER_UI_PLAN.md (what changed, tests, screenshot folders).
- Do NOT merge into main yourself if branch native-vm has also changed kernel/Makefile in a conflicting way; otherwise merge browser-inter into main.
- Reply to Jonas with a short plain-English summary: what pages look like now, what was tested, anything left. Nothing pushed.
