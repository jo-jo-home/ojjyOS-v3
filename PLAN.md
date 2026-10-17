# ojjyOS v3 Improvement Plan — "Make It Work" Release (v3.1.0)

**Project author & creator:** Jonas Lee
**Plan prepared for:** the ojjyOS-v3 repository (`jo-jo-home/ojjyOS-v3`)
**Focus (per owner's direction):** make the OS actually build, boot, and work. Keep the custom
kernel (no Linux/BSD base). Keep the blue Tahoe theme. UI should stay good but does not need
to be fancy — correctness and a working desktop come first.

---

## Part 0 — Where the project stands today (audit summary)

A full audit of every source file (bootloader, kernel core, drivers, filesystem, UI, build
scripts, host tools) was performed. **The project as committed cannot build or boot.** There
are five independent boot-fatal defects, two "entire subsystem dead" defects, and a long tail
of correctness and memory-safety bugs. Summary:

### A. Build is broken before any code runs
| # | Defect | Where | Effect |
|---|--------|-------|--------|
| A1 | Makefile recipe line begins with a **space**, not a tab | `kernel/Makefile:64` | GNU make aborts parsing; **no target in the kernel can be built at all** |
| A2 | `xxd -i` is invoked with an **absolute path**, so the generated variable is `_abs_path_…_embedded_fs`, but `main.c` references plain `embedded_fs` | `scripts/mkfs.sh:200-201` | `system_fs.h` never compiles — **kernel build always fails** |
| A3 | No header dependencies in the Makefile; stale objects are linked silently | `kernel/Makefile:55` | Wrong binaries after edits |
| A4 | 20 stale `.o` files committed to git | `kernel/src/**` | Repo hygiene; rebuilds silently skipped |

### B. Boot chain is broken in five independent ways
| # | Defect | Where | Effect |
|---|--------|-------|--------|
| B1 | `kernel.bin` is an **ELF**, loaded raw and jumped into byte 0 (the ELF header) | `boot/ojjy-boot.c:276-301`, `kernel/Makefile` | Triple fault immediately |
| B2 | Even as a flat binary, `_start` is linked **last**, not first | `kernel/Makefile` object order | Jump lands in a random function |
| B3 | `entry.asm` pushes the BootInfo pointer **onto the BSS stack, then zeroes all of BSS** — `kernel_main` receives NULL | `kernel/src/entry.asm:44-57` | Crash on first framebuffer access |
| B4 | Bootloader's fallback path reads the kernel anywhere in memory but still reports the entry point as `0x100000` | `boot/ojjy-boot.c:283-301` | Crash if the 1 MB allocation fails |
| B5 | 16 KB kernel stack while the UI puts ~110 KB structs on it | `entry.asm`, `compositor.c`, `services.c`, `main.c` | Guaranteed stack overflow when the desktop runs |

### C. Input and IRQ plumbing is dead code
| # | Defect | Where | Effect |
|---|--------|-------|--------|
| C1 | `driver_dispatch_irq()` has **zero callers** — the PS/2 drivers register into a table that no ISR ever consults | `idt.c`, `driver.c:414` | Keyboard and mouse are completely dead |
| C2 | PIC IRQ 2 (cascade) is never unmasked, so **all** slave PIC IRQs (mouse IRQ 12, ATA IRQ 14/15) are blocked | `idt.c`, `ps2_mouse.c` | Mouse dead even if C1 is fixed |
| C3 | No 0xE0 extended-scancode handling in the keyboard driver | `ps2_keyboard.c` | Super/Command key can never exist; arrows emit phantom events; PrintScreen/Pause type letters |
| C4 | Mouse deltas sign-extended from `int8_t` instead of 9-bit two's complement | `ps2_mouse.c:274-290` | Fast mouse movement jumps the wrong way |
| C5 | Scroll wheel events are produced but **never forwarded to the compositor** | `main.c` | Scroll does nothing in the desktop |

### D. Filesystem is unusable
| # | Defect | Where | Effect |
|---|--------|-------|--------|
| D1 | OJFS reader starts path traversal at `parent = 0xFFFFFFFF`, but the image writer nests top-level entries under the root entry (index 0) | `ojfs.c:67` vs `mkojfs.c:186-190` | **Every path below `/` fails**: no apps listed, no launches, no wallpapers, Finder can't browse |
| D2 | One ramfs instance mounted at both `/Users` and `/Library` | `main.c:491-492` | The two trees are aliases of each other |
| D3 | `strncpy()` writes `n+1` bytes when `strlen(src) < n` | `string.c:97-107` | Off-by-one overflow primitive used all over the kernel |
| D4 | `component[len] = '\0'` with unbounded `len` in path parsing (3 copies) | `ojfs.c:79`, `ramfs.c:118/154` | Stack corruption from a long filename |
| D5 | `vfs_normalize_path` is a documented no-op (`.`/`..` unresolved) | `vfs.c:616-631` | `cd ..` broken in Terminal |
| D6 | RAMFS is a bump allocator: growth leaks the old block, unlink never frees | `ramfs.c` | Pool exhausts after editing a few files; all saves fail until reboot |
| D7 | OJFS multi-instance ops use a `current_instance` global | `ojfs.c` | Second OJFS mount would serve the wrong image |

### E. Desktop shell cannot start
| # | Defect | Where | Effect |
|---|--------|-------|--------|
| E1 | `bundle_launch()` gates every app launch and only "About" has a handler — **all other apps silently fail to open** | `services.c:192-196`, `compositor.c:1402` | Dock/Spotlight/Launchpad clicks do nothing; no Finder window at startup |
| E2 | `app_registry_init()` puts a ~24 KB `Bundle[24]` array on the 16 KB stack | `services.c:122` | Stack overflow at UI init; `cmd_apps()` in `main.c` does the same (~16 KB) |
| E3 | Window raise copies a ~110 KB `AppWindowState` onto the stack | `compositor.c:4012` | Stack overflow on any window click |
| E4 | Compositor tracks its own cursor with a speed multiplier while button events arrive with *input-subsystem* coordinates | `compositor.c`, `main.c` | Cursor teleports on every click; clicks hit-test the wrong target |
| E5 | `console_printf`/`serial_printf` don't parse the `-` flag | `console.c` | `ls` and `diag` output is garbage |

### F. Selected correctness bugs (highlights — full list in BUGFIXES.md)
- `mv`/`cp` resolve their source path **in place**, corrupting it (`compositor.c:2268/2290`)
- Dock hit-test boxes are 2× the drawn icons — clicks launch the neighbor (`compositor.c:3695`)
- Finder list rows hit-test 8 px out of phase with drawing (`compositor.c:3753`)
- TextEdit Enter is broken in two of three cases; typing on a full line overwrites instead of inserting
- Calendar Day view iterates an unclamped event count and dereferences uninitialized indices (user-triggerable crash)
- `Cmd+C` is shadowed by Control Center; Finder hijacks plain `r` for rename while you're typing in search
- ATA writes always use LBA28 (silent corruption past 128 GB); block cache hardcodes device 0
- PMM reserves kernel memory only up to 4 MB while the kernel spans ~8 MB (its BSS is 4 MB of wallpaper buffers)
- Paging identity-maps only 4 GB; a framebuffer above 4 GB faults on first pixel write
- RTC century register read can produce year 165xx on hardware without register 0x32

---

## Part 1 — Plan: refine, find bugs, and fix them

Ordered so the OS boots first, then input works, then the filesystem works, then the desktop
works, then apps behave correctly. Every phase is verified by compiling the kernel with a
freestanding x86_64 toolchain and by host-side unit tests of the filesystem and string code
(the full boot image requires Debian/gnu-efi/VirtualBox, which the owner runs).

### Phase A — Make it build and boot
1. **A1** Fix the Makefile tab; add `-MMD` header dependencies; make `kernel.bin` depend on
   `linker.ld`; remove stale `.o` files from git; add `.gitignore`.
2. **A2/B1/B2/B4** Replace the raw-file loading design: the bootloader now **parses the ELF64
   program headers**, loads each `PT_LOAD` segment at its `p_vaddr`, zeroes the `p_memsz`
   tail, allocates pages for the full memory footprint (BSS included), and jumps to the
   entry point from the ELF header. This removes the objcopy/entry-order fragility class
   entirely. The fallback path now fails loudly instead of jumping to an empty address.
3. **B3** Preserve the BootInfo pointer in `rbx` (callee-saved) across the BSS clear; enlarge
   the kernel stack to 64 KB and remove the giant stack users (see E2/E3).
4. **A2** Replace the fragile `xxd | sed` embed with a tiny host tool (`tools/mkarray.c`)
   that deterministically emits `embedded_fs`/`embedded_fs_len`. No more xxd dependency.
5. Bootloader hardening: `SetWatchdogTimer(0)`; use the loaded-image device handle first to
   find the ESP; only accept 32-bpp BGRx GOP modes (also validating the current mode);
   free `QueryMode` buffers; two-pass RSDP search that really prefers ACPI 2.0.
6. PMM: reserve the real kernel image range using the `_kernel_end` linker symbol (replaces
   the hardcoded 1–4 MB guess); guard against a zero descriptor size; count only usable
   memory as "total".
7. Paging: implement a real `paging_map()` that creates page tables on demand, and map the
   framebuffer explicitly if it lives above 4 GB.

### Phase B — Make input work
1. **C1** Bridge the IDT to the driver model: `isr_handler()` now dispatches hardware IRQs
   through `driver_dispatch_irq()` first, then falls back to legacy IDT handlers (the timer).
2. **C2** `pic_enable_irq()` automatically unmasks the IRQ 2 cascade for any IRQ ≥ 8.
3. **C3** Extended-scancode handling in the keyboard driver: 0xE0 prefix state machine; map
   `E0 5B/5C` to Super keys (finally gives the OS a Command key); `E0 1D/38` → right
   Ctrl/Alt; arrows/Home/End/Delete/Pause/Menu handled without phantom events. CapsLock and
   NumLock toggle only on the initial press (not auto-repeat).
4. **C4** Correct 9-bit two's-complement delta decoding for the mouse.
5. Reorder mouse init to the standard safe sequence (disable ports → configure → knock
   sequence → enable reporting → re-enable ports), validate ACK bytes, add real timeouts,
   and make probe fail when the port test doesn't pass.
6. **C5** Forward scroll-wheel events into the compositor with a new
   `compositor_handle_scroll()` and make apps scroll (Finder, TextEdit, Terminal, Preview).

### Phase C — Make the filesystem work
1. **D1** Fix `find_entry()` to resolve the root entry index first (matches what `mkojfs`
   actually writes). This single fix makes apps, wallpapers, Finder and Spotlight work.
2. **D3** Fix `strncpy` to write exactly `n` bytes.
3. **D4** Clamp the component-terminator index in all three path parsers.
4. **D5** Implement real path normalization in the VFS (resolve `.`, `..`, duplicate
   slashes) and use it in every public VFS entry point; ramfs refuses to create nodes
   named `.`/`..`/empty.
5. **D2/D7** Give every filesystem ops a first `fs_data` argument so each mount carries its
   own instance: two separate ramfs instances for `/Users` and `/Library`; OJFS drops the
   `current_instance` global.
6. **D6** RAMFS gets a first-fit free-list allocator for the data pool and node-slot reuse
   on unlink, so saves keep working and deleted files release memory. `O_TRUNC` now requires
   write mode; rename rejects the root and moves-into-own-descendant; readdir NUL-terminates.
7. OJFS `ojfs_validate()` gains structural bounds checks (entry table, string table, and
   per-entry offsets must fit inside the image).
8. Check every `vfs_join_path()` return value (bundle.c, main.c, services.c) and fix
   `mkojfs` unchecked I/O.

### Phase D — Make the desktop work
1. **E1** Decouple app launching from `bundle_launch()`: the compositor opens app windows
   directly by type; `bundle_launch` remains for console-mode launches (About prints to
   console). Dock clicks, Spotlight Enter, Launchpad clicks and default apps now work.
2. **E2** Move the `Bundle[]` arrays in `app_registry_init()` and `cmd_apps()` to static
   storage.
3. **E3** Replace the window-raise struct copy with a static swap buffer plus index fixups.
4. **E4** One source of truth for the cursor: button events hit-test at the compositor's
   own cursor position (the one the user sees), applying the configured mouse speed.
5. **E5** Fix both printf implementations: `-` flag (left align), correct width handling,
   correct `%d`/`%u`/`%x` integer promotion, no walk past a trailing `%`.
6. Implement `compositor_destroy_window()` for real (used by window close buttons).
7. Traffic-light buttons work: red closes the window, yellow minimizes it (dock icon
   restores), windows can be quit properly with `Cmd+Q`.
8. Fix Finder: row hit-test alignment, selected-index clamping after refresh, drag-state
   cleanup, `r` no longer hijacks search input, icon view stops re-reading manifests every
   frame (use the registry icons).
9. Fix Terminal: `mv`/`cp` path corruption, `cat` reads whole files, `~/` prefix boundary,
   `cd ..` works via VFS normalization, backslash-escaped spaces.
10. Fix TextEdit/Notes: Enter split logic, full-line insert behavior, backspace merge at
    full lines, app-level `Cmd+C/X/V` handled before global shortcuts.
11. Fix Calendar: clamped day-view events, agenda row bounds, larger save lines, `|`
    sanitized on save, grid hit-test bounds, Tab applies edits before switching fields.
12. Fix app switcher: mouse support, all running apps shown, releasing Super closes it
    (needs key-release forwarding from main.c).

### Phase E — Verify (COMPLETED ✅)
- [x] Freestanding compile of every kernel translation unit (`clang`, both `aarch64-unknown-none-elf` and `x86_64-unknown-none-elf`), assemble `entry.asm` with `nasm` and `entry.S` with `clang`.
- [x] Host unit tests (`tests/test_fs.c` — 41 assertions; `tests/test_ramfs.c` — 24 assertions): OJFS reader against a real `mkojfs`-generated image; ramfs write/grow/unlink/rename cycles; `strncpy`/normalization edge cases.
- [x] Tools (`mkojfs`, `mkicon`, `mkwallpaper`, `mkarray`) build and run on the host.
- [x] Documented acceptance checklist in `README.md` and `RELEASE.md`.

### Phase F — Native Apple Silicon (`aarch64`) Port & Tahoe Desktop Polish (COMPLETED ✅)
- [x] Full `aarch64` kernel port (`kernel/src/arch/aarch64/`): `entry.S` (EL3/EL2$\rightarrow$EL1, `VBAR_EL1` vectors), `gicv2.c`, `timer_a64.c` (1 kHz ARM Generic Timer), `pl011.c`, `pl031.c`, `virtio.c` (`virtio-mmio` v2 transport), `virtio_input.c` (keyboard + tablet), `virtio_blk.c` (disk), `psci.c` (`SYSTEM_OFF`/`SYSTEM_RESET`), and `linker-aarch64.ld` (`0x41000000`).
- [x] Dual-architecture build system (`kernel/Makefile`, `boot/Makefile`, `boot/ojjy-boot.c`, `scripts/build.sh`, `scripts/mkimg.sh`, `scripts/run-qemu.sh` with Apple Silicon `-accel hvf`).
- [x] RAMFS disk persistence (`kernel/src/fs/persist.c`) to the GPT `ojjyOS Data` partition (`LBA 67584`) with CRC-32 validation, 30s dirty autosave, `sync`, and clean power-off/reboot flush.
- [x] Window resizing (bottom-right drag grip) and green traffic-light Maximize/Zoom toggle.
- [x] Live Mission Control window cards (`Cmd+M`), Control Center Restart/Shut Down/Reduced Transparency buttons, clickable `ojjy` menu bar item (opens About), centered Launchpad labels, Reduced Transparency & Reduced Motion accessibility settings, and PS/2 modifier key release events for `Cmd+Tab`.

---

## Part 2 — Plan: macOS-essential features (COMPLETED ✅)

Guided by "make it more like macOS — essentials like browser, finder, etc", keeping the
focus on function over polish:

1. [x] **Scroll wheel everywhere** (from Phase B) — Finder, Terminal, TextEdit, Notes, and Web.
2. [x] **Working window management** — close (red), minimize (yellow), maximize/zoom (green) traffic lights, bottom-right corner drag resize, running indicators, minimize-to-dock, `Cmd+Q`.
3. [x] **Desktop icons** — `/Users/guest/Desktop` contents render as icons on the wallpaper with double-click to open (text files open in TextEdit, folders in Finder), plus an "ojjyHD" disk icon that opens Finder at `/`.
4. [x] **Web (offline browser) app** — `Web.app` with address bar, back/forward history, scrollable page rendering, and clickable hyperlinks between pages stored under `/System/Web/*.page` (`home`, `about`, `release`, `shortcuts`).
5. [x] **About This ojjyOS** — `About.app` opens a native window (and is reachable by clicking `ojjy` in the top-left menu bar) showing version, author **"Created by Jonas Lee"**, memory, display, and storage status.
6. [x] **Mission Control, Control Center power controls & Accessibility toggles** — Mission Control cards (`Cmd+M`), Control Center Restart/Shut Down buttons, Reduced Transparency (`rt=0/1`), and Reduced Motion (`rm=0/1`).

### Deliberately deferred (next milestones, in order)
1. **M3.1** Real ELF process loading + protected userspace (`EL0` on `aarch64` / Ring 3 on `x86_64`) with per-process page tables, syscall ABI, and preemptive scheduler.
2. **M3.2** AHCI/SATA + NVMe disk support on `x86_64`, `ATA` DMA (bus-mastering).
3. **M3.3** Network stack: `virtio-net` or E1000 driver $\rightarrow$ ARP/IPv4/UDP/TCP $\rightarrow$ DHCP $\rightarrow$ HTTP $\rightarrow$ live pages in `Web.app`.
4. **M3.4** Menu bar dropdown menus, anti-aliased font rendering, sound (`virtio-sound` / Intel HDA), SMP + multicore scheduling.

Everything stays true to the project's identity: custom kernel, blue Tahoe theme,
"Created by Jonas Lee" attribution everywhere it matters.

