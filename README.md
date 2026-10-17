# ojjyOS v3 (v3.1.0)

**Created by Jonas Lee** (`jo-jo-home` / `ojjy`)

**ojjyOS v3** is a custom 64-bit operating system built entirely from scratch by **Jonas Lee**, featuring a custom kernel, custom UEFI ELF64 bootloader, custom read-only and writable filesystems with disk persistence, and a **macOS Tahoe-inspired blue liquid-glass desktop environment**.

- **100% Original Custom Kernel:** Not based on Linux, BSD, Windows, or macOS. Host operating systems are used strictly as build/VM hosts.
- **Native Dual-Architecture Support (`aarch64` + `x86_64`):**
  - **Apple Silicon Native (`aarch64`):** Runs near-natively on Apple Silicon Macs (M1/M2/M3/M4) via `qemu-system-aarch64 -M virt,gic-version=2 -accel hvf` with `ramfb` GOP display, PL011 UART, PL031 RTC, `virtio-mmio` v2 (`virtio-blk`, `virtio-keyboard`, `virtio-tablet`), ARM Generic Timer, and PSCI power management.
  - **Intel/AMD (`x86_64`):** Runs in QEMU or VirtualBox via UEFI GOP, GDT/IDT/4-level paging, 8259 PIC + PIT, PS/2 keyboard & scroll mouse, PIIX4 ATA/IDE + LRU block cache, CMOS RTC, and ACPI S5 power management.
- **macOS Tahoe Blue Glass Desktop:** Translucent blue glass windows, multi-sample wallpaper blur, traffic-light window controls (close, minimize, maximize/zoom), bottom-right drag resizing, Dock with hover magnification, Menu Bar, Spotlight, Launchpad, Control Center, Mission Control, App Switcher, Desktop icons, and 8 built-in apps.

---

## Feature Matrix: Implemented vs. Tested vs. Planned

Per project documentation standards, features are categorized into **Implemented**, **Tested**, and **Planned**:

### Implemented & Verified (`v3.1.0`)
- **Custom UEFI Bootloader (`boot/ojjy-boot.c`):**
  - Dual-architecture UEFI application (`BOOTAA64.EFI` for `aarch64`, `BOOTX64.EFI` for `x86_64`).
  - Parses ELF64 program headers (`PT_LOAD`), validates `e_machine` (`183` / `62`), zeroes BSS tail, sets up GOP 32-bpp BGRx framebuffer, queries UEFI memory map, and jumps to `e_entry`.
- **Dual-Architecture Kernel Core:**
  - **`aarch64` (`kernel/src/arch/aarch64/`):** EL3/EL2 $\rightarrow$ EL1 transition (`entry.S`), `VBAR_EL1` exception vectors with visual framebuffer crash diagnostics, GICv2 interrupt controller, 1 kHz ARM Generic Timer (PPI 30), PL011 serial UART, PL031 RTC, `virtio-mmio` v2 transport (`virtio-blk`, `virtio-keyboard`, `virtio-tablet`), and PSCI (`hvc`/`smc`) shutdown & reboot. Inherits UEFI identity-mapped page tables.
  - **`x86_64` (`kernel/src/`):** 64-bit long mode entry (`entry.asm`), GDT/TSS, IDT (256 vectors + visual panic screen), 4-level paging with on-demand `paging_map()` (including high-MMIO framebuffers above 4 GB), 8259 PIC with automatic IRQ2 cascade unmasking, 1 kHz PIT timer, 16550 UART, PS/2 keyboard (with `0xE0` extended scancodes & Super/Cmd keys) and 3-button scroll mouse, ATA PIO (LBA28/LBA48) + 64-sector LRU block cache, CMOS RTC, and ACPI S5 power-off + 8042 reset.
  - **Physical Memory Manager (`memory.c`):** Bitmap page allocator initialized from the UEFI memory map, dynamically reserving `[_kernel_start .. _kernel_end)` and the GOP framebuffer.
- **Filesystems & Disk Persistence (`kernel/src/fs/`):**
  - **VFS (`vfs.c`):** Unified mount table (`/`, `/Users`, `/Library`), path normalization (`.`, `..`, duplicate slashes), per-mount `fs_data` isolation.
  - **OJFS (`ojfs.c`):** Custom read-only system filesystem embedded in the kernel (`/` with `/System` and `/Applications`).
  - **RAMFS (`ramfs.c`):** Writable filesystem with first-fit free-list data pool allocator and node reuse on unlink, mounted independently at `/Users` and `/Library`.
  - **Disk Persistence (`persist.c`):** Snapshots `/Users` and `/Library` to the `ojjyOS Data` GPT partition (LBA `67584`) with CRC-32 verification. Restores automatically on boot, autosaves every 30 seconds when dirty, and flushes on `sync`, `shutdown`, and `reboot`.
- **macOS Tahoe Blue Glass Desktop (`kernel/src/ui/`):**
  - **Window Management:** Close (red), Minimize to Dock (yellow), Maximize/Zoom (green) traffic lights, `Cmd+Q` close, titlebar drag, and bottom-right corner drag resize (`360x220` minimum).
  - **Desktop & Overlays:** Desktop icons (`/Users/guest/Desktop` + `ojjyHD` volume icon), top Menu Bar (click `ojjy` for About, active app name, status cluster + clock for Control Center), Dock with magnification & bounce animations, Spotlight (`Cmd+Space`), Launchpad (`Cmd+L` with centered full app names), Control Center (`Cmd+C` with Wi-Fi/Bluetooth/Dark Mode/Reduced Transparency toggles and **Restart** / **Shut Down** buttons), Mission Control (`Cmd+M` live window cards), and App Switcher (`Cmd+Tab` with Super-release activation).
  - **Accessibility & Preferences:** Persisted in `/Library/Preferences/system.conf` (Dark/Light mode, 12/24-hour clock, Dock size/magnification, mouse speed, keyboard shortcuts, **Reduced Transparency**, and **Reduced Motion**).
  - **Built-In Applications:** **Finder**, **Terminal**, **TextEdit**, **Notes**, **Calendar**, **Settings**, **Preview**, **Web** (offline hypertext browser over `/System/Web/*.page`), and **About ojjyOS** (creding Jonas Lee).

### Tested
- **Freestanding Cross-Compilation (`clang`):** Every C and assembly translation unit compiles cleanly with `-Wall -Wextra -Werror`-clean zero warnings under both `ARCH=aarch64` (`aarch64-unknown-none-elf`) and `ARCH=x86_64` (`x86_64-unknown-none-elf`).
- **Host Unit Test Suites (`tests/`):**
  - `tests/test_fs.c` (41 assertions): OJFS validation, root & nested directory listing, file reads, VFS path normalization, and exact-`n` `strncpy` bounds.
  - `tests/test_ramfs.c` (24 assertions): RAMFS free-list allocation, repeated file growth/shrink/unlink cycles without pool exhaustion, `mkdir` return semantics, and rename validation.
- **Host Asset & Filesystem Builders (`tools/`):** `mkojfs`, `mkicon`, `mkwallpaper`, and `mkarray` build and generate `build/system.ojfs` and `kernel/src/fs/system_fs.h`.

### Planned (Future Milestones)
- **Preemptive Multitasking & Scheduler:** Kernel threads, per-process kernel stacks, and timer-driven context switching.
- **Protected Userspace & Syscall ABI:** Ring 3 (`x86_64`) / `EL0` (`aarch64`) address spaces, per-process page tables, ELF64 userspace process loader, and crash isolation so a faulting app cannot take down the desktop.
- **Network Stack & Online Web Browser:** `virtio-net` / E1000 NIC driver $\rightarrow$ Ethernet/ARP/IPv4/UDP/TCP/DNS/HTTP so `Web.app` can fetch live pages alongside `/System/Web/*.page`.
- **Audio Subsystem:** `virtio-sound` / Intel HDA output for system alert sounds.
- **ARM64 Scroll Wheel Device:** QEMU's `virtio-tablet-device` does not expose a scroll wheel axis; adding a secondary `virtio-mouse-device` or keyboard page-scroll binding on `aarch64` is planned.

---

## Running Natively on Apple Silicon (macOS + QEMU HVF)

On an Apple Silicon Mac (M1/M2/M3/M4), ojjyOS v3 runs natively as an `aarch64` guest using macOS Hypervisor.framework (`-accel hvf`):

### 1. Install QEMU on macOS
```bash
brew install qemu nasm
```

### 2. Build the `aarch64` Disk Image (on Debian ARM64 or Linux build host / container)
Building the full linkable `BOOTAA64.EFI` + `kernel.bin` + GPT disk image requires `gnu-efi`, `ld.lld`, `mtools`, `dosfstools`, and `gdisk`:
```bash
chmod +x scripts/*.sh
ARCH=aarch64 ./scripts/build.sh
```
*(On macOS without `ld.lld`/`gnu-efi`, you can compile-check all kernel objects for both architectures directly with `make -C kernel ARCH=aarch64 objs` and `make -C kernel ARCH=x86_64 objs`.)*

### 3. Launch in QEMU with Apple Silicon HVF Acceleration
```bash
./scripts/run-qemu.sh aarch64
```
`scripts/run-qemu.sh` automatically locates Homebrew's `edk2-aarch64-code.fd` UEFI firmware, enables `-accel hvf` on Apple Silicon (falling back to `-accel tcg` if unavailable), attaches `ramfb`, `virtio-keyboard-device`, `virtio-tablet-device`, and `virtio-blk-device`, and logs serial output to `/tmp/ojjyos-serial.log`.

---

## Building & Running on Debian (`x86_64` or `aarch64`)

### Prerequisites (Debian 12/13)
```bash
sudo apt update
sudo apt install -y \
    build-essential \
    clang \
    lld \
    nasm \
    gnu-efi \
    mtools \
    dosfstools \
    gdisk \
    qemu-system-arm \
    qemu-system-x86 \
    qemu-efi-aarch64 \
    ovmf
```

### Build
```bash
chmod +x scripts/*.sh

# Build for Apple Silicon / ARM64:
ARCH=aarch64 ./scripts/build.sh

# Or build for x86_64 (default on x86_64 hosts):
ARCH=x86_64 ./scripts/build.sh
```

### Run in QEMU or VirtualBox
```bash
# Run in QEMU (aarch64 or x86_64):
./scripts/run-qemu.sh aarch64
./scripts/run-qemu.sh x86_64

# Run in VirtualBox (x86_64):
./scripts/run.sh
```

---

## Running the Host Unit Tests

```bash
mkdir -p build
clang -Wall -Wextra -O2 -Itests -Ikernel/src -o build/test_fs tests/test_fs.c
clang -Wall -Wextra -O2 -Itests -Ikernel/src -o build/test_ramfs tests/test_ramfs.c
./build/test_fs
./build/test_ramfs
```

---

## Keyboard Shortcuts & Console Commands

### Desktop Shortcuts
| Shortcut | Action |
|----------|--------|
| `Cmd+Space` | Toggle Spotlight search |
| `Cmd+L` | Toggle Launchpad |
| `Cmd+C` | Toggle Control Center (unless TextEdit/Notes is focused, where `Cmd+C` copies text) |
| `Cmd+M` | Toggle Mission Control (live window cards) |
| `Cmd+Tab` | Open App Switcher (release `Cmd` or click an icon to activate) |
| `Cmd+Q` | Close active window / quit active app |
| `Cmd+S` / `Cmd+O` | Save / Open file in TextEdit or Notes |
| `Esc` | Dismiss active overlay |

### Console & Terminal Commands
- `ui` / `ui dark` — Launch the macOS Tahoe blue glass desktop
- `ls`, `cd`, `pwd`, `cat`, `touch`, `mkdir`, `rm`, `mv`, `cp`, `echo`, `clear` — Filesystem operations
- `sync` — Immediately checkpoint `/Users` and `/Library` RAMFS state to disk
- `shutdown` / `reboot` — Save writable filesystems to disk and power off or restart via PSCI (`aarch64`) or ACPI S5 / 8042 (`x86_64`)
- `diag`, `mem`, `time`, `apps`, `about` — System diagnostics and credits

---

## License & Attribution

Copyright (c) 2024 Jonas Lee. All rights reserved.
**Created by Jonas Lee.** See [LICENSE](LICENSE) and [BUGFIXES.md](BUGFIXES.md).
