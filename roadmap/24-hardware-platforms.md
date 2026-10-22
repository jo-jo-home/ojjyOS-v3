# 24 — Hardware platforms

Platform order (D-017): (1) ARM64 Apple Virtualization.framework through
`ojjyOS.app` — Jonas's daily runner; (2) QEMU ARM64 (HVF/TCG) for tests;
(3) QEMU x86_64; (4) real x86_64 UEFI PCs and laptops; (5) Raspberry Pi 5;
(6) Apple Silicon bare metal (stretch). Every feature must work on 1–3;
4–6 are P9 integration targets.

Real-hardware cards produce **hardware records**: a file in
`docs/hardware/<machine>.md` with the model, firmware version, what works
and what doesn't, serial/log excerpts and photos, dated.

---

## Virtual platforms

### HW-10 — `ojjyOS.app` (VZ host app) features
- Phase: P2 · Size: M · Status: todo
- Depends: —
- Files: vmapp/main.swift, scripts/build-vmapp.sh, tests/test_vmapp.swift

**Context.** The app boots the custom kernel on VZ with fixed 2880×1800,
USB HID input, virtio devices (NATIVE_VM_PLAN.md).
**Goal.** Preferences window (CPUs, memory, disk size growth, display
size, shared folder, sound on/off, network NAT/bridged), automatic display
resize (pairs with DRV-VIRTIO-04), pause/save state (VZ save/restore on
supported macOS), multiple VM profiles (e.g. "Daily", "Test"), start at
login option, clean "Shut Down" button sending ACPI power to the guest.
**Done when.** All options work and persist; `test_vmapp.swift` covers
them.
**Tests.** Native guard tests + `vz_smoke.py`.
**Pitfalls.** Never touch a disk while the VM runs (lock files exist).
**Refs.** Apple Virtualization framework docs.

### HW-11 — Research: Rosetta for Linux inside ojjyOS
- Phase: P7 · Size: S · Status: todo
- Depends: LNX-08, DRV-VIRTIO-09
- Files: docs/research/rosetta-linux.md

**Goal.** Investigate whether VZ's Rosetta directory share for Linux
guests (`VZLinuxRosettaDirectoryShare`) can run under our Linux
personality to execute x86_64 Linux binaries (licence terms, technical
checks, performance) as a stopgap before WIN-82/WIN-85.
**Done when.** Research note with a go/no-go decision.
**Tests.** Experiment logs.
**Pitfalls.** Respect Apple's licence terms; may only work for real Linux.
**Refs.** Apple Virtualization docs (Rosetta for Linux).

### HW-40 — QEMU x86_64 parity
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-PCI-01, DRV-NET-01, DRV-USB-02, ELF-03
- Files: scripts/run-qemu.sh, tests/qemu_smoke.py

**Context.** TODO.md: "Intel: PCI enumeration, NIC, absolute USB tablet".
**Goal.** x86_64 build reaches feature parity with ARM under QEMU:
networking (e1000 + virtio-pci), USB tablet input, protected programs,
virtio-gpu or Bochs resolution change, sound (HDA).
**Done when.** Full live suite runs on x86_64 QEMU with the same pass
list as ARM.
**Tests.** Full matrix with `--arch x86_64`.
**Pitfalls.** —
**Refs.** —

---

## Real PCs and laptops

### HW-01 — Real x86_64 PC bring-up
- Phase: P9 · Size: L · Status: todo
- Depends: HW-40, DRV-ACPI-01, DRV-STOR-01, DRV-STOR-02, IRQ-03, SMP-01
- Files: docs/hardware/, boot/ojjy-boot.c

**Goal.** Boot from USB on a real desktop PC: UEFI GOP display, ACPI
(MADT/MCFG/FADT + AML), APIC timer and IOAPIC, all cores, NVMe/AHCI
storage, xHCI keyboard/mouse, e1000e/Realtek Ethernet, HDA sound,
shutdown/reboot.
**Done when.** Hardware record shows a desktop reaching the login window
and browsing the web.
**Tests.** Hardware record; serial log via USB-serial or network logging
(KDB-01 netconsole option).
**Pitfalls.** No serial port on most PCs — add network/USB logging and
an on-screen boot log.
**Refs.** —

### HW-02 — Hardware compatibility list
- Phase: P9 · Size: S · Status: todo
- Depends: HW-01
- Files: docs/hardware/README.md

**Goal.** Table of tested machines and components with status, how to
contribute a record, a `hwreport` command that collects IDs (PCI/USB/ACPI
tables) for unsupported hardware.
**Done when.** List published in docs and the Help viewer.
**Tests.** `hwreport` live test in QEMU.
**Pitfalls.** —
**Refs.** —

### HW-03 — Laptop integration
- Phase: P9 · Size: L · Status: todo
- Depends: HW-01, DRV-ACPI-05, DRV-IN-03, DRV-DISP-05, DRV-NET-03, SVC-60
- Files: docs/hardware/laptops.md

**Goal.** A supported laptop model end to end: battery and charging,
lid sleep/resume, touchpad gestures, backlight and keyboard backlight,
Wi-Fi, Bluetooth, webcam, speakers/mic, function keys, external display.
**Done when.** Hardware record shows a day of real use on battery.
**Tests.** Hardware record checklist.
**Pitfalls.** Choose a model with well-documented, common chips (Intel
Wi-Fi, Intel iGPU, I2C-HID touchpad).
**Refs.** —

### HW-41 — ARM64 Windows laptops (stretch)
- Phase: P10 · Size: XL · Status: todo
- Depends: HW-03
- Files: docs/hardware/arm-laptops.md

**Goal.** Investigate Snapdragon X laptops (ACPI on ARM, Qualcomm
peripherals) — likely device-tree/firmware heavy; split if pursued.
**Done when.** Research note + decision.
**Tests.** —
**Pitfalls.** Vendor-specific firmware; may not be feasible.
**Refs.** —

---

## Boards and Apple Silicon

### HW-20 — Raspberry Pi 5
- Phase: P9 · Size: L · Status: todo
- Depends: BOOT-06, DRV-STOR-04, DRV-GPIO-01, DRV-USB-02
- Files: kernel/src/arch/aarch64/platform_rpi5.c, boot/

**Goal.** Boot ojjyOS on a Raspberry Pi 5 (UEFI via community firmware
or direct kernel boot), BCM2712 UART, GIC-400, generic timer, SD card,
RP1 (PCIe) USB and Ethernet, HDMI framebuffer from firmware, GPIO.
**Done when.** Hardware record: desktop on HDMI with USB keyboard/mouse.
**Tests.** Hardware record.
**Pitfalls.** RP1 is complex; framebuffer-only display at first.
**Refs.** Raspberry Pi documentation (BCM2712, RP1 peripherals PDFs).

### HW-30 — Apple Silicon bare metal (stretch)
- Phase: P10 · Size: XL · Status: todo
- Depends: BOOT-06, HW-20
- Files: docs/research/apple-silicon.md

**Goal.** Research and, if feasible, boot on an M-series Mac natively:
chainload via the m1n1 boot loader (an open-source boot tool, used like
firmware — decision record needed), AIC interrupt controller, DART
IOMMU, UART over USB-C debug, framebuffer from iBoot, NVMe (ANS),
USB, keyboard/trackpad (SPI/MTP). Split into many cards if pursued.
**Done when.** Research note with feasibility, using only public
documentation (Asahi Linux docs describe hardware; never copy code).
**Tests.** —
**Pitfalls.** Multi-year effort; huge driver surface; Jonas's main Mac is
the target — never risk his macOS install (separate partition, backups).
**Refs.** Asahi Linux hardware documentation (public wiki).

---

## Media and images

### HW-50 — Installer media and live mode
- Phase: P9 · Size: M · Status: todo
- Depends: SVC-42, FS-24
- Files: tools/mkinstallmedia, scripts/mkimg.sh

**Goal.** Bootable USB image and ISO (hybrid) for both arches with
"Try ojjyOS" live mode (RAM overlay) and "Install ojjyOS".
**Done when.** ISO boots in QEMU (both arches) and on a real PC into live
mode.
**Tests.** QEMU ISO boot test.
**Pitfalls.** ISO + GPT hybrid layout.
**Refs.** El Torito, UEFI.

### HW-60 — Real-hardware performance tuning
- Phase: P10 · Size: M · Status: todo
- Depends: HW-01, SVC-60
- Files: docs/perf/hardware.md

**Goal.** Profile and tune on real machines: CPU frequency governor,
cache-aware scheduling, interrupt moderation, disk queue depths, display
pipeline latency; record benchmarks vs a Linux/Windows baseline on the
same machine for honesty.
**Done when.** Benchmark report published in docs.
**Tests.** Benchmark suite.
**Pitfalls.** —
**Refs.** —
