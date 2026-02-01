# ojjyOS v3 Release Guide (Debian 13 + VirtualBox)

This guide makes the project buildable on Debian 13 and bootable in VirtualBox on macOS.

## 1) Debian 13 Environment Setup (copy/paste)

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
  vim-common \
  git

git clone <YOUR_REPO_URL>
cd ojjyos-v3
chmod +x scripts/*.sh

./scripts/build.sh
```

Toolchain choice:
- Bootloader: GCC + gnu-efi
- Kernel: clang + lld

## 2) Build Scripts + Artifacts

Primary build:
```bash
./scripts/build.sh
```

Artifacts:
- `build/BOOTX64.EFI` — UEFI bootloader
- `build/kernel.bin` — kernel binary
- `build/ojjyos3.img` — VirtualBox‑bootable disk image
- `kernel/src/fs/system_fs.h` — embedded OJFS image header

If build fails with `efi.h` missing:
```bash
sudo apt install -y gnu-efi
```

## 3) VirtualBox (macOS host) — Click‑by‑Click (Beginner)

### A) Create the VM
1) Open **VirtualBox**
2) Click **New**
3) Name: `ojjyOS-v3`
4) Type: **Other**
5) Version: **Other/Unknown (64‑bit)**
6) Click **Next**
7) Memory: **2048 MB** → **Next**
8) Select **Use an existing virtual hard disk file**
9) Click the folder icon → choose `build/ojjyos3.img` (or `build/ojjyos3.vdi`)
10) Click **Create**

### B) Set the correct VM options
1) Select `ojjyOS-v3` → click **Settings**
2) **System** → check **Enable EFI**
3) **System** → **Processor** → set **2 CPUs**
4) **Display** → **Video Memory** → **128 MB**
5) **Display** → **Graphics Controller** → **VMSVGA**

### C) Attach disk to IDE (required)
1) **Storage** → click **Controller: SATA** (if present)
2) Click **Add Controller** → choose **IDE (PIIX4)**
3) Under IDE controller, click **Add Hard Disk**
4) Choose **Existing Disk** → select `build/ojjyos3.img` (or `.vdi`)
5) Ensure it shows as **IDE Primary Master**
6) Click **OK**

### D) Boot
1) Select the VM → click **Start**
2) Expect: splash screen → console prompt
3) Type `ui` to enter the Tahoe UI

First boot expectations:
- Boot splash
- Console prompt
- `ui` command launches Tahoe UI

## 4) Debug Guide

Serial logging (VirtualBox):
1) VM Settings → **Serial Ports**
2) Enable **Port 1**
3) Port: **COM1** (0x3F8 / IRQ 4)
4) Mode: **Raw File** → `/tmp/ojjyos-serial.log`

View logs:
```bash
tail -f /tmp/ojjyos-serial.log
```

Interpreting boot failures:
- **No splash** → UEFI bootloader not found (check disk + EFI)
- **Serial stops after VFS init** → filesystem image missing (re‑run `./scripts/build.sh`)
- **No disk detected** → IDE controller missing (use PIIX4 IDE)

Safe mode:
- Not implemented. If UI fails, reboot and stay in console mode (don’t run `ui`).

Known issues:
- Full‑frame blur can be heavy in VirtualBox.
- RAMFS is in‑memory only (no persistence across reboots).

## 5) End‑User Guide (Tahoe‑like)

Desktop tour:
- Top menu bar shows active app + time
- Dock at bottom with magnification
- Spotlight, Launchpad, Control Center overlays

Shortcuts:
- **Super+Space** → Spotlight
- **Super+L** → Launchpad
- **Super+C** → Control Center
- **Super+Tab** → App switcher

Finder:
- Open Finder from Dock
- Browse `/Users`, `/Applications`, `/System`
- Rename: select item → `R`
- Delete: select item → Delete

Wallpaper:
- Open **Settings** → Wallpaper → Tahoe Light/Dark
- Or open **Preview** and click the thumbnail

Shutdown/Restart:
- Not implemented. Close VM window → Power Off (VirtualBox).

## 6) Acceptance Checklist (one‑page)

Done when you see:
- VM boots to splash + console prompt
- `ui` shows glass window + dock + menu bar
- Spotlight opens with Super+Space
- Launchpad opens with Super+L
- Finder opens, list shows Name/Type/Size
- TextEdit opens a file from Finder
- Calendar shows month grid + agenda
- Settings toggles 12/24‑hour clock
