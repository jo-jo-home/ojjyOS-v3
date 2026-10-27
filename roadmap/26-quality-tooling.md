# 26 — Quality, tooling, releases and documentation

The infrastructure that keeps a five-year project healthy: tests, CI on
Jonas's Mac, fuzzing, performance tracking, code organisation, releases
and docs. Several P0 cards here unblock everything else (splitting
`compositor.c`, cleaning the tree).

---

## Code organisation (P0–P1)

### QA-20 — Split `compositor.c` into modules (no behaviour change)
- Phase: P0 · Size: L · Status: todo
- Depends: —
- Files: kernel/src/ui/compositor.c (~8,700 lines) → kernel/src/ui/{wm,desktop,menubar,dock,launchpad,spotlight,missioncontrol,controlcenter,login,setup,apps/*}.c

**Context.** One giant file holds the window manager, shell pieces and
every app; parallel AI tracks collide in it and later moves (WS-01,
APP-PORT-*) need clean seams.
**Goal.** Mechanical split into cohesive files with small internal
headers (`ui/internal.h`), shared state made explicit, no behaviour or
pixel change.
**Steps.**
1. Map the file (functions per feature) — write the map into the card.
2. Move one feature per commit (apps first, then shell pieces, WM last),
   building both arches each time.
3. Keep every live UI test green after each move.
**Done when.** No `ui/` file over 2,000 lines; screenshots identical
(pixel diff 0) to before on the design tour.
**Tests.** Full live UI suite + design tour pixel comparison.
**Pitfalls.** Coordinate with any parallel track editing `compositor.c`
(PARALLEL_PLAN.md ownership) — do it when no other track is in the file.
**Refs.** —

### QA-21 — Source tree reorganisation (D-018 layout)
- Phase: P1 · Size: M · Status: todo
- Depends: QA-20
- Files: kernel/src/* → kernel/{arch,core,mm,fs,block,net,drivers,security}/

**Goal.** Move x86 files (`gdt.c`, `idt.c`, `paging.c`, `entry.asm`,
`serial.c`, `timer.c`) to `kernel/src/arch/x86_64/`, create the D-018
directories as code moves into them, update Makefiles and docs (AGENTS.md
§2 layout), keep `git log --follow` history.
**Done when.** Both builds 0 warnings; AGENTS.md layout updated.
**Tests.** Full matrix.
**Pitfalls.** Do in one focused session; tell parallel tracks.
**Refs.** D-018.

### QA-22 — Repository hygiene
- Phase: P0 · Size: S · Status: doing
- Depends: —
- Files: .gitignore, kernel/Makefile, tools/Makefile, scripts/mkfs.sh, scripts/clean-icloud-dupes.sh (new), kernel/src/**/*.o, *.d, "* 2.*" files

**Context.** Build products (`*.o`, `*.d`) appear inside `kernel/src`,
iCloud conflict copies (`tcp 2.d`, `http 2.o`, `uifont 2.o`) accumulate,
and `tools/mkojfs` (a built binary) shows as modified in git status.
(The tools were untracked on 2026-10-26; they still BUILD into `tools/`.)
**Steps.**
- [x] `kernel/Makefile`: objects + dep files under `../build/obj-<arch>/`
      (mkdir in the rule), per-arch dirs so builds never mix; `clean`
      removes the obj dirs plus the legacy in-src `*.o`/`*.d`.
      Test: both dev builds 0 warnings; `bash scripts/test.sh`.
      Trap: Makefile recipes need TABs; `kernel.bin` stays where
      `scripts/build.sh` copies it from.
      — done 2026-10-27 (test: `ARCH=aarch64` + `ARCH=x86_64` full builds exit 0 with 0 warnings, twice each (build/qa-22-{arm,x86}-build{1,2}.log); `bash scripts/test.sh` passes; after `make clean`, `find kernel/src -name '*.o' -o -name '*.d'` is empty)
- [x] `tools/Makefile` builds into `../build/tools/`; `scripts/mkfs.sh`
      calls mkojfs/mkicon/mkarray from there.
      Test: `bash scripts/mkfs.sh` from a clean tools dir; full builds.
      Trap: mkfs.sh also runs from `scripts/build.sh` — absolute paths.
      — done 2026-10-27 (test: mkfs ran inside both full builds; `build/tools/` holds the four binaries; the stale `tools/mk*` binaries from the old layout are deleted, `tools/` holds sources only)
- [x] New `scripts/clean-icloud-dupes.sh`: lists `* <digit>.*` conflict
      copies; `--yes` deletes only UNTRACKED duplicates of build-output
      extensions (.o .d .bin .elf .efi .obj .a .lib .img) — never sources,
      documents or tracked files. Run it (with --yes) in this worktree.
      Test: run in the worktree; deletion list reviewed first.
      Trap: never delete Jonas's files outside build products.
      — done 2026-10-27 (test: list mode found exactly the 4 stale copies (`netcmd 2.o`, `tcp 2.d`, `http 2.o`, `uifont 2.o`) and ignored planted `Note 2.txt`/`src 2.c`; `--yes` then deleted those 4 untracked build products)
- [x] `.gitignore`: extend the iCloud-duplicate patterns to cover every
      build-output extension that appears with a " N" suffix.
      Test: `git check-ignore` matches the real conflict copies.
      — done 2026-10-27 (test: `git check-ignore 'kernel/src/net/tcp 2.d'` matches via `* [0-9].d`; new `.elf/.a/.lib/.img` and two-digit patterns added)
- [ ] Full-build hygiene proof: build both arches twice, then
      `git status --porcelain` must be empty and no `*.o`/`*.d` may
      remain inside `kernel/src`. Evidence `build/qa-22-*`.

**Goal.** Builds write objects only under `build/`; `.gitignore` covers
build products and iCloud duplicates; remove tracked binaries (rebuild
`tools/mkojfs` from source in the build); a `scripts/clean-icloud-dupes.sh`
that lists (never auto-deletes without `--yes`) conflict copies.
**Done when.** `git status` is clean after a full build.
**Tests.** Build twice, check `git status --porcelain` is empty.
**Pitfalls.** Never delete Jonas's files outside build products.
**Refs.** —

### QA-23 — Coding style and formatter
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: .clang-format, docs/style.md

**Goal.** Document the existing style (4 spaces, K&R braces, snake_case,
file header with Jonas credit) and a `.clang-format` matching it, applied
only to new/moved files (no mass reformat of history).
**Done when.** Style doc + config committed; AGENTS.md points to it.
**Tests.** Format check script for changed files.
**Pitfalls.** —
**Refs.** —

---

## Testing infrastructure

### QA-01 — Test strategy document
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: docs/testing.md

**Goal.** Describe the testing ladder (00-how-to-work.md §6) in depth:
how to write host tests (shims like `test_support.h`), live tests (`VM`
helper in `qemu_smoke.py`), VZ tests, pixel gates, fixtures, flake
handling, how to choose test levels per card.
**Done when.** Doc exists; referenced by AGENTS.md.
**Tests.** —
**Pitfalls.** —
**Refs.** —

### QA-02 — Local CI on the Mac
- Phase: P1 · Size: M · Status: todo
- Depends: SDK-01
- Files: tools/ci/, ~/Library/LaunchAgents (installed by script, opt-in)

**Goal.** `ojjy ci` runs the full matrix (builds, host tests, sanitizers,
TCG/HVF/x86 smokes, UI tours, VZ smoke) on `main` nightly when the Mac is
idle and on demand; results as an HTML report in `build/ci/<date>/` with
pass/fail history; notification on failure. Never runs while Jonas's VM
uses the disks (lock check).
**Done when.** A week of nightly reports exists.
**Tests.** The CI runner's own dry-run mode.
**Pitfalls.** Host load from Jonas's other activities — record load in
reports.
**Refs.** —

### QA-03 — Flake tracking
- Phase: P2 · Size: S · Status: todo
- Depends: QA-02
- Files: tools/ci/flakes.py

**Goal.** Record per-test pass/fail history; flag tests failing
intermittently; quarantine list with owners (cards) to fix them.
**Done when.** Flake report section in CI HTML.
**Tests.** —
**Pitfalls.** Quarantined tests must have a fix card.
**Refs.** —

### QA-04 — Coverage-guided fuzzing
- Phase: P1 · Size: M · Status: todo
- Depends: NET-91
- Files: tests/fuzz/ (libFuzzer targets), tools/ci/fuzz.sh

**Goal.** libFuzzer (Homebrew LLVM) targets for every parser (network,
images, fonts, filesystems, ELF/PE, ACPI/FDT, USB descriptors, CSS/HTML,
media containers), seed corpora in the repo (small), nightly fuzzing
budget with crash triage to cards.
**Done when.** 20 fuzz targets run nightly.
**Tests.** Targets build in `scripts/test.sh` (short runs).
**Pitfalls.** Keep found crash inputs as regression tests.
**Refs.** LLVM libFuzzer docs.

### QA-05 — Performance regression suite
- Phase: P2 · Size: M · Status: todo
- Depends: QA-02, ANI-03
- Files: tests/perf/, tools/ci/perf.py

**Goal.** Tracked metrics with thresholds: boot to login (VZ), frame time
for window drag/resize/glass (HVF), app launch time, IPC latency, disk
throughput, network throughput, memory at idle; graphs over time.
**Done when.** Perf section in CI reports with trend charts.
**Tests.** The suite.
**Pitfalls.** Quiet-host discipline; repeat runs and use medians.
**Refs.** BETTER_OS_PLAN.md §5.

### QA-06 — Automatic failure evidence
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: tests/qemu_smoke.py (VM helper)

**Goal.** On any live-test failure: save serial log, last screenshots,
QMP state, kernel log ring (via pmemsave if needed) and a short summary
into the output directory automatically.
**Done when.** A forced failure produces a complete evidence folder.
**Tests.** Forced-failure test.
**Pitfalls.** —
**Refs.** —

### QA-07 — Code coverage for host tests
- Phase: P2 · Size: S · Status: todo
- Depends: QA-02
- Files: scripts/test.sh (coverage mode)

**Goal.** `OJJY_COVERAGE=1 bash scripts/test.sh` produces per-file line
coverage (llvm-cov) in the CI report.
**Done when.** Coverage report generated.
**Tests.** —
**Pitfalls.** —
**Refs.** —

### QA-08 — Static analysis
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: tools/ci/static.sh

**Goal.** clang static analyzer (scan-build) and selected clang-tidy
checks (bugprone, cert, security) on kernel and libraries; zero new
findings policy with a baseline file.
**Done when.** Runs in CI with baseline.
**Tests.** —
**Pitfalls.** False positives: annotate, don't silence globally.
**Refs.** —

### QA-09 — Soak and stress tests
- Phase: P2 · Size: M · Status: todo
- Depends: SMP-03
- Files: tests/qemu_soak.py

**Goal.** Long runs (2 h nightly, 24 h weekly) with random UI actions,
app launches/quits, file operations, network downloads, memory pressure;
asserts no panics, leaks (memory stable), or growing latency.
**Done when.** Weekly 24 h soak passes.
**Tests.** The soak.
**Pitfalls.** —
**Refs.** —

### QA-10 — VZ test harness v2
- Phase: P2 · Size: M · Status: todo
- Depends: HW-10, DRV-VIRTIO-07
- Files: vmapp/, tests/vz_*.py

**Goal.** Structured control channel (virtio-console port 2) for tests
under VZ: inject input, read guest state, capture frames, without
depending on window focus; makes VZ tests as scriptable as QEMU's.
**Done when.** UI tours run under VZ.
**Tests.** VZ tours.
**Pitfalls.** Keep normal launches free of test hooks.
**Refs.** NATIVE_VM_PLAN.md.

---

## Releases, bugs and docs

### QA-30 — Bug tracking process
- Phase: P0 · Size: S · Status: todo
- Depends: —
- Files: bugs/README.md, bugs/OPEN.md

**Goal.** Lightweight tracker in the repo (one line per bug with ID,
symptom, steps, area, severity, linked card) until Jonas moves to GitHub
issues; AI sessions add bugs they find but don't fix.
**Done when.** File exists and is referenced from AGENTS.md.
**Tests.** —
**Pitfalls.** —
**Refs.** —

### QA-40 — Release process and checklist
- Phase: P0 · Size: S · Status: todo
- Depends: —
- Files: docs/release.md, RELEASE.md

**Goal.** Steps for each release: freeze, full matrix + soak, security
review (SEC-02), design gallery diff (DS-15), version bump (main.c,
About, mkfs.sh `version.txt`, splash), What's New page in `mkfs.sh`,
changelog, installed images rebuilt with data-hash verification (no VM
running), `ojjyOS.app` rebuilt, git tag (local), summary for Jonas.
**Done when.** Checklist used for 3.2.
**Tests.** —
**Pitfalls.** In-OS pages are generated by `scripts/mkfs.sh` (edit
there).
**Refs.** AGENTS.md §8.

### QA-41 — Release ojjyOS 3.2
- Phase: P0 · Size: M · Status: todo
- Depends: SHL-02, SHL-03, SHL-04, APP-TERM-01, APP-FINDER-01, APP-TEXTEDIT-01, APP-NOTES-01, APP-CAL-01, APP-SET-01, NET-02, TIM-04, QA-40
- Files: DESKTOP_PLAN.md D9, NET_WEB_PLAN.md

**Goal.** Integrate both P0 tracks, run the release checklist, deliver
installed images, publish What's New ("notifications, snapping, Terminal
themes, polished apps, verified HTTPS").
**Done when.** QA-40 checklist complete; version shows 3.2.0.
**Tests.** Full matrix.
**Pitfalls.** —
**Refs.** —

### DOC-01 — Phase-end documentation refresh
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: README.md, ARCHITECTURE.md, AGENTS.md, roadmap/02-current-state.md

**Goal.** At the end of each phase: update README/ARCHITECTURE, refresh
`02-current-state.md`, prune AGENTS.md historical sections into
`docs/history/` so the handoff stays readable (< 600 lines).
**Done when.** Done for P1 (repeat each phase as a new log line).
**Tests.** —
**Pitfalls.** —
**Refs.** —

### DOC-02 — ojjyOS Wiki (user, admin and developer guides)
- Phase: P4 · Size: L · Status: todo
- Depends: APP-HELP-01
- Files: docs/wiki/

**Goal.** Arch-Wiki-style documentation: installation, every app, every
setting, troubleshooting, command-line guides, ojjypkg, Linux
environment, ojjyWin, developer tutorials, hardware pages; rendered in
the Help viewer and as a static site.
**Done when.** 50 pages covering all shipped features.
**Tests.** Link checker.
**Pitfalls.** Keep pages updated per release (QA-40 step).
**Refs.** —

### DOC-03 — Third-party register (`THIRD_PARTY.md`)
- Phase: P0 · Size: S · Status: done
- Depends: —
- Files: THIRD_PARTY.md (new), tools/check_third_party.py (new), scripts/mkfs.sh, scripts/test.sh

**Context (2026-10-26).** The repo ships third-party font data (Inter,
JetBrains Mono, both SIL OFL 1.1, licences in `fonts/`) and keeps Apple
wallpapers in git-ignored `wallpapers/local/`, but there is no single
register of what is third-party, under which licence, and from where.
**Steps.**
- [x] Write `THIRD_PARTY.md`: `fonts/` entries (Inter, JetBrains Mono,
      sources + OFL 1.1), the generated bitmap/font data derived from
      them, the Apple wallpapers policy (local only, never shipped or
      committed), and `packages/`/CA roots/tz data/dictionaries/Unicode
      data as explicit "not shipped yet" future sections (D-023: ported
      programs only as optional ojjypkg packages).
      Test: file exists and is reviewed.
      Trap: no absolute local paths, no personal data.
      — done 2026-10-27 (test: `tools/check_third_party.py` validates the sections and entries; sabotage test (removing the packages/ heading) fails it with exit 1)
- [x] Ship it read-only in the OS image as
      `/System/Legal/THIRD_PARTY.txt` (`scripts/mkfs.sh`) and add
      `tools/check_third_party.py` asserting the `fonts/` + `packages/`
      sections and the wallpapers note; wire it into `scripts/test.sh`.
      Test: `bash scripts/test.sh` runs it; a broken register fails it.
      Trap: keep the file > 64 lines — it doubles as the editor's
      read-only fixture for APP-TEXTEDIT-01's live tests.
      — done 2026-10-27 (test: `bash scripts/test.sh` runs the check ("register OK (92 lines)"); `strings build/system.ojfs` shows THIRD_PARTY.txt inside the packed image; 92 lines > 64 so it opens read-only in the editor)

**Goal.** List every third-party asset/data/package with licence and
source: Inter and JetBrains Mono (OFL), Unicode data, future CA roots,
tz data, dictionaries, ported packages; note Apple wallpapers are local
only and never shipped.
**Done when.** File exists (and ships in the image); the check runs in
`scripts/test.sh`; QA-40 can rely on it.
**Tests.** Script checking that `fonts/` and `packages/` entries appear.
**Pitfalls.** —
**Log.** Done 2026-10-27: register written (fonts + OFL sources, generated
font data, wallpapers policy, packages/CA/tz/dictionary/Unicode
placeholders per D-023) and shipped read-only in the image as
`/System/Legal/THIRD_PARTY.txt` (92 lines, also the editor read-only
fixture); `tools/check_third_party.py` wired into `scripts/test.sh`
("register OK (92 lines)"; removing a section fails it, verified).
Evidence: build/qa-22-arm-build1.log shows the file packed
(`strings build/system.ojfs`). Limits: future sections are placeholders
until that data actually ships.
**Refs.** —

### DOC-04 — Subsystem architecture docs
- Phase: P2 · Size: M · Status: todo
- Depends: DOC-01
- Files: docs/arch/*.md

**Goal.** One living document per subsystem (memory, scheduler, IPC, VFS,
ojjyFS, network, WindowServer, ojjyKit, security, compat layers) with
diagrams, invariants and "how to extend" sections; cards update them.
**Done when.** Docs exist for all P1–P2 subsystems.
**Tests.** —
**Pitfalls.** —
**Refs.** —

### DOC-05 — Public showcase site (when Jonas decides)
- Phase: P6 · Size: S · Status: todo
- Depends: DOC-02
- Files: site/

**Goal.** Static website presenting ojjyOS (screenshots, features,
download when available, credits to Jonas Lee) — only published when
Jonas explicitly asks (never push without permission).
**Done when.** Site builds locally.
**Tests.** Link checker.
**Pitfalls.** No Apple imagery on the site.
**Refs.** —
