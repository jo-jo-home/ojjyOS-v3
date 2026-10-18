# AGENTS.md — ojjyOS v3 (working context for AI sessions)

**READ THIS ENTIRE FILE BEFORE TOUCHING ANYTHING.** It is the handoff
document: project constraints, history, current state, verification, and roadmap.

## LATEST (2026-10-17): networking, browser with HTTPS, real Wi-Fi tile, apps pass DONE

Commits c07af3a (network), 11cbdbd (browser), 9adb978 (TLS 1.3/HTTPS),
5887bf9 (Wi-Fi tile + Settings > Network), 305fc9e (apps pass). Next ideas
in ROADMAP.md: certificate verification, images (PNG), Downloads, TextEdit
word wrap, Preview for real files, Intel NIC. Nothing pushed.

### (earlier in this session) networking + working web browser

Jonas: "keep working on improvements, make a list of new stuff, improve all
apps, focus on a working browser" and "add wifi bluetooth ... write down plans
and updates and context". Live plan/log: `BROWSER_APPS_PLAN.md`; ideas list:
`ROADMAP.md`. Done: virtio-net/rng, from-scratch IPv4/DHCP/DNS/TCP, HTTP
client, Terminal ifconfig/ping/nslookup/curl, HTML engine + browser (real
sites over http://). Keep big buffers out of BSS (see plan log: firmware
memory at 0x44000000 blocks the ARM kernel image). No Wi-Fi/Bluetooth
hardware exists in QEMU; the Wi-Fi tile will control the real virtual NIC.

## Earlier: cursor repair + UI cleanup DONE (2026-10-17); bug/improvement pass

Cursor offset/lag fixed (absolute tablet path, scanout cursor overlay, no
scene repaint on pointer motion) and UI cleaned (transparent text, UTF-8,
blue glass, round traffic lights, working Finder titlebar toolbar, menu
layout, Settings switches). Receipts at the end of `CURSOR_UI_PLAN.md`.
Jonas then asked: keep writing context into the project while working, and
after the needed work, hunt for bugs/improvements -> see `IMPROVEMENTS.md`.

Session records / HEAD at this write (2026-10-17 ~21:15):

- **`JONAS_MESSAGES.md`** — Jonas's 42 messages to the AI, verbatim, in order,
  extracted from the 9.7 MB session export he dropped into the repo root
  (`ojjyos-v3-bugs-macos-features-custom-kernel.json`; deliberately NOT
  committed — too large). His msg 280 is the canonical requirements list;
  his latest instructions supersede older ones.
- HEAD is `8cc8d5a` (file-copy data loss, Finder shortcut/refresh/history,
  account-name bug pass; details in `IMPROVEMENTS.md`); main is 17 commits
  ahead of origin/main, **nothing pushed**.
- More than one AI session has worked in this repo concurrently today.
  Re-check `git log`/`git status` before editing; every handoff file here is
  a snapshot, not a lock.

### (previous) cursor repair then UI cleanup

Jonas reported absolute-pointer offset and lag, then asked for UI improvements
afterward. This supersedes scheduler work; **no scheduler edits exist**. Read
`CURSOR_UI_PLAN.md` for the measured baseline, plan and safe candidate-build path.
QMP (100,100) produced software cursor (512,582); root cause is absolute tablet
positions converted into deltas and scaled again by compositor mouse speed.
Pointer movement also forces whole-scene redraws. User's live Cocoa VM must not
be stopped or its disk overwritten. Keep updating this context during execution.

## Previous milestone: bounded ARM64 protected apps completed (2026-10-17)

This section supersedes older pause/no-userspace claims below. Jonas resumed
with **"continue"**, then asked to **update context as work proceeds**; keep
doing that in future sessions. Read **HANDOFF.md**, **PROTECTED_APPS.md** and
**PROTECTED_APPS_PLAN.md**, then inspect actual git state before changing code.

- Real static AArch64 ELF programs now run at EL0 with private RX/RW+NX pages,
  a guarded four-page stack, clean registers/thread pointers and a private EL1
  exception stack. Owned kernel/app page tables replace firmware mappings.
- Checked SVC #0 ABI permits PID/UID/uptime/captured write/exit only; NEVER
  route user registers into old `proc.c`'s raw-pointer `syscall_dispatch()`.
- Faults return to the kernel. A 100 ms physical-counter budget stops runaway
  EL0 loops (final spin test: 101 ms); syscall/output/page/file limits apply.
  Full integer trap state is saved; FP/SIMD deliberately traps. All user pages
  are wiped/freed and PID reaped after returning to kernel tables.
- Authenticated `run hello`, `run spin`, etc. work in Terminal and console.
  `programs/` builds separate ELF files into the read-only system image. Intel
  builds/boots normally but explicitly reports protected execution unsupported.
- **Not multitasking:** one bounded synchronous runner; built-in GUI apps and
  desktop remain kernel-hosted. No Intel ring-3, hardened account/file ACLs,
  kernel W^X, bare-metal Mac support, HVF or hard real-time UI guarantee.
- Final builds for both architectures passed with zero warnings/errors.
  **1055 C assertions** (849 previous + 206 user tests) and **9 Python tests**
  pass normally and under compatible Homebrew LLVM ASan/UBSan. User tests also
  run 4000 deterministic malformed-header mutations (not counted separately).
- Final ARM64 live suite passes **19 distinct programs**, fault-class checks,
  timer/syscall limits, equal free-page counts, successful runs after faults,
  12 extra clean runs and Spotlight->Terminal hello. Both architectures pass
  sustained input, sync/shutdown, rebuild preservation, cold-boot authentication
  and corrupted-newest-checkpoint fallback. Only private test VMs/disks touched.
- Final source-image data partitions hash-identically to their retained complete
  pre-rebuild backups; legacy image hash still matches Session 8. Receipt:
  `build/protected-data-preservation.log`.
- Final evidence: `build/protected-build-{arm,intel}.log`,
  `build/protected-{host,sanitized}-tests.log`,
  `build/protected-runtime-arm-final/` and `build/protected-runtime-intel-final-v2/`
  with matching `.log` transcripts. ARM directory contains
  `desktop-after-app-faults.png`. Earlier partial runs remain as debugging history.
- See git log for the local milestone commit. **Nothing pushed.** Next work is
  actual scheduling/concurrent tasks, then user-facing app integration and
  authorization. Fullscreen/HVF remain separate tasks; don't silently expand scope.

### Historical pause (superseded by the resume above)

Jonas's latest request was: **"pause. and write all context to the project.
like a new ai can know whats happening"**. Read **[HANDOFF.md](HANDOFF.md)**
alongside this file before resuming. The reliability pass is committed as
`bac1dec`. The next protected-app milestone was only being investigated;
**no protected-app implementation or runtime changes were made afterward**.
This pause changes documentation only. Do not start the proposed work until
Jonas asks to resume. Never push without explicit authorization.

## Current reliability pass (Session 8, 2026-10-17)

**This section supersedes historical claims below.** The old Session-7
autorepeat explanation was a hypothesis, not a verified diagnosis. Confirmed
defects and the current implementation:

- Virtio queues now retain their negotiated depth. This machine's QEMU
  offers 64-entry input queues; the previous code negotiated 64 but wrapped
  at the 128-entry storage capacity, reading/recycling stale ring entries.
  `tests/test_virtqueue.c` covers 8/64/128 depths and 16-bit index rollover.
- Removed raw evdev `KEY_*` macros that shadowed internal enum names (Home,
  arrows, Delete and other keys mapped incorrectly). Real evdev and PS/2
  decoders share modifier tracking and delayed editing repeats (450/35 ms).
  Adjacent pointer moves coalesce; transitions have reserved queue capacity;
  overflow is counted without printing from the IRQ/producer path.
- Intel standalone UEFI-to-ELF entry explicitly uses SysV ABI (RDI). The
  Intel kernel is now linked at **64 MiB**, not 1 MiB: a live OVMF memory
  map showed ACPI NVS at 8-9 MiB and boot-services allocations overlapping
  the old growing BSS. Allocation failures print conflicting map entries.
- Homebrew's Intel firmware is split code/vars; code alone is not a valid
  `-bios` size. The runner creates a private combined writable pflash image.
  Intel shutdown's literal AML decoder now handles ZeroOp/OneOp and proper
  package lengths; QEMU's actual `_S5_` package uses ZeroOp for both types.
- `update_animations()` now returns `changed`; missing-return warnings fail
  the kernel build. UI re-entry restores buffering; panic paths draw to
  scanout directly, not a backbuffer that a halted kernel cannot present.
- Builds output `build/<arch>/ojjyos3.img`. `tools/finalize_image.py` locks
  and validates existing GPT images, backs them up, copies the entire data
  partition, and atomically installs a separately assembled candidate.
  First per-arch builds import legacy `build/ojjyos3.img` without altering
  it. Refuse malformed/locked disks. Never reset a user's disk to get a test
  passing. VirtualBox VDIs are retained and running VMs are not force-stopped.
- Persistence v2 alternates two 8 MiB slots at LBA 67584 and 83968. Header
  and payload have CRC-32; flush payload before publishing/flushing header.
  Restore validates into scratch first and falls back to the older valid
  slot. v1 had NO checksums: validate structure and migrate via slot 1.
  Autosaves capture a snapshot then write at most eight payload sectors per
  main-loop turn. Later edits stay dirty. Failed sync is reported; failed
  save cancels shutdown/restart. Snapshot capture/CRC and individual block
  requests remain synchronous, so there is no hard real-time latency bound.
- Current runner is **TCG, one vCPU**, not HVF. ARM64 QEMU virt support is
  not bare-metal Apple Silicon support. Process entries/CPU tick estimates
  are bookkeeping, not scheduling, isolated executable processes, or memory
  protection. Account/file authorization and password hardening remain open.
- Existing filesystem/account/heap/process tests plus input, virtqueue,
  persistence fault injection, ACPI AML, and image-update tests run via
  `bash scripts/test.sh`. ASan/UBSan use `HOST_CC=/opt/homebrew/opt/llvm/bin/clang
  OJJY_SANITIZE=1 bash scripts/test.sh`; Apple clang 14's ASan runtime fails
  even for an empty program on this macOS. Sanitized tests namespace the
  freestanding libc to avoid interposing on sanitizer startup.

### Verification receipts (completed Session 8)

- Both full macOS builds succeeded with zero warnings/errors. Delivered
  images: `build/aarch64/ojjyos3.img`, `build/x86_64/ojjyos3.img`.
- 849 host C assertions (37 FS + 81 RAMFS/accounts/heap/proc + 67 input +
  633 persistence + 10 virtqueue + 21 AML) and 9 Python image tests passed.
  The suite also passed with Homebrew LLVM ASan/UBSan. The Python tests
  verified refusal to replace a disk actually locked by a QEMU process.
- Both delivered images passed three-boot live QEMU flows: UI setup,
  Backspace, 256 o/Backspace cycles, console commands, sync, real shutdown,
  private-disk rebuild preserving accounts, cold-boot password login, and
  recovery/login after deliberately damaging the newest checkpoint.
  Screenshots were captured after `[UI] Desktop presented`, not merely
  after the credential-check log.
- The legacy image's SHA-256 before/after delivery was identical:
  `1594a5d572485d4a4df474a2a5fcfbfa0af7f33f3076289e01d23e04c6e72e24`.
  Full backups were retained under each architecture's `backups/` directory.
- Evidence is in this machine's temporary workspace:
  `/private/var/folders/d1/1ykss2fs7_v0nx_r_8qd0_9c0000gn/T/opencode/ojjyos-reliability-build-3ujat0b1/`
  (`deliver-build-*`, `delivered-tests.log`, `delivered-smoke-*`,
  `vm-delivered-*`, `tests-final-sanitized.log`, `ring-before/after.log`).
- Limitations: no bare-metal/HVF claims; manual Cocoa focus-loss behavior,
  VirtualBox runtime and Linux GNU-EFI builds were not revalidated. Panic
  rendering was corrected by inspection/build, not a forced live fault.
  No hard real-time latency or arbitrary app-crash isolation is promised.
  Fullscreen scaling, stronger account security and protected apps remain
  future work. See the local git log for this pass's commit; nothing pushed.

### Historical session summaries (not current verification receipts)

- **Session 1:** Rescued the broken codebase (fixed 28+ build/boot/IRQ/VFS/UI
  bugs, added RAMFS disk persistence, ACPI/PSCI power off, Desktop icons,
  offline `Web.app`, and `About.app`).
- **Session 2 & 3:** Completed the native Apple Silicon (`aarch64`) port
  (`entry.S`, `gicv2`, `timer_a64`, `pl011`, `pl031`, `virtio-mmio` v2
  `virtio_blk` + `virtio_input`, `psci`, dual-arch Makefiles & scripts,
  `run-qemu.sh`), window resize + green zoom, Mission
  Control cards, Reduced Transparency/Motion toggles, Control Center
  Restart/Shut Down buttons, and full documentation sweep.
- **Session 4:** Implemented the multi-user Account & Salted FIPS 180-4
  SHA-256 Credential Engine (`users.c`/`users.h`), First-Boot Setup
  Assistant, macOS Tahoe Lock/Login Screen (`Ctrl+Cmd+Q`), `Settings →
  Users & Accounts`, interactive Menu Bar dropdowns (`ojjy`, `File`,
  `Edit`, `View`, `Window`), Desktop right-click context menu,
  `Calculator.app`, and `Activity Monitor.app` (11 built-in apps total).
- **Session 5:** Implemented the 16-byte-aligned coalescing Kernel Heap
  Allocator (`kmalloc`/`kcalloc`/`krealloc`/`kfree`/`kheap_validate` in
  `memory.c`) and the Kernel Process Table & Syscall Dispatcher
  (`proc.c`/`proc.h` with PID/PPID/UID tracking, per-user app ownership,
  live CPU tick accounting, protected system daemons PID 1–4,
  `syscall_dispatch` for 13 syscalls, and `ps`/`top`/`kill`/`heap`
  commands in `Terminal.app`, console, and `Activity Monitor.app`).
- **Session 6:** Fixed the constant screen **flashing** (compositor
  rendered straight into the live scanout — now double-buffered +
  dirty-tracked, verified 0.000% idle framebuffer diff), shipped the
  anti-aliased **JetBrains Mono** font, 2x cursor, `show-cursor=on`,
  `-smp 1` + MPIDR core parking, Tahoe ASCII art everywhere
  (commits `d4f0fa7`, `6eea513`, `1f860de`).
- **Session 7 (historical handoff):** documented repeated `o`, incorrect
  deletion, apparent freezes, and fullscreen bars. Its high-confidence
  autorepeat claim was unproven. Session 8 found the negotiated-depth and
  key-name collisions above; do not restore the proposed ignore-all-repeats
  workaround or assume VNC stretches correctly without testing it.

---

## 0. START-HERE CHECKLIST (do these in order)

1. **Read this file fully.** Never push to a remote; commit locally as
   "Jonas Lee" (`git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit`).
2. Check git state and preserve user work. Use the current build scripts,
   never an old destructive image builder. Shut down a VM before updating
   its image. Do not delete backups or saved disks automatically.
3. Run `bash scripts/test.sh` and both full builds. For live testing, use
   `tests/qemu_smoke.py` with a new output directory; it always copies the
   source disk. `--fresh-data` resets only that new private test copy.
4. Keep scope kernel-first. Next milestones: protected tasks/address
   spaces, actual scheduling/syscall ABI, account/file authorization,
   measured rendering/input latency. Fullscreen/HVF work remains separate.
5. Record fresh verification, limitations and commit locally; never push
   without Jonas explicitly asking.

**Completed work this codebase already has (all verified):**
- `aarch64`: 28/28 objects compile clean; `x86_64`: 30/30 clean;
  host unit tests 118/118 (`test_fs` 37 + `test_ramfs` 81).
- Full aarch64 port: `main.c` ifdefs, dual-arch Makefiles/bootloader,
  `linker-aarch64.ld` @ `0x41000000`, virtio transport (fixed register
  offsets + `VIRTIO_F_VERSION_1`), GICv2/GICv3 auto-detect, full IRQ
  register save in the vector table, polled input via 1 kHz timer.
- Desktop polish: window resize + green zoom, Mission Control cards,
  Reduced Transparency/Motion, Control Center Restart/Shut Down,
  menu-bar dropdowns, right-click context menu, desktop icons, Web app.
- Accounts (salted SHA-256, first-boot setup, lock screen), Calculator,
  Activity Monitor (11 apps), kernel heap + process table + syscalls.
- Session 6: double-buffered compositor + dirty-tracking (idle
  framebuffer diff **0.000%** — this is the number to re-verify after
  any rendering change), 2x cursor, `-smp 1` + MPIDR core parking,
  anti-aliased JetBrains Mono font (`tools/mkfont.py` regenerates
  `kernel/src/font.c` from `fonts/JetBrainsMono-Regular.ttf`), Tahoe
  ASCII art across splash/serial/console/login/About/Terminal/power-off.
- Persistence: RAMFS snapshots autosaved to the data partition every
  30 s when dirty + on shutdown/reboot/`sync`; restored on boot.
- Commits: `0dbaea6` → `7ceff6a` → `db6dba2` → `d4f0fa7` → `6eea513`
  → `1f860de` (all local, nothing pushed — Jonas pushes himself).

---

## 1. What this project is

**ojjyOS v3** — a hobby operating system built entirely from scratch by
**Jonas Lee** (the repo owner; GitHub `jo-jo-home`, online handle "ojjy").
The AI assistant's job is to improve it. Non-negotiable rules from Jonas:

- **Keep the custom kernel.** No Linux/BSD/Windows/macOS base. Those systems
  may be used as *build tools* only. All kernel work must be original.
- **Credit Jonas Lee everywhere it matters**: README, About screen, boot
  splash, serial banner, system info, power-off screen. Preserve that.
- **Keep the blue Tahoe theme** (macOS-Tahoe-inspired, blue glass). Never
  introduce other accent colors as the theme.
- **macOS-like behaviors implemented natively** — not by depending on macOS
  or pretending to run Mac apps.
- **Autonomous work**: plan thoroughly, execute end-to-end without
  check-ins, then summarize what changed. Do not ask questions mid-task.
- **Never push to a remote until Jonas explicitly says so.** Local commits
  are fine (and expected).
- **Focus on the kernel first** — "it sets up everything else we build
  upon."
- Have fun: Jonas asked for "lots of cool ascii art and welcome texts" in
  the boot — that exists now; keep it fun but not garish.
- Jonas's priority order (his words): *"reliable boot → input → persistent
  storage → protected apps → dependable desktop → visual polish."*

### Hardware reality (critical for understanding the port)

Jonas runs the OS on an **M2 Pro Mac** in a VM:

- **VirtualBox on Apple Silicon can only run ARM64 guests.** It cannot run
  x86_64 guests at all. The x86_64 build targets *other* machines
  (Debian + VirtualBox / x86 QEMU) — Jonas cannot run it locally.
- Therefore the **aarch64 port is the path that actually runs on his Mac**,
  via `qemu-system-aarch64` with **TCG emulation** in the current runner.
  Jonas said: *"ik its built for intel so i dont like it i want it to be
  native."* That is why the port exists and why it is the primary target.
- macOS builds both complete architectures using clang, Homebrew lld,
  standalone UEFI definitions and the Python image builder. No Linux VM or
  gnu-efi is required. `brew install qemu nasm lld`; Homebrew LLVM is also
  installed and supplies a compatible sanitizer runtime.

---

## 2. Repository layout

```
ojjyOS-v3/
├── AGENTS.md               THIS FILE (handoff; update at session end)
├── PLAN.md                 improvement plan (bug-fix plan + feature plan)
├── README.md, RELEASE.md, ARCHITECTURE.md, DESKTOP.md, APPS.md,
│   FILESYSTEM.md, VISUALS.md, BUGFIXES.md, LICENSE   (up-to-date docs)
├── boot/
│   ├── ojjy-boot.c         UEFI bootloader (ELF64 loader; both arches)
│   └── Makefile            builds BOOTX64.EFI / BOOTAA64.EFI
├── kernel/
│   ├── Makefile            ARCH=x86_64 (default) | aarch64 (+ `objs` target)
│   ├── linker.ld           x86_64 script (base 0x4000000)
│   ├── linker-aarch64.ld   aarch64 script (base 0x41000000)
│   └── src/
│       ├── main.c          entry, console shell, splash art, power seq
│       ├── users.c/h       multi-user accounts, salted SHA-256 credentials, home dirs
│       ├── proc.c/h        kernel process table (PID/UID/ticks) & syscall dispatcher
│       ├── memory.c/h      PMM bitmap page allocator + 512 KB coalescing kheap (kmalloc/kfree)
│       ├── system.c/h      power requests (shutdown/restart/sync flags)
│       ├── arch_power.h    arch_power_off()/arch_reboot() interface
│       ├── types.h         types + per-arch asm helpers (both arches)
│       ├── arch/aarch64/   Apple Silicon / ARM64 port (all device/platform code, §4.1)
│       ├── gdt/idt/paging/timer(PIT)/serial(16550)/entry.asm  x86-only
│       ├── serial_core.c   shared serial_puts/puthex/printf
│       ├── framebuffer.c font.c console.c string.c panic.c  (neutral)
│       ├── drivers/        driver.c model, input.c, ps2_*, ata, block_cache,
│       │                   acpi (x86 S5), blockdev.h + blockdev_x86.c,
│       │                   diagnostics.c
│       ├── fs/             vfs, ojfs (read-only), ramfs (writable),
│       │                   bundle, persist (disk checkpoints)
│       └── ui/             compositor.c (whole desktop + 11 apps + login/lock),
│                           services.c, theme.c
├── scripts/                build.sh mkfs.sh mkimg.sh run.sh run-qemu.sh
├── tools/                  mkojfs mkicon mkwallpaper mkarray (host tools)
├── sysroot/                files packed into the OJFS image (11 .app bundles + Web pages)
│   └── System/Web/*.page   the offline Web app's pages (home, about, accounts, release, shortcuts)
├── tests/                  host-side unit tests (test_fs.c, test_ramfs.c — 118 assertions total)
└── build/                  outputs (gitignored)
```

---

## 3. History — what happened in Sessions 1–5

### Session 1 (the rescue, commit `0dbaea6`)

A five-way audit found the original project **could not build or boot at all**.
Fixed 28+ defects across build, boot, input, filesystem, and desktop:

- **Build/boot:** Fixed space-indented `kernel/Makefile`, replaced broken `xxd -i`
  with deterministic `tools/mkarray.c`, preserved `BootInfo *` in `rbx` across BSS
  clear in `entry.asm`, rewrote `LoadKernel()` in `boot/ojjy-boot.c` to parse
  **ELF64 `PT_LOAD` program headers**, reserved up to `_kernel_end` in PMM, and
  implemented on-demand `paging_map()` for framebuffers above 4 GB.
- **Input:** Wired `driver_dispatch_irq()` into `isr_handler()`, unmasked PIC IRQ 2
  cascade for slave IRQs (`>= 8`), added `0xE0` extended scancodes (giving the OS
  a Super/Cmd key), fixed 9-bit signed mouse delta decoding, and wired scroll wheel
  events to `compositor_handle_scroll()`.
- **Filesystem:** Fixed OJFS root index lookup (`parent == root_idx`), made VFS mounts
  carry per-instance `fs_data` so `/Users` and `/Library` are independent RAMFS trees,
  replaced the leaking RAMFS bump allocator with a first-fit free list + node reuse,
  fixed `strncpy` `n+1` write overflow, and implemented real `.`/`..` path normalization.
- **Persistence & Power:** Added `fs/persist.c` (snapshots both RAMFS instances to
  the GPT `ojjyOS Data` partition at sector `67584` with CRC-32 verification,
  restored at boot, autosaved every 30 s when dirty and on `sync`/shutdown/reboot)
  and `drivers/acpi.c` (ACPI S5 power-off and 8042 reset).
- **Desktop:** Decoupled window opening from `bundle_launch()`, implemented window
  close/minimize traffic lights, single-source-of-truth cursor hit-testing, fixed
  Finder/Terminal/TextEdit/Calendar bugs, and added Desktop icons, `Web.app`, and
  `About.app`.

### Sessions 2 & 3 (Native Apple Silicon `aarch64` Port & Tahoe Polish, commit `935f27f`)

- Completed `kernel/src/arch/aarch64/` (`entry.S`, `arch64.c`, `gicv2.c`, `timer_a64.c`,
  `pl011.c`, `pl031.c`, `virtio.c`, `virtio_input.c`, `virtio_blk.c`, `psci.c`),
  `kernel/linker-aarch64.ld` (`0x41000000`), dual-arch `kernel/Makefile` and
  `boot/Makefile`, `scripts/build.sh`, `scripts/mkimg.sh`, and `scripts/run-qemu.sh`
  (Apple Silicon `-accel hvf` + `ramfb` + `virtio-*`).
- Fixed two `virtio` descriptor chain bugs: `d->next` index advancement in
  `virtio_desc_add()` and setting `VRING_DESC_F_NEXT` on write descriptors in
  `virtio_blk.c`.
- Added window bottom-right resize grip + green traffic-light maximize/zoom,
  live Mission Control window cards (`Cmd+M`), `Reduced Transparency` (`rt=`) and
  `Reduced Motion` (`rm=`) toggles in Settings, Control Center Restart/Shut Down
  buttons, and PS/2 modifier key release events for `Cmd+Tab`.

### Session 4 (User Accounts, Salted SHA-256 Credentials & Tahoe Desktop Expansion, commit `3582b8e`)

- **Multi-User Account & Credential Engine (`kernel/src/users.h`, `kernel/src/users.c`):**
  - Full FIPS 180-4 SHA-256 implementation with per-account 8-hex-char salt
    (`SHA-256(salt_hex + ":" + password)`) and constant-time digest verification.
  - Persistent account database in `/Library/Accounts/users.db` (`username|full_name|salt_hex|hash_hex|home_dir|admin`)
    automatically synced to disk via `system_request_sync()`.
  - Automatic per-user home directory provisioning under `/Users/<username>`
    (`Desktop/Welcome.txt`, `Documents/Notes.txt`, `Calendar.txt`).
- **First-Boot Setup Assistant & macOS Tahoe Lock/Login Screen (`compositor.c`):**
  - When `users_count() == 0` (or clicking `+ New Account`), displays the Setup
    Assistant card (**Full Name**, **Username**, **Password**, **Confirm Password**).
  - When locked (`!users_is_authenticated()`, or pressing `Ctrl+Cmd+Q`), displays
    the macOS Tahoe Lock/Login Screen with large clock, user selector pills,
    masked password input, `+ New Account`, `Restart`, and `Shut Down`.
- **Desktop & App Additions:**
  - `Settings → Users & Accounts` (`SETTINGS_PAGE_USERS`) with active user card,
    interactive password change form, account list, `+ Add Account`, `Lock Screen`,
    and `Switch User`.
  - Interactive Menu Bar dropdowns (`ojjy`, `File`, `Edit`, `View`, `Window`) +
    clickable active username pill on the top-right menu bar.
  - Desktop right-click context menu (`New Folder on Desktop`, `New Text Note`,
    `Open Terminal`, `Open Finder`, `Change Wallpaper...`, `Account Settings...`,
    `About ojjyOS`).
  - Added `Calculator.app` (`APP_CALCULATOR`) and `Activity Monitor.app`
    (`APP_MONITOR`), bringing total built-in apps to 11.

### Session 5 (Kernel Heap Allocator & Process/Syscall Subsystem)

- **Kernel Heap Allocator (`kernel/src/memory.h`, `kernel/src/memory.c`):**
  - Added a 512 KB 16-byte-aligned coalescing free-list kernel heap (`kheap_init`,
    `kmalloc`, `kcalloc`, `krealloc`, `kfree`, `kheap_get_total`, `kheap_get_used`,
    `kheap_get_free`, `kheap_get_alloc_count`, `kheap_validate`).
  - Every block header carries magic `0x4F4A4850` (`"OJHP"`), detects double-frees
    and out-of-bounds frees, splits oversized blocks on allocation, and coalesces
    adjacent free blocks both forward and backward on `kfree`.
- **Kernel Process Table & Syscall Subsystem (`kernel/src/proc.h`, `kernel/src/proc.c`):**
  - Tracks up to 24 processes (`PID`, `PPID`, `UID`, `owner`, `app_type`,
    `ProcState`, `cpu_ticks`, `memory_bytes`, `syscalls_invoked`).
  - Spawns 4 protected system daemons at boot (`PID 1 kernel_task`,
    `PID 2 vfs_persistd`, `PID 3 inputd`, `PID 4 WindowServer`) and automatically
    spawns/terminates per-user application processes as windows open and close.
  - Provides `syscall_dispatch(num, a0, a1, a2, a3)` implementing 13 kernel
    syscalls (`SYS_GETPID`, `SYS_GETUID`, `SYS_UPTIME`, `SYS_OPEN`, `SYS_READ`,
    `SYS_WRITE`, `SYS_CLOSE`, `SYS_MKDIR`, `SYS_UNLINK`, `SYS_MALLOC`,
    `SYS_FREE`, `SYS_KILL`, `SYS_SYNC`).
  - Integrated into `Activity Monitor.app` (live process table + KHeap & syscall
    telemetry), `Terminal.app` (`ps`, `top`, `kill <pid>`, `heap`/`mem`), and
    the fallback kernel console.

---

## 4. Architecture & Subsystem Reference

### 4.1 `aarch64` / QEMU `virt` Platform Layer (`kernel/src/arch/aarch64/`)

| File | Responsibility |
|---|---|
| `arch64.h` / `arch64.c` | Platform init (`a64_init`), IRQ/exception dispatch (`a64_irq_handler`, `a64_exception_handler`), input polling (`a64_poll_input`) |
| `entry.S` | EL3/EL2 → EL1 drop, FP/NEON enable (`CPACR_EL1`), 128 KB stack setup, BSS clear (`BootInfo *` preserved in `x19`), `VBAR_EL1` vector table |
| `gicv2.c/h` | GICv2 at `GICD 0x08000000` / `GICC 0x08010000`; masks SPIs, sets priorities, ack/eoi/enable/disable |
| `timer_a64.c` | ARM Generic Physical Timer → 1 kHz tick (`PPI 30`); `timer_init`/`timer_get_ticks`/`timer_sleep` |
| `pl011.c` | `serial_init`/`serial_putc` on PL011 UART `0x09000000` |
| `pl031.c` | PL031 RTC at `0x09010000`; Unix epoch → calendar `RtcTime` conversion |
| `virtio.c/h` | `virtio-mmio` v2 transport scanning 32 slots at `0x0A000000 + 0x200*i` |
| `virtio_input.c` | Virtio keyboard + tablet (`EV_KEY`, `EV_ABS` → screen bounds, `REL_WHEEL`) |
| `virtio_blk.c` | Virtio block device implementing `block_available`/`block_read_sectors`/`block_write_sectors` |
| `psci.c` | `arch_power_off`/`arch_reboot` via PSCI `hvc #0` (fallback `smc #0`) |

### 4.2 Future Milestones (Roadmap for Next Sessions)

1. **M4.1 — Concurrent Scheduling & Intel Ring 3 (bounded ARM64 EL0 exists):**
   - Extend `ProcEntry` in `kernel/src/proc.h` with saved CPU context (`CpuContext`:
     `x19..x30, sp, elr_el1, spsr_el1` on `aarch64`; `r12..r15, rbx, rbp, rsp, rip, rflags`
     on `x86_64`) and per-process kernel stacks allocated from `kmalloc`/`pmm_alloc_page`.
   - Extend the separate checked `user.c` ABI and ARM64 trap path to concurrent
     tasks; implement a checked Intel entry path later. **Do not expose the raw
     kernel `syscall_dispatch()` to userspace.**
   - Extend `user_image.c`'s bounded ELF loader to a deliberate executable/app
     policy and lifetime-managed per-task tables, rather than bypassing validation.
2. **M4.2 — Networking (`virtio-net` + ARP/IPv4/ICMP/UDP/TCP/HTTP):**
   - Add `virtio_net.c` on `aarch64` (`virtio-mmio` device ID 1) and a minimal
     socket/HTTP client so `Web.app` can fetch live HTTP pages when QEMU user-mode
     networking (`-nic user`) is attached.
3. **M4.3 — Audio (`virtio-sound` / HDA) & Anti-Aliased Vector Fonts.**

---

## 4.6. Input investigation: corrected Session-7 handoff

**Symptom (from Jonas, live on his M2 Pro):** "if I press a letter
sometimes it just types a bunch of that letter"; after pressing `o`,
"a bunch of os showed", pressing Delete "just spawns more os", and
then the desktop froze completely. It also froze once before, right
around typing in the first-boot account-setup screen.

The old handoff incorrectly presented host autorepeat as a proven cause and
recommended ignoring all repeats. That proposal is withdrawn. Main-loop input
is batched before drawing; `mark_dirty()` does not force a frame per event.

The confirmed transport defect was a **64/128 negotiated ring-size mismatch**.
Reading `events[desc_id]` was correct, but `desc_id` itself could come from the
wrong used-ring slot after wrapping. The shared transport and input driver now
use `vq->size` for all masks/limits. Device indices are reloaded with volatile
access and barriers; each input harvest has a bounded budget.

A second confirmed defect was the raw evdev `KEY_*` macros overwriting internal
`KeyCode` enumerators: Home mapped to 102 instead of 73, Up to 103 instead of
74, Delete to 111 instead of 85 (`KEY_MAX` was 91). Those macros are removed.

Repeats are classified and rate-limited centrally, not dropped wholesale.
Editing repeats work after 450 ms, at most once per 35 ms; Enter and held
shortcuts do not repeat. Modifier state derives from separate physical left/
right keys. PS/2 Pause bytes are swallowed rather than fabricating modifiers.

Verification: `tests/test_input.c`, `tests/test_virtqueue.c`, and the QMP smoke
flow. A temporary fault variant restoring the old 128-entry ring masks fails
the ring-wrap regression; the corrected source passes. Live tests create an
account via the UI, erase `oooo` with Backspace, cross the ring repeatedly with
256 `o`/Backspace cycles, then execute commands successfully. QMP duplicate-down
commands alone are not proof of the exact hardware event stream: the emulator
may filter them. Host decoder tests exercise repeats directly.

Original manual Cocoa focus-loss behavior is not exhaustively reproduced;
do not claim every possible cause of apparent freezing has been eliminated.

---

## 4.7. Display scaling (still separate follow-up work)

Jonas: "Zoom to Fit + fullscreen — screen is bigger but not filling
the whole screen."

The tested ARM firmware currently boots ramfb at 1024x768 (4:3); aspect-
preserving scaling on a widescreen host leaves bars. Intel's tested GOP mode
is 1920x1080. Previous firmware/CLI experiments are version-specific, not proof
that no other resolution path exists. QEMU on this machine is now 11.1.2.

The Session-7 claim that macOS Screen Sharing would stretch away all bars was
not verified. No `--vnc` feature was added in this reliability pass. Investigate
and test firmware modes, renderer buffer limits, host scaling and HVF separately
before promising a fullscreen fix. Keep the current working runner as fallback.

---

## 5. `aarch64` / QEMU `virt` Cheat Sheet

| Thing | Address / value |
|---|---|
| RAM base | `0x40000000` |
| Kernel load/link | `0x41000000` |
| GICv2 distributor | `0x08000000` |
| GICv2 CPU interface | `0x08010000` |
| Timer PPI | `intid 30` (non-secure EL1 physical timer) |
| PL011 UART | `0x09000000` |
| PL031 RTC | `0x09010000` |
| virtio-mmio slots | `0x0A000000 + i*0x200` (32 slots, version 2 = modern) |
| virtio device IDs | `blk=2`, `input=18` |
| PSCI conduit | `hvc #0` (`SYSTEM_OFF 0x84000008`, `SYSTEM_RESET 0x84000009`) |
| Exception levels | UEFI may leave us at EL2 → `entry.S` drops to EL1 |
| ELF machine | `x86_64=62`, `aarch64=183` |

---

## 6. Build & Verify Commands (this Mac)

```bash
cd "~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3"

# Complete builds, including UEFI and data-preserving images (VMs shut down).
ARCH=aarch64 bash scripts/build.sh
ARCH=x86_64 bash scripts/build.sh
bash scripts/test.sh

# Optional instrumented host suite (compatible Homebrew LLVM runtime).
HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 bash scripts/test.sh

# Live checks use a private disk copy; --fresh-data resets ONLY that new copy.
# Choose a NEW output directory each run; repeat with --arch x86_64 as needed.
python3 -B tests/qemu_smoke.py --arch aarch64 \
  --image build/aarch64/ojjyos3.img --output build/smoke-arm --fresh-data

# Stage only reviewed changes; commit locally as Jonas, never push unasked.
git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit -m "..."
```

### 6.1 QEMU forensics toolkit (how the flashing bug was cracked)

Use the `VM` helper in `tests/qemu_smoke.py` rather than fixed shared sockets,
logs, framebuffer addresses, or the user's writable disk. It handles QMP
greetings, request IDs and asynchronous events, uses a short private AF_UNIX
socket, and stops only its own child process. Preserve evidence in its output
directory. It never force-stops another VM.

`vm.screenshot(path)` uses QMP `screendump` to PNG. `vm.qmp("pmemsave", ...)`
can dump physical memory, but rediscover addresses from that boot's serial log
and honor framebuffer pitch. A stable idle framebuffer does not prove working
input. For static checks, the old flashing regression showed 35-100% pixel
differences at idle; after double buffering it was 0.000%. Do not hardcode
`0xbc7a0000` or assume the dimensions remain 1024x768.

Qcodes: `o`, `shift`, `esc`, `backspace`, `delete`, `ret`, `tab`, `spc`.
The key macOS labels Delete is Backspace, not forward Delete. Absolute tablet
motion uses QMP `abs` events (axis x/y), not relative mouse coordinates.

Health receipts: expected devices attach, `Entering main loop`, real typing
and editing work, saved login survives cold boot. `[UI] Desktop presented`
is emitted after the login-to-desktop frame is actually copied to scanout;
waiting merely for a users.c login log can capture the old login screen.
Queue loss is counted (`diag`), not logged once per event. A successful boot
alone is not evidence that input, persistence, or rendering is correct.

---

## 7. Gotchas Learned the Hard Way (do not regress)

1. **Makefile recipes need TABS.** Double-check any Makefile you edit.
2. **Never push to a remote without Jonas's explicit go-ahead.**
3. **BSS-clear vs stack**: on both arches, anything saved on the stack
   before the BSS wipe is destroyed — keep the `BootInfo` pointer in a
   callee-saved register (x86 `rbx`, ARM `x19`).
4. **`VfsOps` carry `fs_data` as the first parameter of every op.** Mounts
   are per-instance; do not reintroduce globals (`current_instance`).
5. **`strncpy` must write exactly `n` bytes** (tested in `tests/test_fs.c`).
6. **OJFS root entry**: top-level entries have `parent == root entry index`
   (`0` in the writer), NOT `0xFFFFFFFF`. `find_entry()` resolves the root
   index first.
7. **x86 IRQ wiring**: `isr_handler()` → `driver_dispatch_irq(irq)` for
   vectors `32..47` first, legacy IDT handler fallback; `pic_enable_irq`
   for IRQ ≥ 8 must also unmask the IRQ2 cascade.
8. **Big structs never go on the kernel stack** (`AppWindowState` ~110 KB;
   window reordering uses static `state_scratch`; `Bundle` arrays in
   `app_registry_init`/`cmd_apps` are static).
9. **Compositor cursor is the single source of truth** — `main.c` passes
   `compositor_get_cursor()` coords into `compositor_handle_mouse` and
   `compositor_handle_right_click`.
10. **printf set**: `console_printf`/`serial_printf` support
    `%s %d %u %x %p %c %%` with `-`/`0`/width/`l`/`ll`. Cast explicitly
    when passing 64-bit values as `%d`/`%x`.
11. **Persistence checkpoint = raw RAMFS instance bytes.** Changing
    `RAMFS_MAX_NODES`/`RAMFS_DATA_SIZE` invalidates old checkpoints via
    the size check (intentional).
12. **Data partition start sector `67584` is a contract** between
    `scripts/mkimg.sh` and `kernel/src/fs/persist.h`. Change both together.
13. **`aarch64` has no PIT/PS2/ATA/CMOS** — generic timer / `virtio-input` /
    `virtio-blk` / `PL031` instead. Common code must use the neutral APIs
    (`timer_get_ticks`, `input_post_*`, `block_*`, `rtc_read_time`).
14. **The compositor's `ui` command** is the desktop entry; `Shift+Esc`
    returns from desktop UI to the fallback console so plain `Esc` only
    closes menus/overlays/Setup Assistant inside the desktop.
15. **`virtio-blk` single-chain-in-flight model**: `d->next`
    must link to `idx + 1` (not `free_head`), and write sector requests
    must set `VRING_DESC_F_NEXT` alongside `VRING_DESC_F_WRITE`.
    Input instead keeps multiple independent event descriptors in flight;
    its ring masks must use the negotiated queue size, not `VQ_SIZE`.

---

## 8. Version & Attribution

- Version: **3.1.0** ("Make It Work" release) — `OJJYOS_VERSION` in
  `kernel/src/main.c`. Keep in sync: boot art (`main.c` splash), About
  window (`compositor.c` `draw_about_window`), `sysroot/System/version.txt`
  (`mkfs.sh`), serial banner.
- Author: **Jonas Lee** in every user-visible surface (README, About,
  splash, serial banner, power screen, `sysroot/System/version.txt`).
  AI contributions are improvements to HIS project — never add AI
  co-author lines to user-facing strings, and commit as
  `user.name="Jonas Lee"`.
