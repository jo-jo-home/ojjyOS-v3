# AGENTS.md — ojjyOS v3 (working context for AI sessions)

**READ THIS ENTIRE FILE BEFORE TOUCHING ANYTHING.** It is the handoff
document: what this project is, everything that has happened, the exact
current state, and a precise, ordered checklist of what to do next.
The previous AI session ended mid-task at the point marked in §4.2.

---

## 0. START-HERE CHECKLIST (next session's job)

Work top to bottom. Every item is explained in detail in §4.2.

1. Verify the current state builds (commands in §6) — do not skip this.
2. Finish the aarch64 port: `main.c` ifdefs → `diagnostics.c` guards →
   `kernel/Makefile` ARCH switch → `kernel/linker-aarch64.ld` →
   `boot/ojjy-boot.c` machine check → `boot/Makefile` aarch64 path →
   `scripts/run-qemu.sh`.
3. Compile-verify BOTH architectures + rerun host tests (§6).
4. Commit locally (author "Jonas Lee"). **Never push to a remote** —
   Jonas pushes himself when he says so.
5. Then do the docs sweep (§4.3) and the small desktop items (§4.4).
6. Update this file at the end of the session so the next AI has an
   accurate handoff. This is a standing rule.

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
  native."* That is why the port exists and why it is the current focus.
- macOS dev machine has: Apple clang 14 (cross-compiles both arches),
  nasm (brew), python3, Rosetta (x86_64 test binaries run under
  `arch -x86_64`). **No gnu-efi on macOS** (bootloader builds on Debian
  only) and **no ld.lld** — kernel *objects* verify locally; full *links*
  happen on Debian.

---

## 2. Repository layout

```
ojjyOS-v3/
├── AGENTS.md               THIS FILE (handoff; update at session end)
├── PLAN.md                 improvement plan (bug-fix plan + feature plan)
├── README.md, RELEASE.md, ARCHITECTURE.md, DESKTOP.md, APPS.md,
│   FILESYSTEM.md, VISUALS.md   docs (STALE — need the §4.3 update pass)
├── boot/
│   ├── ojjy-boot.c         UEFI bootloader (ELF64 loader; both arches)
│   └── Makefile            builds BOOTX64.EFI (x86 only for now)
├── kernel/
│   ├── Makefile            ARCH=x86_64 (default) | aarch64  [switch TODO]
│   ├── linker.ld           x86_64 script (base 0x100000)
│   └── src/
│       ├── main.c          entry, console shell, splash art, power seq
│       ├── system.c/h      power requests (shutdown/restart/sync flags)
│       ├── arch_power.h    arch_power_off()/arch_reboot() interface
│       ├── types.h         types + per-arch asm helpers (both arches)
│       ├── arch/aarch64/   THE PORT (all device/platform code, see §4.1)
│       ├── gdt/idt/paging/timer(PIT)/serial(16550)/entry.asm  x86-only
│       ├── serial_core.c   shared serial_puts/puthex/printf
│       ├── memory.c        PMM from UEFI memory map (arch-neutral)
│       ├── framebuffer.c font.c console.c string.c panic.c  (neutral)
│       ├── drivers/        driver.c model, input.c, ps2_*, ata, block_cache,
│       │                   acpi (x86 S5), blockdev.h + blockdev_x86.c,
│       │                   diagnostics.c (needs #ifdef guards, TODO)
│       ├── fs/             vfs, ojfs (read-only), ramfs (writable),
│       │                   bundle, persist (disk checkpoints)
│       └── ui/             compositor.c (whole desktop + all apps, ~4.7k
│                           lines), services.c, theme.c
├── scripts/                build.sh mkfs.sh mkimg.sh run.sh  (+run-qemu.sh TODO)
├── tools/                  mkojfs mkicon mkwallpaper mkarray (host tools)
├── sysroot/                files packed into the OJFS image
│   └── System/Web/*.page   the offline Web app's pages
├── tests/                  host-side unit tests (test_fs.c, test_ramfs.c)
└── build/                  outputs (gitignored)
```

Git state: one local commit ahead of origin (`0dbaea6`), then a second
local commit containing the aarch64 WIP + this file (made at handoff).
**Do not push.**

---

## 3. History — what already happened

### Session 1 (the rescue, commit `0dbaea6`)

Jonas asked to improve the repo. A five-way parallel audit found the
project **could not build or boot at all**. Highlights of what was fixed
(all verified: x86_64 freestanding compile of every TU, nasm assembly of
entry.asm, 65 host-side unit tests):

**Build/boot (was fatal):**
- `kernel/Makefile` recipe indented with a space → make couldn't parse.
- `mkfs.sh` `xxd -i <abs path>` produced wrong C variable names → kernel
  never compiled. Replaced by `tools/mkarray.c` (deterministic
  `embedded_fs`/`embedded_fs_len` header).
- `entry.asm`: BootInfo pointer was pushed onto a .bss stack and then the
  BSS (including the pushed value) was zeroed → `kernel_main(NULL)`.
  Fixed (pointer survives in `rbx`). Also `__bss_start/__bss_end` were
  declared `global` instead of `extern` — the file had never assembled.
- Bootloader jumped into an ELF header (raw load). Rewrote `LoadKernel()`
  to parse **ELF64 program headers**, load each PT_LOAD at its link
  address, zero the BSS tail, jump to the real `e_entry`.
- PMM reserved only 1–4 MB while the kernel spans ~8 MB (BSS holds 4 MB of
  wallpaper buffers). Now reserves to the `_kernel_end` linker symbol.
- `paging_map()` implemented for real (on-demand page tables) + explicit
  mapping of framebuffers above 4 GB.

**Input (was completely dead):**
- `driver_dispatch_irq()` had **zero callers** — keyboard/mouse IRQs were
  never delivered. Wired into `isr_handler()`.
- PIC IRQ2 cascade never unmasked → all slave-PIC IRQs dead.
  `pic_enable_irq()` unmasks it automatically for IRQ ≥ 8.
- No 0xE0 extended-scancode handling → **no Super/Cmd key existed at
  all**. Added (arrows, Home/End/PgUp/PgDn/Ins/Del, right mods, Super
  0x5B/0x5C, Menu).
- Mouse deltas: 9-bit two's-complement decoded wrong (fast motion jumped
  backwards). Fixed.
- Scroll events existed but nothing consumed them →
  `compositor_handle_scroll()` + scrolling in Finder/Terminal/TextEdit/
  Notes/Web.
- Mouse init reordered to the safe sequence (config before streaming,
  ACK validation, probe that can actually fail).

**Filesystem (was unusable):**
- OJFS `find_entry()` started at `parent=0xFFFFFFFF` but the writer nests
  top-level entries under the root entry index (0) → **every path below
  `/` failed**. Fixed: resolve the root entry index first.
- One ramfs instance mounted at both `/Users` and `/Library` (aliased
  trees). `VfsOps` gained a `fs_data` first parameter everywhere;
  ramfs/ojfs are per-instance now.
- Kernel `strncpy` wrote n+1 bytes (exact-n now; tested).
- `component[len]='\0'` unbounded in 3 path parsers (clamped now).
- RAMFS bump allocator leaked on every grow/unlink → pool died after a
  few saves. Now a first-fit free-list + node-slot reuse.
- `vfs_normalize_path` was a no-op → real `.`/`..` resolution
  (Terminal `cd ..` works).
- `ramfs_mkdir` returned a node *index* instead of 0/-1 → every mkdir
  "failed". Caught by the host tests.

**Persistence & power (new):**
- Writable state **survives reboot**: `fs/persist.c` snapshots the two
  RAMFS instances (flat structs) byte-for-byte to the "ojjyOS Data" GPT
  partition (`OJJY_DATA_START_SECTOR = 67584`, must match mkimg.sh),
  restores at boot. Autosave every 30 s when dirty + on
  shutdown/reboot/`sync`.
- ACPI S5 power-off (RSDP→XSDT→FADT→PM1a_CNT + \_S5 AML scan) and 8042
  reset reboot in `drivers/acpi.c`; `system.c` request plumbing;
  `shutdown`/`reboot`/`sync` in console + Terminal + UI.
- mkimg.sh: 128 MB image = 32 MB ESP + data partition (sgdisk extents
  fixed — the old one collided with the backup GPT).

**Desktop:**
- Apps could never launch (window opening was gated on `bundle_launch()`,
  which only "About" satisfied). Windows now open directly by app type.
- Working traffic lights (close + minimize; dock restores), real
  `compositor_destroy_window`, Cmd+Q closes the window.
- Cursor teleport on click fixed (compositor cursor is the single source
  of truth; `compositor_get_cursor()` feeds hit-testing).
- Dock hit boxes halved; Finder row math fixed (+36→+44 + scroll);
  drag-state leaks fixed; per-frame manifest reloads removed;
  selected-index clamping; app-level Cmd+C/X/V un-shadowed from global
  Cmd+C.
- Terminal: `mv`/`cp` in-place path corruption fixed, `cat` reads whole
  files, `cd ..` works, `terminal_resolve_path` returns 0/-1 and every
  caller checks it.
- TextEdit: Enter split fixed; no overwrite at full lines; terminator
  hardening on line shifts.
- Calendar: fixed a **user-triggerable crash** (unclamped day-view event
  loop dereferenced garbage indices), `|` sanitized on save, Tab applies
  edits, grid/agenda hit-tests bounded, `calendar_load` no longer drops a
  final line lacking `\n`.
- New: **desktop icons** (`/Users/guest/Desktop` + "ojjyHD" disk icon,
  double-click to open), **Web app** (offline browser over
  `/System/Web/*.page` tiny markup: `# heading`, `>id Label` links;
  address bar/back/forward/scroll; pages ship in sysroot; honest about
  being offline), **About window** (version, "Created by Jonas Lee",
  memory), app switcher shows 8 + mouse + Super-release dismiss.
- Boot experience: ASCII-art splash ("ojjyOS" in block letters) with
  staged progress bar, `[ OK ]` welcome banner on the console, boxed
  Terminal welcome, Jonas credit on the power screen.
- App switcher: shows 8 apps, click-to-activate, Super-release dismiss
  (`compositor_handle_key_release`, needs modifier release events from
  the keyboard driver — x86 PS/2 driver still doesn't post modifier
  events; ARM virtio driver does).

**Hygiene:** stale `.o` files removed from git; `.gitignore` added;
commits authored as "Jonas Lee".

### Session 2 (current): the aarch64 port — IN PROGRESS

Jonas: *"i want it to actually support apple silicon... i want it to be
native"* + *"lock in on the kernel... focus and spend a lot of time on the
actual kernel."* Also asked for the boot ASCII art (done, in `0dbaea6`).

Design settled (do not change without a strong reason):
- Target: **QEMU `-M virt,gic-version=2`**, `-accel hvf` on Apple Silicon,
  `-device ramfb` for the GOP framebuffer, PL011 serial, virtio-mmio
  devices (virtio-blk for disk, virtio-keyboard + virtio-tablet for
  input). GICv2 chosen over v3 because it is far simpler and QEMU
  supports it explicitly.
- Kernel links at **0x41000000**. Same bootloader C code as x86; only the
  ELF `e_machine` check differs (62 x86 / 183 aarch64).
- The ARM kernel **keeps UEFI's page tables** (UEFI identity-maps RAM and
  MMIO; `paging_init` is x86-only). Document this in ARCHITECTURE later.
- **Input is polled, not IRQ-driven**: `a64_poll_input()` drains
  virtio-input each main-loop iteration; the 1 kHz generic-timer
  interrupt (GIC PPI 30) keeps the loop alive (`wfi` wakes on it). Only
  the timer uses the GIC — devices never raise interrupts.
- Power via **PSCI**: `hvc #0` (QEMU virt has no EL3), fallback `smc #0`;
  SYSTEM_OFF = 0x84000008, SYSTEM_RESET = 0x84000009.
- Serial split: `serial_core.c` holds the shared puts/puthex/printf
  (built on `serial_putc`); per-arch files provide `serial_init` +
  `serial_putc` (x86: 16550 `serial.c`; ARM: `pl011.c`).
- `types.h` gates all low-level helpers on `__x86_64__` / `__aarch64__`;
  ARM has no-op port-I/O stubs so common code compiles (port-I/O drivers
  are excluded from ARM builds).
- Shared code that MUST stay arch-neutral: `main.c` (mostly), `system.c`,
  `memory.c`, `framebuffer/font/console/string/panic.c`, everything in
  `fs/` and `ui/`, `drivers/driver.c`, `drivers/input.c`, the
  `drivers/blockdev.h` API, `drivers/rtc.h` API, `timer.h` API.

### 4.1 Files the aarch64 port already has (all written, compile-checked individually)

In `kernel/src/arch/aarch64/`:

| File | Status / notes |
|---|---|
| `arch64.h` | arch header: `a64_init`, `a64_irq_handler`, `a64_exception_handler`, `a64_poll_input` |
| `entry.S` | GNU syntax (clang assembles it). EL3/EL2→EL1 drop, stack setup, BSS clear (BootInfo preserved in x19), VBAR_EL1 vector table (16×0x80 aligned 0x800), 128 KB stack in .bss |
| `gicv2.c/h` | GICv2 at GICD 0x08000000 / GICC 0x08010000; init masks all SPIs, sets priorities, enables ctlrs; ack/eoi/enable/disable helpers |
| `timer_a64.c` | generic timer → 1 kHz tick; `timer_init`/`timer_get_ticks`/`timer_sleep`; PPI 30; `a64_timer_isr()` re-arms CNTP_TVAL |
| `pl011.c` | `serial_init`/`serial_putc` on UART 0x09000000 (poll TXFF; LF→CRLF) |
| `pl031.c` | RTC 0x09010000; `rtc_init`/`rtc_read_time`/`rtc_print_time`/`rtc_get_driver`(NULL stub); civil_from_days conversion; weekday |
| `virtio.c/h` | virtio-mmio v2 transport: 32-slot scan at 0x0A000000+0x200*i, device init sequence, `virtio_queue_init`/`desc_add`/`submit`/`poll_used`. Simple single-chain-in-flight allocator (recycles on used) |
| `virtio_devices.h` | attach/poll/ready declarations shared between transport and drivers |
| `virtio_input.c` | keyboard + tablet: Linux keycodes→kernel KeyCode/ASCII tables, modifier state (incl. Super; posts modifier events so the app-switcher Super-release works), BTN_* → mouse buttons, EV_ABS→pixel mapping via `input_get_bounds()`, REL_WHEEL→scroll (QEMU tablet has no wheel — scroll is effectively x86-only for now, say so in docs) |
| `virtio_blk.c` | block device; implements the `block_*` API directly (there is NO separate `blockdev_a64.c`; `drivers/blockdev_x86.c` is the x86 twin). Single `blk_transfer_sector()` helper; one 512-byte buffer per transfer; capacity read from config |
| `psci.c` | `arch_power_off`/`arch_reboot` via PSCI hvc→smc fallback |

Plus in shared code:
- `types.h`: per-arch inline helpers (cli/sti/hlt = DAIF/wfi on ARM).
- `serial_core.c`: shared formatting (split out of serial.c).
- `drivers/input.c/h`: added `input_get_bounds()` (used by the tablet).
- `boot/ojjy-boot.c`, `kernel/linker.ld`, Makefiles: **NOT yet touched**
  for dual-arch — that is exactly where the next session continues.

### 4.2 REMAINING WORK — exact steps (do these in order)

**Step 1 — `kernel/src/main.c` arch ifdefs.**
Current main.c unconditionally includes x86 headers (`gdt.h`, `idt.h`,
`paging.h`, `drivers/ps2_keyboard.h`, `drivers/ps2_mouse.h`,
`drivers/ata.h`, `drivers/rtc.h`, `drivers/block_cache.h`) and calls
`gdt_init()/idt_init()/paging_init()` etc. Required changes:
- Wrap the x86-only includes: `#ifdef __x86_64__ ... #else #include
  "arch/aarch64/arch64.h" #endif`. (Keep `drivers/rtc.h` unconditional —
  it is arch-neutral and both arches implement it.)
- In `kernel_main()`: wrap `gdt_init(); idt_init();` and the
  `pmm_init+paging_init` pairing and the PS/2/ATA/block-cache driver
  registration block in `#ifdef __x86_64__`. For aarch64 call
  `a64_init(boot_info);` (it does GIC + virtio + rtc_init) and skip
  `paging_init()` entirely (UEFI tables stay). `timer_init()` runs on
  both arches (PIT vs generic timer) — keep it unconditional.
- Keep the common init: `driver_subsystem_init()`, `input_init()`,
  `input_set_mouse_bounds`, VFS/mount/persist, bundle init,
  `interrupts_enable()`, splash, welcome banner — all arch-neutral.
- In the main loop, add `#ifndef __x86_64__ a64_poll_input(); #endif`
  right after the power/sync handling (before draining the input queue).
- Note: `serial_init(COM1_PORT)` stays unconditional (PL011 ignores the
  port argument).
- The console-mode `cat`/`ls` etc. and the `ui` command are arch-neutral.

**Step 2 — `kernel/src/drivers/diagnostics.c` guards.**
It includes `ata.h` + `block_cache.h` and calls `ata_print_devices()`.
Wrap those includes and the ATA/cache diagnostics section in
`#ifdef __x86_64__ ... #endif` (keep driver list, memory, input sections
arch-neutral). This file is reachable from the console `diag` command,
so it must compile on both arches.

**Step 3 — `kernel/Makefile` ARCH switch.**
Restructure:
```make
ARCH ?= x86_64
ifeq ($(ARCH),aarch64)
  TARGET_TRIPLE := aarch64-unknown-none-elf
  CFLAGS += --target=$(TARGET_TRIPLE) -mgeneral-regs-only
  # NO -mno-red-zone etc. (x86-only flags), NO -mcmodel=kernel
  LDFLAGS := -T linker-aarch64.ld -nostdlib -static -z max-page-size=0x1000
  ASM_RULE = $(CC) $(CFLAGS) -c   # entry.S via clang (nasm is x86-only)
  SRCS := (common list) + arch/aarch64/*.c
else
  (current x86 flags/linker.ld; SRCS common + x86-only files + entry.asm via nasm)
endif
```
Concrete source split:
- Common: `src/main.c system.c string.c memory.c framebuffer.c font.c
  console.c panic.c serial_core.c` + `src/fs/*.c` (all 6) + `src/ui/*.c`
  (all 3) + `src/drivers/driver.c input.c diagnostics.c`
- x86-only: `gdt.c idt.c paging.c timer.c serial.c entry.asm` +
  `drivers/ps2_keyboard.c ps2_mouse.c ata.c block_cache.c acpi.c
  blockdev_x86.c`
- aarch64-only: `arch/aarch64/entry.S arch/aarch64/arch64.c
  gicv2.c timer_a64.c pl011.c pl031.c virtio.c virtio_input.c
  virtio_blk.c psci.c`
- Keep `-MMD -MP` header deps and the `-include $(DEPS)` line for both.
- Verify `make clean` still works and recipes use TABS (gotcha #1 in §7).

**Step 4 — `kernel/linker-aarch64.ld`.**
Copy of `kernel/linker.ld` with `. = 0x41000000;` instead of 0x100000 and
the same sections (text/rodata/data/bss + `__bss_start/__bss_end` +
`_kernel_end` + /DISCARD/). `ENTRY(_start)`.

**Step 5 — `boot/ojjy-boot.c` machine check.**
Around the ELF validation (`ehdr.e_machine != EM_X86_64`), add:
```c
#if defined(__x86_64__)
  #define OJJY_EXPECTED_MACHINE 62    /* EM_X86_64 */
#elif defined(__aarch64__)
  #define OJJY_EXPECTED_MACHINE 183   /* EM_AARCH64 */
#else
  #error "Unsupported boot architecture"
#endif
```
and compare against `OJJY_EXPECTED_MACHINE`. Everything else in the
bootloader (GOP ramfb-compatible, ELF program-header loading,
AllocateAddress at p_paddr, memory map, EBS retry) is arch-neutral.

**Step 6 — `boot/Makefile` aarch64 target.**
Parameterize like the kernel Makefile (`ARCH ?= $(shell uname -m)` with
explicit override). Debian gnu-efi ships aarch64 headers at
`/usr/include/efi/aarch64`; typical flags:
`--target=aarch64-linux-gnu -I/usr/include/efi -I/usr/include/efi/aarch64
-fshort-wchar -DEFI_FUNCTION_WRAPPER` is x86-only — do NOT define
EFI_FUNCTION_WRAPPER on aarch64; gnu-efi's aarch64 uses libgnuefi-aarch64
+ `elf_aarch64_efi.lds` from `/usr/lib/`. Mirror the structure of the
existing x86 rules; keep objcopy PE→raw step (same for both). You cannot
test this on the Mac — mark it "build on Debian" and rely on Jonas to
report, or double-check gnu-efi's Debian docs.

**Step 7 — `scripts/run-qemu.sh`.** THE script Jonas will run on his Mac.
Requirements:
- `ARCH=${1:-aarch64}` (default aarch64 — that's his machine).
- aarch64: `qemu-system-aarch64 -M virt,gic-version=2 -m 2048 -accel hvf
  (fallback: -accel tcg) -bios <edk2-aarch64 firmware> -device ramfb
  -device virtio-keyboard-device -device virtio-tablet-device
  -drive file=build/ojjyos3.img,format=raw,if=none,id=hd -device
  virtio-blk-device,drive=hd -serial file:/tmp/ojjyos-serial.log`.
  Firmware paths to probe: macOS brew
  `/opt/homebrew/share/qemu/edk2-aarch64-code.fd` (also
  `.../edk2-arm-code.fd` variants), Linux
  `/usr/share/qemu-efi-aarch64/QEMU_EFI.fd`,
  `/usr/share/ovmf/...`. Fail with a clear message + install hint
  (`brew install qemu`) if missing. Note: the disk image must be built
  with `ARCH=aarch64` kernel (mkimg copies `build/kernel.bin`).
- x86 mode: `qemu-system-x86_64 -M pc -m 2048 -bios <OVMF> -vga std
  -drive file=...,format=raw -serial file:...` (PIIX IDE is `-M pc`
  default; the kernel's ATA driver expects it).
- Print the serial-log tail hint like run.sh does.

**Step 8 — verify everything (§6 commands).**
- aarch64: compile every listed source with
  `clang --target=aarch64-unknown-none-elf -ffreestanding
  -mgeneral-regs-only -Wall -Wextra -Wno-unused-parameter -O2 -Isrc -c`
  and assemble `entry.S` with the same clang (`-c`). Fix all errors —
  likely suspects: ARM `%x` formats in serial_printf calls (int casts),
  missing prototypes, `VirtQueueMem` size assumptions.
- x86_64: rerun the full compile loop + nasm + the two host test binaries
  (both must print ALL ... PASSED).
- Commit locally: author "Jonas Lee", message describing the port. NO PUSH.

**Step 9 — update AGENTS.md** (status of each step, anything learned).

### 4.3 After the port: docs sweep (Jonas requirement #8)

- `README.md`: rewrite for 3.1.0 + dual-arch. Sections: what it is
  (custom kernel, not Linux/BSD/macOS), "Created by Jonas Lee"
  prominently, build on Debian (x86) AND `run-qemu.sh` on Apple Silicon,
  feature list split into **Implemented / Tested / Planned** (Jonas's
  requirement: "Distinguish implemented, tested, and planned features").
  Planned must include: multitasking/scheduler, ELF userspace + syscalls,
  protected address spaces, app crash isolation, network stack (→ real
  browser), window resize, sound. Scroll-wheel on aarch64: note QEMU's
  virtio-tablet has no wheel device yet.
- `RELEASE.md`: add QEMU-on-Mac instructions; keep Debian/VBox flow.
- `BUGFIXES.md`: write the full audit→fix log (source material: §3 here
  and PLAN.md; list each finding with file, symptom, fix).
- `LICENSE`: create — "Copyright (c) 2024 Jonas Lee. All rights reserved.
  Created by Jonas Lee." (matches the README statement; do NOT pick an
  open-source license for him).
- `ARCHITECTURE.md`: add the aarch64 platform layer section (§5 table of
  this file is the source), the persistence design, and note that ARM
  keeps UEFI page tables.
- `PLAN.md`: mark completed items.

### 4.4 Small desktop items still open (Jonas's list, post-port)

1. **Window resize** by dragging bottom-right corner + **maximize**
   (green traffic light; store/restore bounds in CompositorWindow — add
   `saved_x/y/w/h`, `maximized`; apps derive layout from win->x/y/w/h so
   this works).
2. **Mission Control** real content: draw open-window cards (app icon +
   title) instead of the stub; click card → raise + close overlay; wire
   the `OVERLAY_MISSION_CONTROL` click case (currently just closes).
3. **Settings → Appearance**: "Reduced transparency" and "Reduced
   motion" toggles (`SettingsState` + save/load keys `rt=`/`rm=`);
   compositor: reduced transparency → opaque panels + skip blur;
   reduced motion → snap animations to final state in
   `update_animations()`.
4. **Control Center honesty**: remove the fake volume/brightness sliders
   (no hardware drivers); label Wi-Fi/Bluetooth rows "(no radio)" or
   hide them; ADD real **Restart** and **Shut Down** buttons calling
   `system_request_restart()`/`system_request_shutdown()` (compositor.c
   already includes `../system.h`; the power sequence +
   safe-to-power-off screen already exist).
5. **Menu bar**: remove the placeholder "WiFi"/"Vol" text (keep the live
   clock; the File/Edit/View labels are decorative but harmless — leave).
6. **x86 PS/2 keyboard**: post KEY_PRESS/RELEASE events for modifier
   keys too (like the ARM driver) so the app-switcher Super-release
   works on x86 (currently returns early after updating modifiers).
   Low risk; watch for the compositor treating bare-Super presses
   sensibly (it does — no shortcut matches a bare Super).

### 4.5 Explicitly deferred (document as Planned, do NOT fake)

Real multitasking/scheduler; ELF userspace processes + syscalls +
protected address spaces; crash isolation (app crash kills the OS today —
in-kernel apps by design for now); network stack + real browser; window
server as separate process; SMP/APIC (x86) & GICv3; sound; resizable
windows live-layout. Jonas approved the priority order; userspace is
M4. When the time comes: design doc first (PLAN.md), then implement.

---

## 5. aarch64 / QEMU virt cheat sheet

| Thing | Address / value |
|---|---|
| RAM base | 0x40000000 |
| Kernel load/link | 0x41000000 |
| GICv2 distributor | 0x08000000 |
| GICv2 CPU interface | 0x08010000 |
| Timer PPI | intid 30 (non-secure EL1 physical timer) |
| PL011 UART | 0x09000000 |
| PL031 RTC | 0x09010000 |
| virtio-mmio slots | 0x0A000000 + i*0x200 (32 slots, version 2 = modern) |
| virtio device IDs | blk=2, input=18 |
| PSCI conduit | `hvc #0` (SYSTEM_OFF 0x84000008, SYSTEM_RESET 0x84000009) |
| Exception levels | UEFI may leave us at EL2 → entry.S drops to EL1 |
| ELF machine | x86=62, aarch64=183 |

virtio-mmio v2 register map used in `virtio.h`: magic 0x000, version
0x004, device id 0x008, status 0x080, queue sel 0x028, num_max 0x030,
num 0x034, ready 0x038, desc 0x040, avail 0x048, used 0x050, notify
0x060, config 0x100. Init order: reset → ACK|DRIVER → features(0) →
FEATURES_OK → queue setup → DRIVER_OK.

x86-only files (excluded from aarch64 builds): gdt.c, idt.c, paging.c,
timer.c (PIT), serial.c (16550), entry.asm, drivers/{ps2_keyboard,
ps2_mouse, ata, block_cache, acpi, blockdev_x86}.c.

---

## 6. Build & verify commands (this Mac)

```bash
cd "~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3"

# Filesystem image + embedded header (works on macOS)
bash scripts/mkfs.sh

# x86_64 compile check (every TU)
cd kernel
for f in src/*.c src/drivers/*.c src/fs/*.c src/ui/*.c; do
  clang -target x86_64-unknown-none-elf -ffreestanding -fno-stack-protector \
    -fno-stack-check -fno-pie -fno-pic -mno-red-zone -mno-mmx -mno-sse \
    -mno-sse2 -mcmodel=kernel -Wall -Wextra -Wno-unused-parameter -O2 \
    -Isrc -c -o /dev/null "$f" || echo "FAIL $f"
done
nasm -f elf64 -o /dev/null src/entry.asm && echo ASM_OK

# aarch64 compile check (per-file, same -Isrc)
clang --target=aarch64-unknown-none-elf -ffreestanding -mgeneral-regs-only \
  -Wall -Wextra -Wno-unused-parameter -O2 -Isrc -c -o /dev/null <file>
clang --target=aarch64-unknown-none-elf -c src/arch/aarch64/entry.S -o /dev/null

# Host unit tests (need build/system.ojfs from mkfs.sh)
clang --target=x86_64-apple-macos13 -O1 -w -Ikernel/src -c tests/test_fs.c -o /tmp/tt.o
clang --target=x86_64-apple-macos13 -O1 -w -Ikernel/src -c kernel/src/fs/ojfs.c -o /tmp/ojfs.o
clang --target=x86_64-apple-macos13 -O1 -w -Ikernel/src -c kernel/src/fs/vfs.c -o /tmp/vfs.o
clang --target=x86_64-apple-macos13 -O1 -w -Ikernel/src -c kernel/src/fs/ramfs.c -o /tmp/ramfs.o
clang --target=x86_64-apple-macos13 -O1 -w -c kernel/src/string.c -o /tmp/string.o
clang --target=x86_64-apple-macos13 /tmp/tt.o /tmp/ojfs.o /tmp/vfs.o /tmp/string.o -o /tmp/test_fs
arch -x86_64 /tmp/test_fs          # expect: ALL OJFS/STRING TESTS PASSED (32)
# test_ramfs.c the same way (ramfs.o replaces ojfs.o) — expect 33 passed

# Local commit (author Jonas; NEVER push)
git add -A && git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit -m "..."
```

Full image build + real boot verification happens on **Debian**
(gnu-efi, ld.lld, nasm, sgdisk, mtools, VirtualBox). Apple Silicon runs
via `scripts/run-qemu.sh` (to be written, §4.2 step 7). QEMU itself is
NOT yet installed on this Mac — `brew install qemu` if you want to
smoke-test, or leave it to Jonas.

---

## 7. Gotchas learned the hard way (do not regress)

1. **Makefile recipes need TABS.** The original repo shipped a
   space-indented recipe; nothing built. Double-check any Makefile you
   edit (the write tool preserves what you give it — use real tabs).
2. **Never push to a remote without Jonas's explicit go-ahead.**
3. **BSS-clear vs stack**: on both arches, anything saved on the stack
   before the BSS wipe is destroyed — keep the BootInfo pointer in a
   callee-saved register (x86 `rbx`, ARM `x19`).
4. **VfsOps carry `fs_data` as the first parameter of every op.** Mounts
   are per-instance; do not reintroduce globals (`current_instance`).
5. **strncpy must write exactly n bytes** (tested in tests/test_fs.c).
6. **OJFS root entry**: top-level entries have `parent == root entry
   index` (0 in the writer), NOT 0xFFFFFFFF. `find_entry()` must resolve
   the root index first (this was THE filesystem bug).
7. **x86 IRQ wiring**: `isr_handler()` → `driver_dispatch_irq(irq)` for
   vectors 32..47 first, legacy IDT handler fallback; `pic_enable_irq`
   for IRQ ≥ 8 must also unmask the IRQ2 cascade.
8. **Big structs never go on the kernel stack** (AppWindowState ~110 KB;
   window reordering uses static `state_scratch`; Bundle arrays in
   `app_registry_init`/`cmd_apps` are static).
9. **Compositor cursor is the single source of truth** — main.c passes
   `compositor_get_cursor()` coords into `compositor_handle_mouse`
   (mouse-speed mismatch caused cursor teleports before).
10. **printf set**: `console_printf`/`serial_printf` support
    `%s %d %u %x %p %c %%` with `-`/`0`/width/`l`/`ll`. Cast explicitly
    when passing 64-bit values as `%d`/`%x` (int promotion matters).
11. **Persistence checkpoint = raw RAMFS instance bytes.** Changing
    `RAMFS_MAX_NODES`/`RAMFS_DATA_SIZE` invalidates old checkpoints via
    the size check (intentional).
12. **Data partition start sector 67584 is a contract** between
    `scripts/mkimg.sh` and `kernel/src/fs/persist.h`. Change both
    together.
13. **aarch64 has no PIT/PS2/ATA/CMOS** — generic timer / virtio-input /
    virtio-blk / PL031 instead. Common code must use the neutral APIs
    (`timer_get_ticks`, `input_post_*`, `block_*`, `rtc_read_time`).
14. **The compositor's `ui` command** is the desktop entry; console mode
    stays as the fallback shell. Both are arch-neutral.
15. **`input_get_bounds()`** returns the screen bounds used by absolute
    pointers; `input_set_mouse_bounds` validates non-positive sizes.
16. virtio-blk/input use a **single-chain-in-flight** model with
    contiguous descriptor allocation — do not submit a second chain
    before recycling the first (`virtio_poll_used` resets `free_head`
    to the chain head).

---

## 8. Version & attribution

- Version: **3.1.0** ("Make It Work" release) — `OJJYOS_VERSION` in
  `kernel/src/main.c`. Keep in sync: boot art (main.c splash), About
  window (compositor.c `draw_about_window`), `sysroot/System/version.txt`
  (mkfs.sh), serial banner.
- Author: **Jonas Lee** in every user-visible surface (README, About,
  splash, serial banner, power screen, `sysroot/System/version.txt`).
  AI contributions are improvements to HIS project — never add AI
  co-author lines to user-facing strings, and commit as
  `user.name="Jonas Lee"`.
