# 03 — Target architecture and key decisions

Where ojjyOS is going, so that every card builds toward the same system.
Decisions are numbered `D-0NN`; new ones are appended with
`templates/decision-record.md`. A card that contradicts a decision must
first add a superseding decision.

---

## 1. Layer diagram (end state, ~2031)

```
+---------------------------------------------------------------------------+
|  Apps: Finder, Terminal, ojjyWeb, Mail, Photos, Music, Code, Store, games  |
|  Linux programs (LNX personality)      Windows programs (ojjyWin, WIN)      |
+---------------------------------------------------------------------------+
|  Frameworks: ojjyKit (UI), ojjyMedia, ojjyNet, ojjyStore, Foundation-like  |
|  libojjy (native API)  |  libc (POSIX)  |  ojjyWin DLLs  |  Linux libc (guest) |
+---------------------------------------------------------------------------+
|  System services (user space, supervised by ojjyd):                        |
|  WindowServer · audiod · netconfigd · notifyd · logd · searchd · pkgd ·    |
|  authd/keychaind · updated · powerd · printd · bluetoothd · locationd      |
+---------------------------------------------------------------------------+
|  Kernel (hybrid, C):                                                       |
|  scheduler · threads · VM · ports/IPC · syscalls (native + Linux personality)|
|  VFS + ojjyFS/FAT/exFAT/ext4/ISO · block layer · network stack (v4/v6,TCP,  |
|  UDP, sockets) · driver framework + drivers · security (caps, sandbox)     |
+---------------------------------------------------------------------------+
|  Arch layer: aarch64 (VZ, QEMU, Pi, Apple Silicon*) · x86_64 (QEMU, PCs)    |
+---------------------------------------------------------------------------+
|  UEFI firmware · ojjy-boot (UEFI app) · signed kernel + initial ramdisk     |
+---------------------------------------------------------------------------+
```

## 2. Decisions

### D-001 Hybrid kernel
Drivers, filesystems, block layer and network stack stay **in the kernel**
(like XNU/NT) for performance and simplicity; everything with UI, every
service and every app is a **user-space process** with its own address space.
User-mode drivers are allowed later for risky classes (USB gadgets,
printers) via `DRV-UMD-01`. *Why*: a pure microkernel doubles the work before
anything visible improves; a monolith with in-kernel UI (today) cannot
isolate crashes.

### D-002 Languages
Kernel, drivers, libc, core libraries: **C17** (freestanding, no FP in the
ARM kernel). ojjyKit and apps: C with an object model (refcounted handles,
callbacks); optional C++ allowed in user space for large apps once the C++
runtime exists (`USR-30`). No garbage-collected language in the core.
Scripting: a Lua-like embedded language is *not* planned; shell scripts
cover automation.

### D-003 Address space layout (64-bit, both arches)
- User: `0x0000_0000_0040_0000` – `0x0000_7fff_ffff_ffff` (47-bit),
  null guard at 0–4 MiB, ASLR for stack/heap/mmap/shared libs.
- Kernel: upper half (`0xffff_8000_0000_0000`+), direct map of all physical
  RAM, kernel image, per-CPU areas, vmalloc area.
- ARM: TTBR0 = user, TTBR1 = kernel, 4 KiB granule, 48-bit VA.
  x86_64: 4-level paging, same split.
- Migration: today's kernel runs low/identity mapped; `MEM-05` moves it to
  the upper half.

### D-004 Execution model
- **Process** = address space + handle table + credentials + threads.
- **Thread** = kernel stack + saved context + scheduling state.
- Preemptive, priority-based scheduler with 4 classes: *realtime* (audio,
  input), *interactive* (WindowServer, focused app — boosted),
  *normal*, *background*. Per-CPU run queues, work stealing (`SCH-*`).
- Kernel is preemptible except in short spinlock sections.

### D-005 IPC: ports and messages
Capability-based **ports** (Mach/Zircon-like): a process holds *handles* to
ports; messages carry bytes + handles (to pass shared memory, files, other
ports). Synchronous request/reply fast path + async notifications.
Services publish names in a **bootstrap registry** owned by `ojjyd`.
Shared-memory regions (`vm_object` handles) for big data (window surfaces,
audio buffers). Signals exist only in the POSIX/Linux layer.

### D-006 System call ABI
- Native: `svc #0` (ARM) / `syscall` (x86), number in x8/rax, up to 6 args,
  return value or negative error code. Calls are grouped (process, thread,
  memory, handle, port, file, socket, time, sync, debug). Every pointer is
  validated with the user-copy helpers.
- The legacy `syscall_dispatch()` is deleted in `SYS-10`.
- **Personalities**: a process has a personality (native, Linux) chosen by
  the ELF loader; the Linux personality decodes Linux syscall numbers
  (`LNX-*`). Windows programs use the native ABI through ojjyWin DLLs.
- vDSO-style page for fast time queries (`TIM-06`).

### D-007 Executable formats
ELF64 for native and Linux programs (static first, dynamic linking with
`ld-ojjy.so` in `ELF-05`). PE32+ only inside ojjyWin's user-space loader.
App bundles: `Name.app/` with `Contents/Info.plist`-like `Info.toml`,
`Contents/ojjy/<arch>/binary`, resources, icons (`APP-00` format card).

### D-008 Filesystem layout (macOS-like, POSIX-compatible)
```
/System          read-only, signed OS image (apps, frameworks, kernel modules)
/System/Library  frameworks, fonts, icons, sounds, locales
/Applications    user-installable app bundles (+ /System/Applications)
/Library         machine-wide settings, caches, services' data
/Users/<name>    homes: Desktop, Documents, Downloads, Pictures, Music,
                 Movies, Library (per-user settings, caches, keychain)
/Volumes         mounted disks
/bin /usr /etc /var /tmp /dev /proc   POSIX view (symlinks + virtual FS)
/opt/ojjypkg     packages installed by ojjypkg (prefix), /opt/linux (Linux env)
```
Today's `/System` (OJFS) and `/Users`, `/Library` (RAMFS) map onto this.

### D-009 Filesystem: ojjyFS
Copy-on-write B-tree filesystem (APFS/btrfs-like **design**, original code):
checksummed metadata and data, snapshots (Time Machine-style backups and
update rollback), clones, compression (later), encryption per volume
(`SEC-30`), 4 KiB blocks, extents, POSIX permissions + ACLs + extended
attributes (tags, quarantine). Replaces RAMFS persistence (`FS-*`).

### D-010 Graphics stack
- Kernel: GPU drivers expose a **command-buffer API** (contexts, buffers,
  textures, fences) — `GPU-*`. Initially backed by virtio-gpu 3D
  (Venus/Vulkan or virgl) and a CPU fallback renderer.
- User space: **ojjyGPU** library (small, Vulkan-shaped, original);
  **WindowServer** process composites window surfaces (shared memory or GPU
  textures) with Liquid Glass shaders (blur, refraction, specular).
- Apps draw with **ojjyDraw** (2D vector/text, CPU or GPU) into their
  surfaces; ojjyKit sits on top.
- Wayland protocol bridge for Linux GUI apps (`LNX-30`); Win32 GDI/D3D
  mapped by ojjyWin.

### D-011 Text
Original TrueType/OpenType rasterizer (`TXT-*`) with hinting-free
anti-aliasing at HiDPI, glyph cache, a shaping engine (Latin, Cyrillic,
Greek first; Arabic/Hebrew bidi, Indic, CJK later), Unicode line breaking,
emoji (colour bitmap fonts), font fallback chains. Inter (OFL) remains the
UI font; JetBrains Mono (OFL) for code; Noto (OFL) for coverage.

### D-012 UI toolkit: ojjyKit
Retained-mode widget tree in C: views, layout (stack/grid/flex), controls,
text system, menus, windows, accessibility tree, animations (springs),
Liquid Glass materials as first-class view backgrounds. Apps never draw
their own window chrome. Spec: `15-design-system.md`.

### D-013 Services and startup
`ojjyd` (PID 1) — launchd/systemd-like supervisor: dependency-ordered
startup, on-demand launch via ports, restart on crash, logs to `logd`.
Configuration as plain text (TOML-like) in `/System/Library/Services` and
`/Library/Services`.

### D-014 Security model
Users + groups + POSIX modes + ACLs; administrator group; per-app
**sandbox profiles** (entitlements: files, network, camera, microphone,
location, contacts…) enforced in the kernel; user consent prompts handled
by `authd`; code signing (Ed25519) for system, packages and bundles;
secure boot chain (signed kernel verified by ojjy-boot); full-disk
encryption; W^X, ASLR, stack canaries, KASLR, SMEP/PAN.

### D-015 Packages and updates
- `ojjypkg`: pacman-like CLI, `.ojpkg` = tar + zstd + manifest + Ed25519
  signature; repositories `core`, `extra`, `community`, and **OUR** (user
  build scripts `OJBUILD`, AUR-like).
- System updates: A/B system volumes (ojjyFS snapshots), atomic switch,
  automatic rollback if boot fails (`SVC-40`).
- Store app = graphical front end for packages + bundles.

### D-016 Compatibility
- **Linux**: syscall personality in the kernel + `/proc`, `/sys`, `/dev`
  emulation; ARM64 Linux binaries natively; Wayland bridge for GUI.
- **Windows (ojjyWin)**: user-space PE loader, our own `ntdll`, `kernel32`,
  `user32`, `gdi32`, … implemented on the native API; registry stored as a
  file-backed hive; x86_64→ARM64 binary translator (`WIN-80+`) for x86
  programs on ARM; native on the x86_64 build.

### D-017 Primary platform order
1. ARM64 Virtualization.framework (`ojjyOS.app`), 2. QEMU ARM64 HVF/TCG,
3. QEMU x86_64, 4. real x86_64 UEFI PCs, 5. Raspberry Pi 5,
6. Apple Silicon bare metal (stretch). Every feature must work on 1–3.

### D-018 Source tree (target)
```
boot/                    UEFI bootloader
kernel/arch/{aarch64,x86_64}/   arch code (moves from kernel/src/arch, x86 files)
kernel/core/             scheduler, threads, ipc, syscalls, time
kernel/mm/               pmm, vmm, heap, slab
kernel/fs/               vfs + filesystems
kernel/block/            block layer, partitions
kernel/net/              network stack
kernel/drivers/<class>/  drivers (bus, storage, input, display, audio, net, usb)
kernel/security/         credentials, sandbox, audit
lib/{libc,libojjy,libgpu,libdraw,ojjykit,...}/   user-space libraries
services/<name>/         user-space services (windowserver, audiod, ...)
apps/<Name>/             apps (each its own folder, bundle metadata)
compat/{linux,win}/      compatibility layers (user-space parts)
tools/, tests/, scripts/, sdk/, docs/, roadmap/
```
`kernel/src` is migrated gradually (`QA-20` keeps the build working at
every step).

### D-019 Build system
Make + small Python helpers stay (no CMake/meson dependency); a top-level
`ojjy` build script orchestrates kernel, libraries, services, apps,
packages and images (`SDK-01`). Host toolchain: clang/lld; later
self-hosted (`SDK-40`).

### D-020 ojjyWeb
The browser is a separate app and a multi-process system later
(browser process, renderer processes sandboxed, network service, GPU
process) — details in `27-ojjyweb.md`, to be refined with Jonas.

### D-021 ojjyFS on-disk format — *reserved, recorded by FS-10*
### D-022 GPU backend order — *reserved, recorded by GPU-00*

### D-023 Originality policy for user space and ported software
The **base system** — kernel, drivers, libc, shell and core tools,
services, frameworks (libojjy, ojjyKit, ojjyDraw, ojjyGPU, libweb…) and
all built-in apps — is original code written for ojjyOS, as Jonas
requires. **Third-party open-source software** (git, Python, clang, vim,
LLVM libc++, XWayland, .NET runtime, games…) may be **ported and offered
as optional ojjypkg packages**, never compiled into the base image, with
the licence recorded in the package metadata and `THIRD_PARTY.md`.
Compatibility layers (LNX, WIN) are original; the programs they run are
the user's. Data files with permissive licences (fonts, Unicode/CLDR,
tz database, dictionaries, CA lists) may ship in the base system when
recorded in `THIRD_PARTY.md`. *Why:* keeps ojjyOS Jonas's own work while
making it a useful, Arch-like system with access to existing software.

### D-024 ojjyWin architecture — *reserved, recorded by WIN-00*
### D-025 x86-64 translation strategy — *reserved, recorded by WIN-80*

### D-026 HW-13 staged high-RAM access (2026-10-29, design only)

Refine D-003's migration without relinking the kernel: runtime, map-sized
PMM region/bitmap metadata is boot-reserved below 4 GiB. Legacy/DMA APIs
remain LOW; explicit HIGH/ANY physical APIs are enabled only after a
privileged RAM-only alias at `0xffff800000000000 + PA` is installed
(ARM TTBR1, Intel upper PML4). Checked translation helpers and opt-in
mapped buffers let protected user frames and audited CPU-only browser/TLS
arenas use high RAM; no high identity mapping at USER_BASE, no cacheable
Device alias, no EL0 access to kernel aliases. Use LOW page tables and
owned low copies of the firmware map. MEM-03/04 own the HW13-A/HW13-X
preparation boxes; buddy allocation, page-frame DB, vmalloc, relinking and
Intel ring 3 stay with their existing cards. Rationale, measured maps,
caller inventory and alternatives: `docs/hw-13-memory-design.md`.
Nothing in this decision claims runtime high-RAM support is implemented.

## 3. Migration path from today (the big moves, in order)

1. **Split `compositor.c`** into modules without behaviour change (`QA-20`).
2. **Kernel foundations** (P1): VMM + upper-half kernel → slab heap → threads
   and preemptive scheduler (kernel threads first: network, autosave,
   rendering) → SMP → native syscall ABI → processes from ELF → IPC ports.
3. **Userland** (P2): libc + shell + coreutils as processes; `ojjyd`;
   **WindowServer** moves out of the kernel (surfaces + input events over
   ports); apps ported one by one to ojjyKit processes (Calculator first).
4. **Storage** (P3): block layer + async I/O → ojjyFS on the data partition
   with migration from RAMFS snapshots → USB storage, FAT/exFAT.
5. **GPU** (P4): virtio-gpu 3D, ojjyGPU, GPU-composited WindowServer.
6. **Network/security** (P5), **apps** (P6), **compat** (P7–P8),
   **hardware** (P9), **polish** (P10).

At each move the old path keeps working until the new one is proven by the
same tests; then the old code is deleted in a dedicated card.
