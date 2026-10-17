# AGENTS.md — ojjyOS v3 (working context for AI sessions)

**READ THIS ENTIRE FILE BEFORE TOUCHING ANYTHING.** It is the handoff
document: what this project is, everything that has happened across
Sessions 1–6, the exact current state, how to build & run every check,
and the ordered roadmap of what comes next.

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
- **Session 7 (THIS HANDOFF — IN FLIGHT, fix designed, NOT yet coded):**
  Jonas reported: pressing a letter "sometimes types a bunch of that
  letter", pressing Delete "spawns more o's", then the desktop froze.
  **Diagnosis (done, high confidence): QEMU host-side key auto-repeat**
  — QEMU's cocoa frontend re-sends key-down events ~30x/sec while a key
  is held (and can leave a key *stuck* repeating if its window loses
  focus mid-press). Every repeat arrives as a fresh virtio EV_KEY and
  our drivers post each one as a real `KEY_PRESS` → letter floods +
  one full-desktop redraw per event under slow TCG → redraw backlog →
  queue saturates → mouse dead → looks frozen.
  **THE FIX (implement first, then verify — details in §4.6.):**
  kernel-side auto-repeat suppression in BOTH keyboard drivers: when a
  key-down arrives for a key that is *already down* (`input_is_key_down`),
  post `INPUT_EVENT_KEY_REPEAT` instead of `KEY_PRESS` (nothing consumes
  KEY_REPEAT today, so repeats become no-ops). Insertion points are
  marked below. Then run the QMP flood-test in §4.6 to prove it.

---

## 0. START-HERE CHECKLIST (do these in order)

1. **Read this file fully.** Never push to a remote; commit locally as
   "Jonas Lee" (`git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit`).
2. **Verify the current build** (commands in §6): `bash scripts/mkfs.sh`,
   `ARCH=aarch64 bash scripts/build.sh`, then the two host unit tests
   (118 assertions) — everything must pass before you start editing.
3. **Implement the Session-7 key-repeat fix** (§4.6, both drivers, exact
   code locations given), rebuild, and run the QMP flood-test to verify.
4. **Fix the display "letterbox" complaint** (§4.7): Zoom To Fit shows
   black side bars because the guest is 1024x768 (4:3) on a 16:10 Mac
   display — that is *correct* aspect-preserving behavior. Add the
   planned `--vnc` mode to `run-qemu.sh` (macOS Screen Sharing stretches
   to fill the whole screen), as specified in §4.7.
5. Re-run the full verification sweep (§6), update this file's session
   log, commit.

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
  via `qemu-system-aarch64` with **HVF acceleration (near-native)**.
  Jonas said: *"ik its built for intel so i dont like it i want it to be
  native."* That is why the port exists and why it is the primary target.
- macOS dev machine has: Apple clang 14 (cross-compiles both arches),
  nasm (brew), python3, Rosetta. **No gnu-efi on macOS** (bootloader builds
  on Debian only) and **no ld.lld** — kernel *objects* verify locally via
  `make -C kernel ARCH=aarch64 objs` and `make -C kernel ARCH=x86_64 objs`;
  full *links* happen on Debian (or once `brew install lld qemu` is run).

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
│   ├── linker.ld           x86_64 script (base 0x100000)
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

1. **M4.1 — Preemptive Context Switching & Ring 3 / EL0 Userspace:**
   - Extend `ProcEntry` in `kernel/src/proc.h` with saved CPU context (`CpuContext`:
     `x19..x30, sp, elr_el1, spsr_el1` on `aarch64`; `r12..r15, rbx, rbp, rsp, rip, rflags`
     on `x86_64`) and per-process kernel stacks allocated from `kmalloc`/`pmm_alloc_page`.
   - Wire `SVC #0` (in `kernel/src/arch/aarch64/entry.S`) and `INT 0x80` / `SYSCALL`
     (in `kernel/src/idt.c`) to call `syscall_dispatch()`.
   - Add an ELF64 userspace binary loader in `proc.c` so external binaries in `/bin`
     or `.app` bundles can execute in isolated address spaces.
2. **M4.2 — Networking (`virtio-net` + ARP/IPv4/ICMP/UDP/TCP/HTTP):**
   - Add `virtio_net.c` on `aarch64` (`virtio-mmio` device ID 1) and a minimal
     socket/HTTP client so `Web.app` can fetch live HTTP pages when QEMU user-mode
     networking (`-nic user`) is attached.
3. **M4.3 — Audio (`virtio-sound` / HDA) & Anti-Aliased Vector Fonts.**

---

## 4.6. SESSION 7 — key-repeat fix spec (implement EXACTLY this)

**Symptom (from Jonas, live on his M2 Pro):** "if I press a letter
sometimes it just types a bunch of that letter"; after pressing `o`,
"a bunch of os showed", pressing Delete "just spawns more os", and
then the desktop froze completely. It also froze once before, right
around typing in the first-boot account-setup screen.

**Root cause (high confidence, diagnosis complete):**
QEMU's host-side keyboard auto-repeat re-sends key-down events
(~30/sec) while a key is physically held — and the cocoa frontend can
leave a key **stuck repeating** if its window loses/regains focus
mid-press. Our drivers currently post EVERY key-down as a fresh
`INPUT_EVENT_KEY_PRESS`, so:
- each repeat types another character (the "bunch of os"), and
- each key event triggers `mark_dirty()` → a full desktop redraw
  (expensive under TCG emulation) → the redraw backlog saturates the
  256-entry input queue → mouse events starve → the desktop appears
  frozen.

Already-ruled-out causes (do NOT chase these again):
- Login/setup field buffers ARE bounds-checked (`len + 1 < cap`,
  `kernel/src/ui/compositor.c` ~line 5307). Not an overflow.
- `virtio_input_poll` is CORRECT (Gemini's version in tree): it drains
  *all* used-ring entries and reads `events[desc_id]` (the right slot),
  recycles per-descriptor. An earlier "stale slot 0" theory was wrong.
- Serial log shows a single clean boot — not a reboot loop.

**The fix — kernel-side auto-repeat suppression (both arches):**

A key-down for a key that is *already down* = auto-repeat → post
`INPUT_EVENT_KEY_REPEAT` (nothing consumes it yet — it becomes a
no-op) instead of `INPUT_EVENT_KEY_PRESS`. The key-state bitmap that
`input_is_key_down()` reads is updated by `input_post_key_event()` on
PRESS/RELEASE only, so checking *before* posting is correct.

**Fix location 1 — `kernel/src/arch/aarch64/virtio_input.c`,
`handle_key_event()` (~line 241, right after the modifier `switch`
and the `key_to_keycode` lookup, BEFORE the ascii/post block at
~lines 246-258):**

```c
    KeyCode keycode = key_to_keycode[code];
    if (keycode == KEY_NONE) {
        return;
    }

    /* Auto-repeat: the host keyboard stack re-sends key-down events
     * while a key is held (QEMU can even leave a key stuck repeating
     * if its window loses focus mid-press). Only the first press
     * becomes a KEY_PRESS; repeats become KEY_REPEAT, which nothing
     * consumes yet - this stops the letter-flood and the redraw
     * backlog that froze the desktop. */
    if (down && input_is_key_down(keycode)) {
        input_post_key_event(INPUT_EVENT_KEY_REPEAT,
                             (uint8_t)code, keycode, 0);
        return;
    }
```

Notes: the modifier `switch` above it is idempotent for repeats;
CapsLock is already guarded by the local `caps_down` flag. Release
events must still post normally (`down` false passes through).

**Fix location 2 — `kernel/src/drivers/ps2_keyboard.c` (x86), in
`ps2_kbd_handle_irq()` after the modifier `switch` and keycode lookup,
and in `handle_extended_key()` after the extended keycode lookup.**
Same pattern:

```c
    if (!released && keycode != KEY_NONE && input_is_key_down(keycode)) {
        input_post_key_event(INPUT_EVENT_KEY_REPEAT, scancode, keycode, 0);
        return true;
    }
```

**Verification — the QMP flood-test (this is the proof):**

1. Build + boot headless with a QMP socket (command pattern in the
   note under section 6), wait ~75 s for the desktop
   ("Entering main loop" in the serial log).
2. Drop to the text console so typing is visible in the serial log:
   `main.c` exits the UI on **Shift+Esc** — send via QMP
   `input-send-event`: `shift` down, `esc` down+up, `shift` up.
3. Flood the letter `o`: send
   `{"execute":"input-send-event","arguments":{"events":[{"type":"key","data":{"down":true,"key":{"type":"qcode","data":"o"}}}]}}`
   **40 times with no key-ups**.
4. **Expected after the fix:** the serial log shows exactly ONE `o`
   echoed at the `> ` prompt (console_putc mirrors to serial). Before
   the fix this prints ~40 `o`s. Then send `o` up, then a normal
   press cycle (`o` down/up twice) → both `o`s appear (real presses
   still work). Flood `backspace` downs x40 → exactly one backspace.
5. Also confirm the serial log has NO `[INPUT] WARNING: Queue
   overflow` lines during the flood, and idle framebuffer dumps stay
   0.000% different afterwards.

QMP qcodes used: letters `"a"`-`"z"`, `"shift"`, `"esc"`,
`"delete"` (forward-del) and `"backspace"` (the key macOS labels
"delete").

**Optional follow-up (nice, not required):** implement macOS-style
delayed repeat later — Terminal/TextEdit could consume
`INPUT_EVENT_KEY_REPEAT` with a ~500 ms delay + ~30 ms interval using
`timer_get_ticks()`. Do NOT do this in the same change; the
suppression alone is the fix.

---

## 4.7. SESSION 7 — display "letterbox" + tiny-window resolution

Jonas: "Zoom to Fit + fullscreen — screen is bigger but not filling
the whole screen."

**This is correct behavior, not a bug:** the guest is 1024x768 (4:3)
and his display is ~16:10, so aspect-preserving Zoom To Fit leaves
black bars left/right. Established facts (all tested on this machine —
do not re-test blindly):
- `ramfb` is the ONLY GOP provider in the edk2-aarch64 firmware build
  here; it exposes 640x480 / 800x600 / 1024x768 only.
- `-device ramfb,xres=...,yres=...` → `Property not found` in this
  QEMU. No CLI resolution override exists.
- `-device virtio-gpu-device` → the firmware has no GOP driver for it
  → bootloader prints `[FAIL] Could not initialize framebuffer:
  Unsupported` and boot stops. Tested and rejected.
- Available QEMU display backends on this Mac: `none`, `curses`,
  `cocoa`, `dbus` (no SDL in this build).

**Planned fix (implement): add a `--vnc` mode to `scripts/run-qemu.sh`:**
- When invoked as `bash scripts/run-qemu.sh aarch64 --vnc`, replace the
  `-display cocoa,show-cursor=on` args with `-display none -vnc 127.0.0.1:0`.
- Print instructions: open **`vnc://127.0.0.1:5900`** in Safari/Finder
  → Screen Sharing opens → **View → Turn Scaling On → Enter Full
  Screen** — Screen Sharing *stretches* to fill the entire screen
  (no aspect bars), which is exactly what Jonas asked for.
- Keep the cocoa default (aspect-correct) and mention in the printed
  tips that `--vnc` exists for fill-the-whole-screen mode.

There is NO way to raise the guest resolution beyond 1024x768 without
rebuilding edk2 with more ramfb modes or a virtio-gpu GOP driver —
explicitly out of scope for now; document it as a future option.

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

# 1. Rebuild the OJFS filesystem image + embedded kernel header (works on macOS)
bash scripts/mkfs.sh

# 2. Compile-verify BOTH kernel architectures (must succeed with 0 warnings, 0 errors)
make -C kernel clean && make -C kernel ARCH=aarch64 objs
make -C kernel clean && make -C kernel ARCH=x86_64 objs
make -C kernel clean

# 3. Run host unit tests (37 in test_fs + 81 in test_ramfs = 118 total assertions)
clang -O1 -w -Ikernel/src tests/test_fs.c kernel/src/fs/ojfs.c kernel/src/fs/vfs.c kernel/src/string.c -o /tmp/test_fs && /tmp/test_fs
clang -O1 -w -Ikernel/src tests/test_ramfs.c kernel/src/fs/ramfs.c kernel/src/fs/vfs.c kernel/src/users.c kernel/src/memory.c kernel/src/proc.c kernel/src/string.c -o /tmp/test_ramfs && /tmp/test_ramfs
rm -f /tmp/test_fs /tmp/test_ramfs

# 4. Local commit (author Jonas; NEVER push)
git add -A && git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit -m "..."
```

### 6.1 QEMU forensics toolkit (how the flashing bug was cracked)

Run QEMU headless with a QMP socket + serial log:
```bash
qemu-system-aarch64 -M virt,gic-version=2 -m 2048 \
  -accel tcg,thread=multi -cpu cortex-a72 -smp 1 \
  -bios /opt/homebrew/share/qemu/edk2-aarch64-code.fd \
  -global virtio-mmio.force-legacy=false -device ramfb \
  -device virtio-keyboard-device -device virtio-tablet-device \
  -drive file=build/ojjyos3.img,format=raw,if=none,id=hd \
  -device virtio-blk-device,drive=hd \
  -serial file:/tmp/ojjyos-serial.log -display none \
  -qmp unix:/tmp/qmp.sock,server,nowait -no-reboot &
```
(Framebuffer address is in the serial log: "Framebuffer: 1024x768 @ 0x...",
1024x768x4 = 3145728 bytes.)

Dump/compare the live framebuffer via QMP (python):
```python
import socket, json
def qmp(cmd, args=None):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect("/tmp/qmp.sock")
    f = s.makefile("rw"); json.loads(f.readline())
    f.write(json.dumps({"execute":"qmp_capabilities"})+"\n"); f.flush(); json.loads(f.readline())
    m = {"execute":cmd}
    if args: m["arguments"]=args
    f.write(json.dumps(m)+"\n"); f.flush(); return json.loads(f.readline())
qmp("pmemsave", {"val":0xbc7a0000,"size":3145728,"filename":"/tmp/fb1.raw"})
# ...then unpack '<%dI' % (1024*768) and diff two dumps:
#   0.000% when idle = healthy (flashing bug showed 35-100%)
```

Send synthetic input via QMP (for the repeat-fix flood test):
```python
qmp("input-send-event", {"events":[{"type":"key","data":{
     "down":True,"key":{"type":"qcode","data":"o"}}}]})
# qcodes: "a".."z", "shift", "esc", "backspace", "delete", "spc"
# Mouse (tablet is absolute): {"type":"rel","data":{"axis":"x","value":-40}}
```

Boot health checks in the serial log: exactly ONE "ojjyOS v3.1.0"
banner (a reboot loop shows several), "[GIC] GICv2 ready" (or v3),
"[VIRTIO-BLK] Attached", both "[VIRTIO-INPUT] Device N attached" lines
(pointer + keyboard), "Entering main loop", and after ~30 s
"[PERSIST] Autosave active". Zero `[INPUT] WARNING: Queue overflow`
lines unless you are deliberately flooding.

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
15. **`virtio-blk`/`virtio-input` single-chain-in-flight model**: `d->next`
    must link to `idx + 1` (not `free_head`), and write sector requests
    must set `VRING_DESC_F_NEXT` alongside `VRING_DESC_F_WRITE`.

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

