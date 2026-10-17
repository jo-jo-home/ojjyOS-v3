# ojjyOS v3.1.0 — Full Audit & Bug-Fix Log

**Project author & creator:** Jonas Lee (`jo-jo-home` / `ojjy`)

This document records the comprehensive audit of the `ojjyOS-v3` repository and every defect resolved across the v3.1.0 rescue, the native Apple Silicon (`aarch64`) port, and the macOS Tahoe desktop & filesystem pass.

---

## 1. Build System & Repository Hygiene

| ID | File(s) | Symptom | Root Cause & Fix |
|----|---------|---------|------------------|
| **A1** | `kernel/Makefile` | `make` aborted with `missing separator` on every target | Line 64 recipe was indented with spaces instead of a tab. Rewrote `kernel/Makefile` with strict tabs and dual-arch (`ARCH=x86_64` / `ARCH=aarch64`) support. |
| **A2** | `scripts/mkfs.sh`, `tools/mkarray.c` | `kernel/src/main.c` failed to compile (`embedded_fs` undeclared) | `xxd -i` with an absolute path emitted `_abs_path_to_system_ojfs` instead of `embedded_fs`. Replaced `xxd -i` with a deterministic C tool (`tools/mkarray.c`) that always emits `embedded_fs` and `embedded_fs_len`. |
| **A3** | `kernel/Makefile` | Header edits did not recompile dependent `.c` files | Added `-MMD -MP` dependency generation and `-include $(DEPS)` across all targets. |
| **A4** | `kernel/src/**`, `.gitignore` | Stale `.o` files committed to git masked build failures | Removed all tracked `.o` artifacts and added `.gitignore` for build outputs and object files. |
| **A5** | `scripts/mkimg.sh` | Disk image GPT partition layout collided with backup GPT | Extended disk image to 128 MB (32 MB EFI System Partition + 64 MB `ojjyOS Data` partition at sector `67584`) and fixed `sgdisk` sector bounds. |

---

## 2. Bootloader, Kernel Entry, Memory & Paging

| ID | File(s) | Symptom | Root Cause & Fix |
|----|---------|---------|------------------|
| **B1** | `boot/ojjy-boot.c`, `kernel/Makefile` | Immediate triple fault / synchronous exception on jump to kernel | `kernel.bin` is linked as an ELF64 executable, not a flat binary, so jumping to byte 0 executed the `\x7FELF` magic header. Rewrote `LoadKernel()` in `boot/ojjy-boot.c` to parse ELF64 headers (`Elf64_Ehdr` / `Elf64_Phdr`), validate `e_machine` (`62` for `x86_64`, `183` for `aarch64`), load each `PT_LOAD` segment at `p_paddr`, zero the `p_memsz - p_filesz` BSS tail, and jump to `ehdr.e_entry`. |
| **B2** | `kernel/src/entry.asm` | `kernel_main(boot_info)` received `NULL` on `x86_64` | `entry.asm` pushed `rdi` (`BootInfo *`) onto the stack located inside `.bss` and then zeroed `.bss` with `rep stosb`, wiping the saved pointer. Preserved `BootInfo *` in callee-saved `rbx` across BSS zeroing (`x19` on `aarch64` `entry.S`). Also fixed `global __bss_start` $\rightarrow$ `extern __bss_start`. |
| **B3** | `kernel/src/entry.asm`, `kernel/src/arch/aarch64/entry.S` | Stack overflow during desktop initialization | Increased kernel stack from 16 KB to 64 KB on `x86_64` and 128 KB on `aarch64`, and moved large temporary `Bundle[]` / `AppWindowState` buffers off the stack into static storage. |
| **B4** | `kernel/src/memory.c`, `kernel/linker.ld`, `kernel/linker-aarch64.ld` | Physical Memory Manager (PMM) handed out pages overlapping kernel `.bss` (wallpaper buffers) | `pmm_init()` hardcoded a 1–4 MB reservation, whereas the kernel spans ~8 MB and links at `0x100000` (`x86_64`) or `0x41000000` (`aarch64`). Exported `_kernel_start` and `_kernel_end` from both linker scripts and reserved `[_kernel_start .. _kernel_end)` dynamically. |
| **B5** | `kernel/src/paging.c` | Page fault when UEFI GOP framebuffer was located above 4 GB (`x86_64`) | Implemented on-demand 4-level page-table allocation in `paging_map()` and explicitly mapped the GOP framebuffer physical range if above 4 GB. |

---

## 3. Interrupts, Input Subsystem & Drivers

| ID | File(s) | Symptom | Root Cause & Fix |
|----|---------|---------|------------------|
| **C1** | `kernel/src/idt.c`, `kernel/src/drivers/driver.c` | Keyboard and mouse completely dead on `x86_64` | `driver_dispatch_irq()` had zero callers in `isr_handler()`. Wired hardware IRQs (`32..47`) in `isr_handler()` to call `driver_dispatch_irq(irq)` before legacy handlers. |
| **C2** | `kernel/src/idt.c` | PS/2 mouse (IRQ 12) and secondary ATA (IRQ 14/15) never fired | Slave PIC cascade line (IRQ 2) was never unmasked. Updated `pic_enable_irq()` to unmask IRQ 2 automatically whenever `irq >= 8`. |
| **C3** | `kernel/src/drivers/ps2_keyboard.c` | No Super/Command key (`Cmd+Space`, `Cmd+Tab`, `Cmd+Q` impossible); arrow keys emitted stray characters | Added `0xE0` extended-scancode state machine supporting `KEY_LSUPER` (`E0 5B`), `KEY_RSUPER` (`E0 5C`), arrow keys, Home/End/PgUp/PgDn/Insert/Delete, and right modifiers. |
| **C4** | `kernel/src/drivers/ps2_keyboard.c`, `kernel/src/arch/aarch64/virtio_input.c` | Holding `Cmd+Tab` and releasing `Cmd` did not dismiss the App Switcher | Posted `INPUT_EVENT_KEY_PRESS` and `INPUT_EVENT_KEY_RELEASE` events for modifier keys (`KEY_LSUPER`, `KEY_RSUPER`, Shift, Ctrl, Alt) on both PS/2 (`x86_64`) and `virtio-input` (`aarch64`) so `compositor_handle_key_release()` receives Super release events. |
| **C5** | `kernel/src/drivers/ps2_mouse.c` | Fast mouse movements jumped backward; mouse init intermittently hung | Fixed 9-bit two's-complement sign extension (`dx - ((status << 4) & 0x100)`), reordered PS/2 mouse initialization to disable streaming before configuration, and validated ACK bytes. |
| **C6** | `kernel/src/main.c`, `kernel/src/ui/compositor.c` | Scroll wheel events were ignored | Added `compositor_handle_scroll()` and wired scroll support across Finder, Terminal, TextEdit, Notes, and Web. |

---

## 4. Native Apple Silicon (`aarch64`) Port Bug Fixes

| ID | File(s) | Symptom | Root Cause & Fix |
|----|---------|---------|------------------|
| **M1** | `kernel/src/arch/aarch64/virtio.c` | Multi-descriptor virtio chains (such as `virtio-blk` 3-descriptor requests) failed or read wrong chain links | `virtio_queue_desc_add()` wrote `d->next = (head + 1) % NUM` instead of `(idx + 1) % NUM` when `next` was true, corrupting chained descriptors past the second link. Fixed to `(idx + 1) % VIRTQUEUE_NUM`. |
| **M2** | `kernel/src/arch/aarch64/virtio_blk.c` | `virtio-blk` sector writes failed on QEMU `virtio-mmio` v2 | For `write=true`, descriptor `d1` (the 512-byte data buffer) set `flags = 0`, overwriting `VRING_DESC_F_NEXT` and detaching the status descriptor `d2`. Fixed `d1` flags to `write ? VRING_DESC_F_NEXT : (VRING_DESC_F_NEXT | VRING_DESC_F_WRITE)`. |
| **M3** | `kernel/src/arch/aarch64/*.{c,h}` | Relative includes (`../types.h`) broke when compiling from `kernel/` with `-Isrc` | Standardized all `arch/aarch64` headers and sources to use `src/`-relative paths (`"types.h"`, `"drivers/rtc.h"`, etc.) and added `interrupts_enable()` / `interrupts_disable()` inline helpers in `arch64.h`. |
| **M4** | `kernel/src/arch/aarch64/arch64.c`, `kernel/src/panic.c` | Unhandled ARM64 synchronous exceptions only printed to UART and `panic.c` used `x86_64`-only `cli`/`hlt` | Updated `a64_exception_handler()` to render a full visual crash screen on the GOP/ramfb framebuffer with `ESR_EL1`, `ELR_EL1`, and `FAR_EL1`, and updated `panic.c` to use portable `interrupts_disable()` + `hlt()`. |

---

## 5. Filesystem (VFS, OJFS, RAMFS, Persistence)

| ID | File(s) | Symptom | Root Cause & Fix |
|----|---------|---------|------------------|
| **D1** | `kernel/src/fs/ojfs.c` | Every path lookup below `/` failed (`ls /Applications`, wallpapers, icons all missing) | `mkojfs` writes top-level entries with `parent = 0` (the root directory entry index), while `ojfs.c:find_entry()` searched for `parent = 0xFFFFFFFF`. Fixed `find_entry()` to resolve the root entry index first. |
| **D2** | `kernel/src/fs/vfs.c`, `ojfs.c`, `ramfs.c`, `main.c` | `/Users` and `/Library` were aliased to the same directory tree | Added per-mount `fs_data` context to all `VfsOps` callbacks and allocated distinct `RamFs` instances for `/Users` and `/Library`. |
| **D3** | `kernel/src/string.c` | Off-by-one buffer overflow across the kernel | `strncpy(dst, src, n)` wrote `n + 1` bytes when `strlen(src) < n`. Fixed to write at most `n` bytes, verified by host unit tests. |
| **D4** | `kernel/src/fs/ojfs.c`, `ramfs.c`, `vfs.c` | Path component overflow on long filenames | Clamped `len` to `sizeof(component) - 1` before writing NUL terminators. |
| **D5** | `kernel/src/fs/vfs.c` | `cd ..` in Terminal did nothing | Implemented real `.` / `..` / duplicate-slash path normalization in `vfs_normalize_path()`. |
| **D6** | `kernel/src/fs/ramfs.c` | Saving files repeatedly exhausted RAMFS memory; `ramfs_mkdir` always reported failure | Replaced bump allocator with a first-fit free-list allocator + node reuse on unlink, and fixed `ramfs_mkdir()` to return `0` on success instead of the positive node index. |
| **D7** | `kernel/src/fs/persist.c`, `kernel/src/system.c` | All user edits and settings were lost on reboot | Implemented disk checkpointing (`persist_save` / `persist_load`) of `/Users` and `/Library` RAMFS instances to the GPT `ojjyOS Data` partition (`LBA 67584`) with CRC-32 validation, 30-second dirty autosave, `sync` command, and clean shutdown/restart hooks. |

---

## 6. Desktop Compositor & Built-In Apps

| ID | File(s) | Symptom | Root Cause & Fix |
|----|---------|---------|------------------|
| **E1** | `kernel/src/ui/compositor.c` | Clicking apps in Dock, Launchpad, or Spotlight never opened windows | `launch_app_index()` gated window creation on `bundle_launch()`, which only succeeded for console `About.app`. Built-in apps now open their compositor windows directly by `AppType`. |
| **E2** | `kernel/src/ui/compositor.c`, `main.c` | Cursor jumped on every mouse click and clicked the wrong UI element | Button events used raw `input.c` coordinates instead of the speed-scaled compositor cursor. Added `compositor_get_cursor()` as the single source of truth. |
| **E3** | `kernel/src/ui/compositor.c` | Windows could not be closed, minimized, maximized, or resized | Implemented red (close), yellow (minimize to Dock), and green (maximize/zoom toggle) traffic-light buttons, `Cmd+Q`, and bottom-right corner drag resizing (`360x220` minimum). |
| **E4** | `kernel/src/ui/compositor.c` | Mission Control was an empty stub; Control Center lacked power buttons; Launchpad truncated app names | Implemented real Mission Control window cards (click to unminimize & focus), Control Center Restart/Shut Down/Reduced Transparency buttons, and centered Launchpad labels. |
| **E5** | `kernel/src/ui/compositor.c`, `services.c` | Settings Appearance toggles had misaligned hitboxes; no Reduced Transparency or Reduced Motion options | Aligned toggle hitboxes with drawn switches (`main_x + 210..270`) and persisted `rt=0/1` (Reduced Transparency) and `rm=0/1` (Reduced Motion) in `/Library/Preferences/system.conf`. |
| **E6** | `kernel/src/ui/compositor.c` | Finder list clicks selected the wrong row; `mv`/`cp` in Terminal corrupted source paths; Calendar Day view crashed | Fixed Finder row offset (`+44`), fixed Terminal `mv`/`cp` separate path buffers and multi-chunk `cat`, fixed TextEdit line split/insert bounds, and clamped Calendar Day/Agenda event counts. |
