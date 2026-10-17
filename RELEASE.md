# ojjyOS v3.1.0 Release & Run Guide (Apple Silicon QEMU + Debian / VirtualBox)

**Created by Jonas Lee**

This guide covers building and running **ojjyOS v3.1.0** both **natively on Apple Silicon (`aarch64`) via QEMU + HVF** and on **`x86_64` via QEMU or VirtualBox**.

---

## 1) Native Apple Silicon (`aarch64`) on macOS via QEMU

Because VirtualBox on Apple Silicon cannot run `x86_64` VMs and ojjyOS v3.1.0 supports `aarch64` natively, the recommended way to run ojjyOS on an M1/M2/M3/M4 Mac is with `qemu-system-aarch64` and macOS Hypervisor.framework (`-accel hvf`).

### A) Install macOS Host Prerequisites
```bash
brew install qemu nasm
```

### B) Verify Kernel Compilation Locally on macOS
Apple's built-in `clang` cross-compiles both `aarch64` and `x86_64` kernel objects out of the box:
```bash
./scripts/mkfs.sh
make -C kernel clean && make -C kernel ARCH=aarch64 objs
make -C kernel clean && make -C kernel ARCH=x86_64 objs
```

### C) Build Full Disk Image (`build/ojjyos3.img`)
Full ELF linking (`ld.lld`) and UEFI bootloader building (`gnu-efi`, `mtools`, `dosfstools`, `gdisk`) run on Debian Linux (e.g., a lightweight Debian ARM64 VM or container on your Mac):
```bash
sudo apt update
sudo apt install -y build-essential clang lld nasm gnu-efi mtools dosfstools gdisk
chmod +x scripts/*.sh
ARCH=aarch64 ./scripts/build.sh
```
This produces:
- `build/BOOTAA64.EFI` — ARM64 UEFI bootloader
- `build/kernel.bin` — ARM64 ELF64 kernel linked at `0x41000000`
- `build/ojjyos3.img` — 128 MB GPT disk image (32 MB EFI System Partition + 64 MB `ojjyOS Data` persistence partition)

### D) Boot on macOS with HVF Acceleration
```bash
./scripts/run-qemu.sh aarch64
```
What `scripts/run-qemu.sh` configures automatically:
- `-M virt,gic-version=2 -cpu host -accel hvf -m 2048` (falls back to `-cpu cortex-a72 -accel tcg` on non-Apple-Silicon hosts)
- UEFI firmware (`edk2-aarch64-code.fd` from Homebrew `/opt/homebrew/share/qemu/`)
- `-device ramfb` (UEFI GOP framebuffer at 1024x768x32)
- `-device virtio-keyboard-device` and `-device virtio-tablet-device`
- `-device virtio-blk-device` backed by `build/ojjyos3.img`
- `-serial file:/tmp/ojjyos-serial.log`

---

## 2) Debian 13 (`x86_64` or `aarch64`) Build Setup

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
  git

chmod +x scripts/*.sh

# Build for the host architecture (or pass ARCH=aarch64 / ARCH=x86_64 explicitly):
./scripts/build.sh
```

Artifacts produced in `build/`:
- `build/BOOTAA64.EFI` (when `ARCH=aarch64`) or `build/BOOTX64.EFI` (when `ARCH=x86_64`)
- `build/kernel.bin` — ELF64 kernel binary
- `build/ojjyos3.img` — 128 MB bootable GPT disk image
- `kernel/src/fs/system_fs.h` — embedded OJFS system image header

---

## 3) VirtualBox (`x86_64` Host) — Step-by-Step

1. Run `ARCH=x86_64 ./scripts/build.sh` and `./scripts/run.sh`, or configure manually:
2. **System → MotherBoard:** Enable **EFI**, RAM **2048 MB**
3. **Display → Screen:** Video Memory **128 MB**, Graphics Controller **VMSVGA**
4. **Storage:** Add **IDE (PIIX4)** controller and attach `build/ojjyos3.img` (or `build/ojjyos3.vdi`) as **IDE Primary Master**
5. **Serial Ports:** Port 1 **COM1**, Mode **Raw File** $\rightarrow$ `/tmp/ojjyos-serial.log`

---

## 4) Debugging & Serial Log

Watch real-time kernel and compositor logs:
```bash
tail -f /tmp/ojjyos-serial.log
```

Common troubleshooting:
- **`UEFI firmware not found` when running `run-qemu.sh`**: Run `brew install qemu` on macOS or `sudo apt install qemu-efi-aarch64 ovmf` on Debian.
- **`Architecture mismatch` in bootloader**: Ensure `build/ojjyos3.img` was built with matching `ARCH=aarch64` or `ARCH=x86_64`.
- **Disk persistence**: Writable changes in `/Users` and `/Library` are automatically saved to the `ojjyOS Data` GPT partition every 30 seconds when dirty, on `sync`, and when clicking **Restart** or **Shut Down** in Control Center.

---

## 5) End-User Guide (macOS Tahoe Blue Glass Desktop)

- **Boot Splash & Console:** Boots with an ASCII-art `ojjyOS` splash screen and staged progress bar into the interactive console (`help`, `ls`, `cat`, `diag`, `sync`, `shutdown`, `reboot`, `ui`).
- **Launching the Desktop:** Type `ui` (or `ui dark`) at the console prompt.
- **Window Controls:**
  - **Red traffic light** or `Cmd+Q`: Close window
  - **Yellow traffic light**: Minimize window to Dock (click its Dock icon or Mission Control card to restore)
  - **Green traffic light**: Maximize / Zoom window to fill desktop (click again to restore previous bounds)
  - **Bottom-right corner grip**: Drag to resize window (`360x220` minimum)
- **Menu Bar:**
  - Click **`ojjy`** on the far left to open **About ojjyOS** ("Created by Jonas Lee")
  - Click the **right status/clock cluster** to open **Control Center** (toggle Wi-Fi, Bluetooth, Dark/Light appearance, Reduced Transparency, or trigger **Restart** / **Shut Down**)
- **Overlays:**
  - `Cmd+Space` — Spotlight search
  - `Cmd+L` — Launchpad (full centered app names)
  - `Cmd+C` — Control Center
  - `Cmd+M` — Mission Control (shows cards for all open windows; click any card to focus/restore)
  - `Cmd+Tab` — App Switcher (release `Cmd` or click an icon to switch)
