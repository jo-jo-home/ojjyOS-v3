# 02 — Current state (baseline snapshot, 2026-10-22)

The starting point every card builds on. "Now" in cards means this snapshot.
Update the snapshot only when a whole subsystem is replaced (and say so in
the card log). File sizes are approximate line counts.

`main` = `c6da1a9`. ~212k lines in `kernel/src` (of which ~160k are generated
font/icon data). Version string 3.1.0 (`kernel/src/main.c`).

---

## 1. Platforms and boot

| Item | State |
|---|---|
| Architectures | **aarch64** (primary) and **x86_64** — both build on macOS with clang + lld, 0 warnings |
| Runners | `build/ojjyOS.app` (Apple Virtualization.framework, `vmapp/main.swift`) = what Jonas uses; `scripts/run-qemu.sh` (HVF default, TCG with `OJJY_ACCEL=tcg`); x86_64 under QEMU+OVMF |
| Bootloader | `boot/ojjy-boot.c` — own UEFI app, loads the kernel ELF (`PT_LOAD`), passes `BootInfo` (memory map, framebuffer) |
| Kernel entry | ARM: `arch/aarch64/entry.S` (EL2→EL1, FP enable, stack, BSS clear, vectors); x86: `entry.asm`, `gdt.c`, `idt.c`, `paging.c` |
| Disk image | GPT; ESP + read-only OJFS system image + data partition at **LBA 67584** (two 8 MiB persistence slots) + wallpaper pack at **LBA 102400**; builder `scripts/mkimg.sh` + `tools/finalize_image.py` (preserves user data, backups) |
| Display | ARM: own virtio-gpu 2D driver (async, 8 slots), 2880×1800 under VZ/HVF (2× HiDPI of 1440×900); x86: GOP framebuffer |

## 2. Kernel core

| Area | Files | State / limits |
|---|---|---|
| Physical memory | `memory.c` | Bitmap page allocator; `pmm_alloc_bytes()` **cannot free** (bump-style for big buffers) |
| Kernel heap | `memory.c` | 512 KB coalescing free-list heap with magic/double-free detection |
| Virtual memory | `arch/aarch64/user_arch.c`, x86 `paging.c` | Kernel identity-ish mapping; owned page tables for **one** EL0 program; no general VMM, no demand paging |
| Scheduling | `proc.c` | **None.** Process table = bookkeeping (24 entries, PIDs, tick estimates). Single main loop, cooperative; `compositor_wants_fast_loop()` decides sleep |
| User programs | `user.c`, `user_image.c`, `arch/aarch64/user_entry.S`, `programs/` | Bounded synchronous EL0 runner (ARM only): one static ELF at a time, 100 ms budget, 6-call SVC ABI (pid, uid, uptime, write, exit…), 19 test programs in `sysroot/System/Programs/*.elf`. Intel: unsupported |
| Syscalls | `proc.c syscall_dispatch()` | Legacy raw-pointer dispatcher — **must never be exposed to user space** |
| Interrupts | `arch/aarch64/gicv2.c` (GICv2/v3), x86 `idt.c` + PIC | Timer 1 kHz; input polled from the main loop |
| Timers/clock | `timer_a64.c`, `pl031.c`, x86 `timer.c` (PIT), `drivers/rtc.c` | Monotonic ticks; RTC; no NTP yet (Track N: N4) |
| Power | `psci.c`, `drivers/acpi.c` + `acpi_aml.c` | Shutdown/reboot; tiny AML `_S5_` decoder; no sleep states |
| Randomness | `random.c` | ChaCha20 DRBG seeded from virtio-rng/timers |
| SMP | — | Single CPU (`-smp 1`, extra cores parked) |
| Debugging | `serial_core.c`, `panic.c`, `drivers/diagnostics.c` | Serial log (`[TAG] …` lines), panic screen; no debugger stub |

## 3. Drivers

| Driver | Files | State |
|---|---|---|
| PCI (ARM ECAM) | `arch/aarch64/pci.c` | Enumeration for VZ/QEMU virtio-pci; x86 has no PCI yet |
| virtio transport | `arch/aarch64/virtio.c` | MMIO v2 + PCI modern; negotiated queue depth fix |
| virtio-blk | `virtio_blk.c` | Synchronous single request in flight |
| virtio-net | `virtio_net.c` | Works (DHCP lease under QEMU and VZ) |
| virtio-gpu | `virtio_gpu.c` | 2D only, async flush |
| virtio-input | `virtio_input.c` | Keyboard + absolute tablet + wheel |
| virtio-console | `virtio_console.c` | VZ serial log |
| virtio-rng | `virtio_rng.c` | Entropy |
| xHCI + USB HID | `xhci.c`, `usb_hid.c` | Directly attached keyboard/pointer only (VZ uses this); no hubs, hot-plug, mass storage |
| x86 legacy | `drivers/ps2_*.c`, `ata.c`, `acpi.c`, `rtc.c` | PS/2 keyboard/mouse, PIO ATA disk |
| Driver model | `drivers/driver.c` | Simple registry + IRQ dispatch |
| Input core | `drivers/input.c` | Shared key translation, repeat policy (450/35 ms), modifiers |

## 4. Storage

| Item | State |
|---|---|
| VFS | `fs/vfs.c` — mount table with per-instance `fs_data`, path normalisation, per-user access checks (S4/S5 hardening) |
| OJFS | `fs/ojfs.c` — read-only system image (apps, icons, pages, programs) |
| RAMFS | `fs/ramfs.c` — `/Users`, `/Library`: **256 nodes, 1 MiB data each**, in RAM |
| Persistence | `fs/persist.c` — whole-RAMFS snapshots to the data partition, two CRC-checked slots, autosave every 30 s, fallback to older slot |
| Bundles | `fs/bundle.c` — `.app` manifest parsing |
| Limits | No real on-disk filesystem, no large files, no permissions bits, no USB drives, no other filesystems |

## 5. Networking (Track N owns TLS/X.509/clock)

| Item | State |
|---|---|
| Stack | `net/net.c`: Ethernet, ARP, IPv4, ICMP (ping), UDP, DHCP, DNS; `net/tcp.c` (8 connections) |
| TLS | `net/tls.c`: TLS 1.3 client (x25519, ChaCha20-Poly1305, AES-GCM). Certificates verified since NET-02 (2026-10-27: `crypto/x509*.c`, 51 built-in roots, CertificateVerify). No TLS 1.2 |
| Crypto | `crypto/`: SHA-256/384/512, AES-GCM, ChaCha20-Poly1305, x25519, big integers, RSA verify, ECDSA P-256/384 verify |
| HTTP | `web/http.c`: HTTP/1.1 client, chunked, gzip/deflate, cookies hook, redirects, POST |
| Tools | Terminal `ifconfig`, `ping`, `nslookup`, `curl` (`ui/netcmd.c`) |
| Missing | Sockets API for programs, IPv6, server sockets, Wi-Fi (no hardware in VMs), firewall |

## 6. Graphics and UI

| Item | State |
|---|---|
| Compositor + all apps | `ui/compositor.c` — **~8,700 lines**: window manager, desktop, menu bar, Dock, Launchpad, Spotlight, Mission Control, Control Center, login/setup, and every built-in app (kernel-hosted) |
| Rendering | `gfx.c`: anti-aliased spans, rounded rects, shadows, gradients, Liquid Glass material (`gfx_liquid_glass`), glass rims, sprite cache; CPU only |
| Text | `uifont.c` + generated `uifont_data.c` (Inter, prerendered sizes at 1×/2×), `webfont.c` (browser sizes), `font.c` JetBrains Mono for console |
| Icons/cursor | `ui/icons_data.c` (generated by `tools/mkicons.py`), `cursor_image.c` |
| Animation | Layer + ghost system, menu/Dock easing, Reduce Motion |
| Appearance | Light/Dark/Auto, 8 accents, Clear/Tinted glass, menu bar glass/solid, corner radius, Terminal theme setting |
| Wallpapers | 17 Tahoe pictures (local only), JPEG decoder, picture pack, Settings grid |

## 7. Apps (all kernel-hosted inside `compositor.c` unless noted)

Finder, Terminal, TextEdit, Notes, Preview, Calendar, Calculator
(`ui/calcmath.c`), Activity Monitor, Settings, About, **Web** (ojjyOS Web:
`ui/browser*.c` + engine in `web/`), login window, setup assistant. Bundles
in `sysroot/Applications/*.app`. One window per app.

### Browser engine (Track D, 2026-10-19)
`web/dom.c` (HTML parser), `web/css.c` (cascade, `@layer`, `var()`,
media queries), `web/layout.c` (block/inline/flex/grid/table/float/
absolute), `web/paint.c`; images `image/` (PNG, GIF, JPEG incl.
progressive, WebP, SVG). Tabs, omnibox, bookmarks, history, downloads,
find, zoom, cookies, gzip. **No JavaScript.**

## 8. Accounts and security

`users.c`: multiple users, PBKDF2-HMAC-SHA256 (10k iterations), CSPRNG
salts, admin-gated account creation, per-user home isolation at VFS level,
lock screen. No sandbox, no code signing, no disk encryption, kernel not W^X.

## 9. Tests and tooling

- Host suites (`scripts/test.sh`, also under ASan/UBSan): `test_acpi_aml,
  test_calc, test_crypto, test_framebuffer, test_fs, test_gfx, test_image,
  test_input, test_jpeg, test_net, test_persist, test_pubkey, test_ramfs,
  test_user, test_virtio_pci, test_virtqueue, test_wallpack, test_web,
  test_xhci`.
- Live QEMU tests: `tests/qemu_*.py` (smoke, pointer, perf, apps, files, net,
  browser, web tour, appearance, wallpapers, design tour, animation).
- VZ tests: `tests/vz_boot.py`, `tests/vz_smoke.py`, `scripts/test-vmapp.sh`,
  `tests/test_vmapp.swift`.
- Host renderer for the web engine: `tests/web_render.c`.
- Generators: `tools/mkfont.py`, `mkuifont.py`, `mkicons.py`, `mkcursor.py`,
  `import_wallpapers.py`, `finalize_image.py`.

## 10. Active parallel work at the time of writing

- Track D (`desktop-polish`): `DESKTOP_PLAN.md` D2–D9 remain
  (notifications, snapping, live resize, Terminal themes, app polish).
- Track N (`net-web`): `NET_WEB_PLAN.md` N2–N4 (X.509, certificate
  verification, SNTP) and TLS 1.2.

These are represented in the roadmap as P0 cards that point at those plans.
