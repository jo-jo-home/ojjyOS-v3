# 00 — How to work on the roadmap (session protocol)

This file tells an AI session exactly how to take one roadmap card from
`todo` to `done`. It complements `../AGENTS.md` (project rules and commands);
if they disagree, `AGENTS.md` and Jonas's latest message win.

---

## 1. Start of session (always, ~10 minutes)

1. Read `../AGENTS.md` top to bottom (LATEST blocks first), then
   `roadmap/README.md`, then this file.
2. `git worktree list`, `git log --oneline -8 main`, `git status` in every
   worktree. Uncommitted changes belong to whoever made them: never reset,
   stash, checkout over or delete them.
3. Find your card:
   - If Jonas named a card ID, take that one.
   - Else run `python3 roadmap/tools/roadmap.py next` and take the first
     ready card of the lowest phase, unless another session is `doing` it.
4. Read the whole card **and** every card in its `Depends` line (to know the
   interfaces you build on) **and** the files listed under `Files`.
5. Check the card's dependencies really are done in code, not only marked.
   If something is missing, either do the missing piece (if it is small and
   clearly part of the dependency's card) or mark your card `blocked` with a
   one-line reason and pick another.
6. Set the card's `Status: doing`, commit that one-line change
   ("roadmap: start SCH-03") so parallel sessions see it.

## 2. Where to work

- One **worktree per parallel track** (see `../PARALLEL_PLAN.md`). The main
  folder `ojjyOS-v3` is Jonas's checkout; tracks work in sibling worktrees
  (`ojjyOS-v3-ui`, `ojjyOS-v3-net`, or a new `ojjyOS-v3-<track>` created with
  `git worktree add ../ojjyOS-v3-<track> -b <branch> main`).
- Publish finished work by fast-forwarding `main`:
  `git fetch -q . <branch>:main`. If that fails, someone else moved `main`:
  rebase your branch onto `main`, rerun the tests, then fast-forward. Never
  `git branch -f main`, never force anything.
- The main folder is fast-forwarded to `main` only for deliveries (installed
  image rebuilds), and only when no VM is running.

## 3. Splitting work

- Size `S`/`M` cards: finish in one go.
- Size `L` cards: the numbered steps are natural commit points. Commit after
  each green step with "SCH-03 step 2: …".
- Size `XL` cards must be split **before** coding: add new cards (next free
  numbers in the same prefix) to the area file, make the XL card depend on
  them (it becomes the "integration" card), run the tool, commit
  "roadmap: split WIN-20".
- If you discover new necessary work, add a card for it instead of silently
  expanding scope. Mention new cards in your summary.

## 4. Definition of Done (applies to every card, on top of its own list)

A card is done only when **all** of these hold:

1. Every bullet of the card's **Done when** list is true and evidenced.
2. Builds: `ARCH=aarch64` and `ARCH=x86_64` both build with **0 warnings**
   (dev builds: `OJJY_BUILD_DIR="$PWD/build/dev-<arch>" ARCH=<arch> bash
   scripts/build.sh` — absolute path required). x86_64 may legitimately
   report "unsupported" for an ARM-only feature, but it must build.
3. Host tests: `bash scripts/test.sh` passes, and
   `HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 bash scripts/test.sh`
   passes (no ASan/UBSan reports).
4. New logic has **new tests**: host unit tests for anything that can run on
   the host (parsers, algorithms, data structures, layout), plus a live test
   (`tests/qemu_*.py`, `tests/vz_*.py`) for anything a user sees or a device
   does. Parsers of untrusted input get a fuzz loop.
5. No regression: the existing live smoke (`tests/qemu_smoke.py`) passes on
   TCG and HVF; for UI work the relevant tour too.
6. Evidence directory `build/<card-id-lower>-*` holds logs/screenshots; the
   card's log line names it.
7. Docs: the area file card set to `Status: done` with a short **Log** line
   at the end of the card (date, commit, evidence dir, any limits);
   `STATUS.md` regenerated; user-facing docs (`APPS.md`, `DESKTOP.md`,
   in-OS pages in `scripts/mkfs.sh`) updated when behaviour changed;
   `AGENTS.md` LATEST block updated at the end of a multi-card session.
8. Committed locally as Jonas Lee with the co-author trailer; `main`
   fast-forwarded. Nothing pushed.

## 5. Evidence and honesty

- Quote measurements with how they were taken (TCG vs HVF vs VZ, host load).
  TCG is ~8x slower than HVF/VZ; never quote TCG timings as user-facing speed.
- A boot that reaches the desktop is not proof that a feature works: test the
  feature itself (typed input, saved file after cold boot, decoded pixels).
- When something is only partly done, say so in the card log and leave or
  add a follow-up card. Never mark `done` with known failing criteria.
- Screenshots that matter are looked at, not just captured.

## 6. Testing ladder (cheapest first)

1. **Host unit test** (`tests/test_*.c`, run by `scripts/test.sh`): fastest,
   runs under ASan/UBSan. Put pure logic in files that compile on the host.
2. **Host render/tool** (e.g. `tests/web_render.c`): for visual engines.
3. **QEMU TCG** (`tests/qemu_*.py`, private disk copy): device + UI flows.
4. **QEMU HVF** (`OJJY_ACCEL=hvf`): real-speed timings, GICv3 path.
5. **QEMU x86_64** (`--arch x86_64`): the Intel build.
6. **VZ native** (`tests/vz_*.py`, `scripts/test-vmapp.sh`): what Jonas runs.
7. **Real hardware** (later phases): recorded manually with photos/logs.

Live tests always run on a **private copy** of a disk image in a **new**
output directory, never on `build/<arch>/ojjyos3.img` itself.

## 7. Code quality bar

- C17, freestanding kernel (`-mgeneral-regs-only` on ARM: **no floating
  point in the kernel** — use fixed point). Match surrounding style:
  4-space indent, `snake_case`, file header comment with
  "ojjyOS - Created by Jonas Lee".
- No large buffers in BSS or on the stack (BSS collides with firmware
  memory on ARM; kernel stacks are small). Use the page allocator.
- Every input from disk, network, USB, user programs or files is untrusted:
  bounds-check everything, fuzz parsers, no undefined behaviour (left shifts
  of negative values, unaligned access, signed overflow).
- MMIO through the accessors in `arch/aarch64/mmio.h` (single LDR/STR — HVF
  requirement). Device waits are wall-clock bounded, never spin counts.
- Keep files focused: no new 8,000-line files. New subsystems get their own
  directory and a header that documents the API.
- Comments explain *why*, not *what*; no AI names in code or UI strings.

## 8. Licensing and originality (permanent)

- Original code only. Do not copy or translate code from Linux, *BSD,
  Windows/ReactOS, Wine, Haiku, SerenityOS, Fuchsia, Chromium, WebKit,
  Gecko, Mesa, FFmpeg, libc implementations, etc. Reading public
  specifications (UEFI, ACPI, PCI, USB, virtio, NVMe, Arm ARM, Intel SDM,
  ELF, PE/COFF, RFCs, W3C/WHATWG, OpenType, Unicode, Microsoft Learn API
  documentation) is expected.
- Exception (decision D-023): third-party open-source **programs and
  libraries** may be ported as **optional ojjypkg packages** (never part
  of the base image), with licences recorded. The base system stays
  original.
- Third-party **data** with permissive licences is fine when recorded in
  `THIRD_PARTY.md` (to be created by `DOC-03`): fonts (OFL), Unicode data,
  CA root list (from public sources), tz database (public domain).
- Apple's Tahoe wallpapers stay in git-ignored `wallpapers/local/`; never
  commit Apple assets. Same rule for any proprietary file.
- Test programs for compatibility layers must be freely redistributable
  (built from source by us, or open-source binaries).

## 9. Performance rules

- Every user-visible card states a budget in its Done list where relevant
  (frame time, boot time, memory). Defaults: 60 fps target for animations
  (16.7 ms), input-to-photon < 50 ms under HVF/VZ, cold boot to login < 3 s
  under VZ, idle CPU near zero (the desktop must sleep when idle).
- Measure on a quiet host. A/B against the previous build under the same
  load when in doubt.

## 10. Security rules

- Least privilege by default. New syscalls validate every pointer through
  the user-copy helpers; no kernel pointer is ever returned to user space.
- Cryptography: only constant-time comparisons for secrets; secrets wiped
  after use; randomness from `random_bytes()`.
- Any new network-facing parser gets a fuzz loop in the host tests.

## 11. Ending a session

- Finish the current step to green or revert it; never leave `main` broken.
- Update the card (status, log line), regenerate `STATUS.md`, commit.
- If stopping mid-card: write a **Handoff** paragraph inside the card
  (what is done, what is next, traps found) and leave `Status: doing`.
- Summarize to Jonas in plain English: what changed, what works, what does
  not yet, how to try it.

## 12. Adding or changing cards

- Use `templates/task-card.md`. IDs never change or get reused; a dropped
  card becomes `Status: done` with "Log: dropped — reason".
- Architecture changes get a decision record (`templates/decision-record.md`)
  appended to `03-target-architecture.md` (next `D-0NN` number).
- Run `python3 roadmap/tools/roadmap.py check` before committing roadmap
  edits; it rejects malformed cards, unknown dependencies and cycles.
