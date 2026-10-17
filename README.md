# ojjyOS v3 (v3.1.0)

**Created by Jonas Lee** (`jo-jo-home` / `ojjy`)

**ojjyOS v3** is a custom 64-bit operating system built entirely from scratch by **Jonas Lee**, featuring a custom kernel, custom UEFI ELF64 bootloader, custom read-only and writable filesystems with disk persistence, and a **macOS Tahoe-inspired blue liquid-glass desktop environment**.

- **100% Original Custom Kernel:** Not based on Linux, BSD, Windows, or macOS. Host operating systems are used strictly as build/VM hosts.
- **Native Dual-Architecture Support (`aarch64` + `x86_64`):**
  - **ARM64 guest on Apple Silicon (`aarch64`):** Runs in QEMU's `virt` machine using TCG emulation, `ramfb` GOP display, PL011 UART, PL031 RTC, `virtio-mmio` v2 (`virtio-blk`, `virtio-keyboard`, `virtio-tablet`), ARM Generic Timer, and PSCI power management. This is not bare-metal Mac support; HVF acceleration is not enabled by the current runner.
  - **Intel/AMD (`x86_64`):** Runs in QEMU or VirtualBox via UEFI GOP, GDT/IDT/4-level paging, 8259 PIC + PIT, PS/2 keyboard & scroll mouse, PIIX4 ATA/IDE + LRU block cache, CMOS RTC, and ACPI S5 power management.
- **macOS Tahoe Blue Glass Desktop:** Translucent blue glass windows, multi-sample wallpaper blur, First-Boot Account Setup Assistant, Tahoe Lock/Login Screen with salted SHA-256 credentials, traffic-light window controls (close, minimize, maximize/zoom), bottom-right drag resizing, Dock with hover magnification, interactive Menu Bar dropdowns (`ojjy`, `File`, `Edit`, `View`, `Window`), Desktop right-click context menu, Spotlight, Launchpad, Control Center, Mission Control, App Switcher, Desktop icons, and 11 built-in apps.

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
- **User Accounts & Salted SHA-256 Credentials (`kernel/src/users.c`):**
  - **Salted FIPS 180-4 SHA-256 Password Hashing:** Domain-separated 32-bit salt + 256-bit SHA-256 digest with constant-time verification (`hash_equal_ct`), stored in `/Library/Accounts/users.db` and persisted across reboots.
  - **First-Boot Account Setup Assistant & Tahoe Lock/Login Screen:** When no accounts exist (`users_count() == 0`) or **+ New Account** is clicked, presents a glass Setup Assistant card (`Full Name`, `Username`, `Password`, `Confirm`). When locked or logged out, presents the macOS Tahoe Lock Screen with clock, user avatar initials, `@username` badge, masked password field, **Sign In**, **Switch User**, and **+ New Account**.
   - **Per-User Home Directories (`/Users/<username>`):** Automatically provisions `/Users/<username>/Desktop` and `/Users/<username>/Documents` with starter `Welcome.txt`, `Notes.txt`, and `Calendar.txt`. Apps bind to the logged-in user's home directory. These are not enforced user-access boundaries; file ownership checks and account-security hardening remain future work.
- **Filesystems & Disk Persistence (`kernel/src/fs/`):**
  - **VFS (`vfs.c`):** Unified mount table (`/`, `/Users`, `/Library`), path normalization (`.`, `..`, duplicate slashes), per-mount `fs_data` isolation.
  - **OJFS (`ojfs.c`):** Custom read-only system filesystem embedded in the kernel (`/` with `/System` and `/Applications`).
  - **RAMFS (`ramfs.c`):** Writable filesystem with first-fit free-list data pool allocator and node reuse on unlink, mounted independently at `/Users` and `/Library`.
   - **Disk Persistence (`persist.c`):** Alternates two 8 MiB checkpoint slots in the data partition (LBA `67584`). CRC-32 protects the payload and commit header. Payload is flushed before its header; restore falls back to the previous valid checkpoint. Legacy v1 snapshots are structurally validated and migrated without overwriting the old snapshot first. Autosaves use bounded batches between input/render turns; explicit `sync`, shutdown, and restart wait for completion. Failed saves remain dirty and cancel shutdown/restart.
- **macOS Tahoe Blue Glass Desktop (`kernel/src/ui/`):**
  - **Window Management:** Close (red), Minimize to Dock (yellow), Maximize/Zoom (green) traffic lights, `Cmd+Q` close, titlebar drag, and bottom-right corner drag resize (`360x220` minimum).
  - **Interactive Menu Bar & Desktop Context Menu:** Dropdown menus for `ojjy` (About, Settings, Activity Monitor, Lock Screen, Switch User, Restart, Shut Down), `File`, `Edit`, `View`, and `Window`, plus clickable active username pill (`Settings → Users & Accounts`) and right-click desktop context menu (New Folder on Desktop, New Text Note, Open Home in Finder, Open Terminal, Open Calculator, Toggle Dark/Light, Lock Screen).
  - **Desktop & Overlays:** Desktop icons (`/Users/<user>/Desktop` + `ojjyHD` volume icon), Dock with magnification & bounce animations, Spotlight (`Cmd+Space`), Launchpad (`Cmd+L` with centered full app names), Control Center (`Cmd+C` with Wi-Fi/Bluetooth/Dark Mode/Reduced Transparency toggles and **Restart** / **Shut Down** buttons), Mission Control (`Cmd+M` live window cards), and App Switcher (`Cmd+Tab` with Super-release activation).
  - **Accessibility & Preferences:** Persisted in `/Library/Preferences/system.conf` (Dark/Light mode, 12/24-hour clock, Dock size/magnification, mouse speed, keyboard shortcuts, **Reduced Transparency**, and **Reduced Motion**).
  - **11 Built-In Applications:** **Finder**, **Terminal**, **TextEdit**, **Notes**, **Calendar**, **Settings** (including **Users & Accounts**), **Preview**, **Web** (offline hypertext browser over `/System/Web/*.page`), **Calculator**, **Activity Monitor** (live RAM bar, uptime, active user, and kernel driver IRQ/IO table), and **About ojjyOS** (crediting Jonas Lee).

### Tested
- **Freestanding Cross-Compilation (`clang`):** Every C and assembly translation unit compiles cleanly with `-Wall -Wextra` zero warnings and zero errors under both `ARCH=aarch64` (`aarch64-unknown-none-elf`) and `ARCH=x86_64` (`x86_64-unknown-none-elf`).
- **Host Unit Test Suites (`tests/`):**
  - `tests/test_fs.c` (37 assertions): OJFS validation, root & nested directory listing (all 11 `.app` bundles + `/System/Web/accounts.page`), file reads, VFS path normalization, and exact-`n` `strncpy` bounds.
   - `tests/test_ramfs.c` (81 assertions): RAMFS allocation and file operations, accounts, heap allocation, process bookkeeping, and syscall dispatch.
   - `tests/test_input.c`: Real evdev and PS/2 decoders, key mappings, left/right modifiers, delayed repeats, Backspace, queue overflow, and releases.
   - `tests/test_virtqueue.c`: Negotiated queue depths (8/64/128), repeated ring wraps, and 16-bit index rollover.
   - `tests/test_persist.c`: Checksums, fallback, torn writes, flush failures, concurrent edits during autosave, and legacy migration using real RAMFS and a fault-injected block device.
   - `tests/test_image.py`: Data-preserving rebuilds, backups, migration, invalid layouts, and running-VM disk locks (real QEMU check when installed).
   - `tests/qemu_smoke.py`: Opt-in live VM setup/login, typing/Backspace, command responsiveness, sync/shutdown, and cold-boot credential recovery. Always uses a private disk copy.
- **Host Asset & Filesystem Builders (`tools/`):** `mkojfs`, `mkicon`, `mkwallpaper`, and `mkarray` build and generate `build/system.ojfs` and `kernel/src/fs/system_fs.h`.

### Planned (Future Milestones)
- **Preemptive Multitasking & Scheduler:** Kernel threads, per-process kernel stacks, and timer-driven context switching.
- **Protected Userspace & Syscall ABI:** Ring 3 (`x86_64`) / `EL0` (`aarch64`) address spaces, per-process page tables, ELF64 userspace process loader, and crash isolation so a faulting app cannot take down the desktop.
- **Network Stack & Online Web Browser:** `virtio-net` / E1000 NIC driver $\rightarrow$ Ethernet/ARP/IPv4/UDP/TCP/DNS/HTTP so `Web.app` can fetch live pages alongside `/System/Web/*.page`.
- **Audio Subsystem:** `virtio-sound` / Intel HDA output for system alert sounds.
- **ARM64 Scroll Wheel Device:** QEMU's `virtio-tablet-device` does not expose a scroll wheel axis; adding a secondary `virtio-mouse-device` or keyboard page-scroll binding on `aarch64` is planned.

---

## Running on Apple Silicon (macOS + QEMU ARM64)

The current runner uses one ARM64 vCPU under TCG, not Hypervisor.framework acceleration:

### 1. Install QEMU on macOS
```bash
brew install qemu nasm lld
```

### 2. Build the `aarch64` Disk Image
macOS builds use standalone UEFI definitions, clang/lld, and a Python GPT/FAT image builder; no Linux container or gnu-efi is required:
```bash
chmod +x scripts/*.sh
ARCH=aarch64 ./scripts/build.sh
```
Outputs are isolated under `build/aarch64/` and `build/x86_64/`. Shut down a VM before rebuilding its disk. A rebuild preserves the data partition and creates a complete disk backup under that architecture's `backups/` directory. The first per-architecture build imports saved data from legacy `build/ojjyos3.img` if present, leaving the legacy image unchanged. Corrupt or unfamiliar layouts and locked disks are refused instead of reformatted. Backups are retained until you explicitly remove them.

### 3. Launch in QEMU
```bash
./scripts/run-qemu.sh aarch64
```
`scripts/run-qemu.sh` locates firmware, uses TCG with one vCPU, attaches the devices above, and logs to `build/aarch64/serial.log`. Intel logs use `build/x86_64/serial.log`; Homebrew's split Intel firmware is combined into a private writable pflash image. A 1024x768 guest still has aspect-ratio bars on widescreen displays; VNC stretching has not been verified or added.

The VirtualBox runner preserves an existing VDI rather than recreating it and refuses to force-stop a running VM. A VDI has its own saved data; rebuilding a raw QEMU image does not update that VDI.

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
bash scripts/mkfs.sh  # Generate the embedded system filesystem if needed
bash scripts/test.sh

# Optional ASan/UBSan; use a compiler runtime compatible with your macOS:
HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 bash scripts/test.sh

# Optional live check against a fresh build (choose a NEW output directory):
python3 tests/qemu_smoke.py --arch aarch64 \
  --image build/aarch64/ojjyos3.img --output build/smoke-arm --fresh-data
```

`--fresh-data` resets only the newly created private smoke-test disk, never the
source image. The smoke test also rebuilds that private disk and corrupts its
newest checkpoint to verify recovery. Evidence stays in its output directory.

Process entries and syscall dispatch exist, but apps still execute in the kernel.
Activity Monitor's task ticks are estimates, not scheduler measurements. Protected
userspace, app crash isolation, strong password derivation, and enforced file
authorization are not implemented by this reliability pass.

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
| `Ctrl+Cmd+Q` | Lock Screen immediately |
| `Cmd+S` / `Cmd+O` | Save / Open file in TextEdit or Notes |
| `Esc` / `Shift+Esc` | Dismiss active overlay or menu (`Shift+Esc` returns to kernel console) |

### Console & Terminal Commands
- `ui` / `ui dark` — Launch the macOS Tahoe blue glass desktop
- `whoami`, `users`, `adduser <user> <pass> [Full Name]`, `passwd <old> <new>`, `su <user> <pass>` / `login <user> <pass>`, `lock` / `logout` — User account & credential management
- `ls`, `cd`, `pwd`, `cat`, `touch`, `mkdir`, `rm`, `mv`, `cp`, `echo`, `clear` — Filesystem operations
- `sync` — Immediately checkpoint `/Users` and `/Library` RAMFS state to disk
- `shutdown` / `reboot` — Save writable filesystems to disk and power off or restart via PSCI (`aarch64`) or ACPI S5 / 8042 (`x86_64`)
- `diag`, `time`, `apps`, `about` — System diagnostics and credits

---

## License & Attribution

Copyright (c) 2024 Jonas Lee. All rights reserved.
**Created by Jonas Lee.** See [LICENSE](LICENSE) and [BUGFIXES.md](BUGFIXES.md).
