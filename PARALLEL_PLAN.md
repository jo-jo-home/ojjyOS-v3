# PARALLEL_PLAN.md — two AIs working at the same time (2026-10-18)

Jonas: "study the current project and make detailed plans for what to make.
i want to ask another ai to do something too while u work at the same time".

Two tracks run in parallel, each in its OWN git worktree and branch, with
strict file ownership so they never edit the same code. Both publish
finished steps to `main` with fast-forwards. Read this whole file before
touching anything, then your track's plan.

## 1. Where the project stands (studied 2026-10-18 evening, `main` = `0b18cb5`)

Done and on `main`:
- Custom kernel for ARM64 + x86-64; protected EL0 programs (ARM64, one at a
  time); RAMFS persisted to disk with CRC'd double slots; accounts with
  PBKDF2 + CSPRNG salts + admin gating + per-user VFS isolation.
- **Native Mac app** (`build/ojjyOS.app`, `scripts/run-vm.sh`) on Apple's
  Virtualization.framework: PCI virtio, virtio-console, xHCI USB HID,
  Retina display, full screen (N1–N11, `NATIVE_VM_PLAN.md`). QEMU remains
  for tests (TCG/HVF) and Intel.
- Network stack (DHCP/DNS/TCP), HTTP, TLS 1.3, HTML engine and browser with
  Inter text.
- Desktop: HiDPI renderer, Inter UI font, icons, animations, Liquid Glass on
  floating chrome (LG1/LG2) and window rims (LG3), official Tahoe wallpapers
  (git-ignored local pack), Settings > Appearance customisation (C).

Gaps found while studying (feed the two tracks below):
- **TLS does not check anything about the server's identity**:
  `net/tls.c handle_certificate()` only parses lengths, and the
  CertificateVerify message is skipped (`stage = 4` without verifying the
  signature). Any machine in the path can impersonate any site. No X.509,
  RSA or ECDSA code exists (`crypto/` has SHA-256, X25519, AES-GCM,
  ChaCha20-Poly1305 only).
- No images on web pages (`web/html.c` treats `<img>` as a placeholder); no
  PNG decoder; JPEG is baseline-only (most web JPEGs are progressive).
- Clock is whatever the firmware/RTC says; no NTP. (Auto Dark mode and
  certificate validity dates depend on it.)
- No downloads; links to files show as text or fail.
- Desktop job leftovers: glass controls (LG4), notifications (S1), window
  snapping (S2), live-resize smoothness (S3), Terminal themes (S4), every-app
  polish (A), small leftovers (`HANDOFF_UI.md` §5).
- Hygiene: the repo lives on the iCloud-synced Desktop. iCloud creates
  "conflict copies" such as `kernel/src/net/net 2.o` and
  `sysroot/System/Programs/guard 2.elf` (195+ files). The `.o/.d` ones are
  git-ignored, but the duplicate `.elf` programs inside `sysroot/` can be
  packed into the OS image. (Jonas decides whether to move the repo out of
  iCloud; see §6.)
- Kernel backlog (scheduler, apps as EL0 programs, journaled FS) is big and
  touches everything; it is NOT run in parallel with these two tracks (§5).

## 2. The two tracks

| | Track D — Desktop (this AI, "Claude") | Track N — Network & Web (the other AI) |
|---|---|---|
| Worktree | `ojjyOS-kernel/ojjyOS-v3-ui` | `ojjyOS-kernel/ojjyOS-v3-net` |
| Branch | `desktop-polish` | `net-web` |
| Plan | `DESKTOP_PLAN.md` (+ `HANDOFF_UI.md` §5) | `NET_WEB_PLAN.md` |
| Goal | finish the Tahoe desktop: glass controls, notifications, snapping, live resize, Terminal themes, every app | real HTTPS security (certificate + signature verification), NTP clock, images on web pages (PNG + progressive JPEG), downloads, Web app polish |

The main folder `ojjyOS-kernel/ojjyOS-v3` is Jonas's (currently on branch
`ui-polish-3`, equal to `main`). Neither AI edits, builds in, or switches
branches there. Its `build/ojjyOS.app` and `build/aarch64|x86_64` images are
what Jonas runs: do not rebuild them while working (see §4.5).

## 3. File ownership (the rule that keeps us out of each other's way)

**Track N owns** (only N edits these):
- `kernel/src/crypto/**`, `kernel/src/net/**`, `kernel/src/web/**`
- `kernel/src/image/**` EXCEPT `wallpack.*` (N adds png.c, progressive JPEG,
  `image.h`; `jpeg.c` may be extended by N, its API must stay compatible)
- `kernel/src/ui/browser.c/h`, `kernel/src/ui/netcmd.c/h`
- `kernel/src/drivers/rtc.c`, `kernel/src/arch/aarch64/pl031.c`, a new
  `kernel/src/clock.c/h` (NTP-corrected wall clock)
- `kernel/src/random.c` (only if needed for TLS)
- tests: `tests/test_crypto.c`, `test_net.c`, `test_web.c`, `test_jpeg.c`,
  new `test_x509.c`, `test_png.c`, `test_clock.c`, `gen_*_vectors.py`,
  `*_vectors.h`, `tls_probe.c`, `web_server.py`, `qemu_net.py`,
  `qemu_browser.py`, `qemu_network_ui.py`, new `qemu_https.py` etc.
- tools: new `tools/mkroots.py` (root certificate store generator)
- docs: `NET_WEB_PLAN.md`, `BROWSER_APPS_PLAN.md`

**Track D owns** (only D edits these):
- `kernel/src/ui/compositor.c/h`, `ui/services.c/h`, `ui/theme.c/h`,
  `ui/wallpaper.c/h`, `ui/draw.h`, `ui/icons*`, `ui/calcmath.*`
- `kernel/src/gfx.c/h`, `framebuffer.*`, `uifont.*`, `font*.c`,
  `cursor_image.c`, `kernel/src/image/wallpack.*`
- `scripts/mkfs.sh`, `tools/mkicons.py`, `tools/mkuifont.py`,
  `tools/import_wallpapers.py`, `sysroot/**`
- tests: `test_gfx.c`, `test_framebuffer.c`, `test_calc.c`,
  `test_wallpack.c`, `qemu_design_tour.py`, `qemu_ui_tour.py`,
  `qemu_apps_*.py`, `qemu_anim_check.py`, `qemu_perf.py`,
  `qemu_pointer.py`, `qemu_wallpapers.py`, `qemu_appearance.py`,
  `qemu_files.py`, new desktop tests
- docs: `DESKTOP_PLAN.md`, `HANDOFF_UI.md`, `LIQUID_GLASS_APPS_PLAN.md`,
  `APPS.md`, `DESKTOP.md`, `VISUALS.md`

**Nobody edits without need; append-only when needed** (small, separate
commits; expect trivial merges): `kernel/Makefile` (add source lines only),
`scripts/test.sh` (add a build line + add to the run list only),
`TODO.md` (tick/annotate your own track's lines only), `AGENTS.md` (add your
own dated "LATEST" block at the top; never rewrite the other's), `README.md`,
`ROADMAP.md`.

**Off limits to both during this round** (kernel core, owned by nobody right
now): `kernel/src/main.c`, `arch/**` (except N's `pl031.c`), `boot/**`,
`vmapp/**`, `users.c`, `fs/**`, `memory.c`, `proc.c`, `user*.c`,
`scripts/build*.sh`, `scripts/mkimg.sh`, `scripts/run-*.sh`,
`tools/finalize_image.py`, `tests/qemu_smoke.py`, `tests/vz_*.py`. If a
track truly needs a one-line hook in one of these (e.g. N calling
`clock_tick()` / NTP start after DHCP from `main.c`), make it a tiny,
separate commit with a message starting `hook:` and mention it in the
track's plan log, so the other AI sees it when rebasing.

**Interfaces between the tracks** (agreed now, so nobody waits):
- Notifications: Track N posts with the existing
  `notifications_push(const char *message)` from `ui/services.h` (e.g.
  "Downloaded report.pdf", "Clock synced"). Track D upgrades the
  implementation into banners (S1) and keeps that function working; D may
  add `notifications_post(icon, title, body)` and N may switch to it later.
- Images: Track N creates `kernel/src/image/image.h`:
  `bool image_info(const uint8_t *data, size_t len, int *w, int *h);`
  `bool image_decode(const uint8_t *data, size_t len, uint32_t *argb, size_t pixels);`
  (output 0xAARRGGBB, straight alpha; dispatches JPEG baseline+progressive /
  PNG by signature). Track D uses it in Preview/Finder once it lands on
  `main` (optional for D; D must not block on it).
- Downloads folder: Track N writes to `/Users/<user>/Downloads/` through
  VFS (creating the folder if missing). Track D adds a Downloads item to the
  Finder sidebar.
- Clock: Track N's `clock.c` keeps `rtc_read_time()`'s signature unchanged,
  so the desktop clock and Auto appearance pick up NTP time automatically.

## 4. Working rules (both tracks)

1. Work only in your own worktree. `cd` there first, every time.
2. Dev builds only, inside your worktree:
   `OJJY_BUILD_DIR="$PWD/build/dev-aarch64" ARCH=aarch64 bash scripts/build.sh`
   and the same for `x86_64`. Zero warnings. `bash scripts/test.sh` and the
   sanitizer run (`HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1
   OJJY_TEST_DIR="$PWD/build/tests-san" bash scripts/test.sh`) pass.
3. Live tests: always a NEW `--output` directory inside your own worktree's
   `build/`; they copy the disk, never touch the source image. Both AIs may
   run QEMU at the same time (private sockets), but **performance numbers are
   only valid on a quiet host** — say in the log if the other track was
   running heavy jobs, or re-measure later.
4. Commit locally as Jonas, small and often, with the trailer:
   `git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit`,
   message ending with `Co-Authored-By: <your model> <noreply@...>`.
   Stage files by name. Never commit `ojjyos-v3-bugs-macos-features-custom-kernel.json`,
   anything in `wallpapers/local/`, build output, or iCloud `* 2.*` copies.
5. Publishing a green step to `main` (from your worktree):
   ```bash
   git fetch -q . <your-branch>:main || {      # main moved: integrate first
     git rebase main                            # or: git merge main
     # rebuild both arches, rerun scripts/test.sh, then:
     git fetch -q . <your-branch>:main; }
   ```
   Never force-update `main`, never rewrite commits already on `main`.
   **Installed images:** neither track rebuilds `build/aarch64`,
   `build/x86_64` or `build/ojjyOS.app` in the main folder. When BOTH tracks
   are finished, the last one to finish does the final delivery (§5 of
   `HANDOFF_UI.md` "Finish"), only when no VM is running.
6. Never push to any remote. Never `git reset --hard`, `stash`, or `clean`
   anything you did not create.
7. Keep your plan's log up to date after every step (what, evidence paths,
   commit), and tick `TODO.md`, so either AI or a new one can continue.
8. Product rules: keep the blue Tahoe theme as default, credit Jonas Lee,
   no AI names in the product, nothing big in BSS or on the kernel stack
   (use `pmm_alloc_bytes`; it cannot free — reuse buffers), the Apple
   wallpapers never leave `wallpapers/local/` and local disk images.

## 5. After this round (not in parallel — kernel core)

Jonas's priority order is "reliable boot -> input -> persistent storage ->
protected apps -> dependable desktop -> visual polish". The kernel items in
`BETTER_OS_PLAN.md` §1–2 (preemptive scheduler with per-task kernel stacks,
built-in apps as EL0 programs over a real syscall ABI, kernel W^X and heap
guards, `pmm_free_bytes`, journaled filesystem with real ACLs, per-user
settings) touch `main.c`, `proc.c`, `memory.c`, `fs/` and the main loop that
both tracks depend on. Do them as ONE track when Tracks D and N are merged,
starting with the scheduler. A prompt for it can be written then.

## 6. Decisions for Jonas (not blocking)

- Move the repo out of the iCloud-synced Desktop (e.g. `~/Developer/`) or
  mark the folder "keep downloaded / exclude": iCloud creates duplicate
  `* 2.*` files and slows builds. Moving changes the paths in many docs, so
  do it between rounds, not during.
