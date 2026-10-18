# Native VM Plan — ojjyOS.app on Apple's Virtualization framework (2026-10-18)

Jonas: "sure implement the rest qemu is laggy and i want it to feel smooth
like a native virtualbox windows aarch64 vm".

Goal: replace QEMU (on Apple Silicon) with our own Mac app built on Apple's
Virtualization.framework (VZ) — the same hypervisor stack Parallels/UTM-Apple
use: Metal-backed display view, native full screen and Retina scaling, real
USB keyboard/pointer. QEMU stays supported (tests, Intel host, fallback).

This file is the plan and the live log. Every step ends with verification
and a local commit. Nothing is pushed.

## Probe results (bootloader flight recorder, `\ojjyboot.log` on the ESP,
read with `tools/esp_read.py`; probe host `vmapp/probe.swift`)

| Item | QEMU virt (today) | Apple VZ |
|---|---|---|
| Firmware | edk2 | Apple EFI (VZEFIBootLoader) |
| RAM | 0x40000000.. | 0x70000000..0xF0000000 (2 GiB) |
| Kernel at 0x41000000 | ok | **not RAM** -> AllocatePages fails |
| GOP | ramfb linear / virtio-gpu BltOnly | virtio-gpu BltOnly (no linear fb) |
| GIC | v2 @0x08000000 (v3 under HVF) | **v3**: GICD 0x10000000, GICR 0x10010000 |
| Timer PPIs (GTDT) | phys 30, virt 27 | phys 30, **virt 27** |
| PSCI | HVC | HVC (FADT arm-boot 0x3) |
| Devices | virtio-mmio slots @0x0A000000 | **PCIe** ECAM 0x40000000, BARs preassigned |
| Serial | PL011 0x09000000 | none -> **virtio-console** (PCI 0:5) |
| RTC | PL031 0x09010000 | none -> EFI time from the bootloader |
| Keyboard/pointer | virtio-input | **USB xHCI** (PCI 0:9, Apple 106b:1a06), HID |
| ACPI | yes | FACP, GTDT, APIC(MADT), MCFG only |

VZ PCI bus 0: 1 virtio-net (bar0 0x1_0003_0000), 5 virtio-console
(0x1_0002_0000), 6 virtio-blk (0x1_0001_0000), 7 virtio-gpu (0x1_0000_0000),
8 virtio-rng (0x1_0004_0000) — virtio BAR0s are 64-bit **above 4 GiB**, BAR2
(MSI-X) at 0x5000x000; xHCI bar0 0x50001000, bar1 0x50000000.

## Steps

- N1 Link the ARM kernel at **0x71000000** (RAM on both machines).
- N2 `arch/aarch64/platform.c`: discover the machine from ACPI (MADT GICD/GICR,
  MCFG ECAM, GTDT virtual-timer PPI, SPCR UART if any). QEMU keeps its known
  devices (virtio-mmio, PL011, PL031) only when it is QEMU.
- N3 Page tables: 2 MiB granularity below 4 GiB from the UEFI memory map
  (RAM normal, everything else device), IPS 40-bit, device mapping for the
  PCI 64-bit BAR window above 4 GiB (kernel tables only).
- N4 Timer: virtual timer (CNTV) everywhere, PPI from GTDT.
- N5 Early console: boot messages kept in a ring buffer; PL011 when present,
  virtio-console once found (flushes the ring).
- N6 PCI enumeration + virtio-pci modern transport; blk, net, rng, gpu,
  console drivers become transport-independent.
- N7 Wall clock: bootloader passes EFI GetTime() epoch; PL031 optional.
- N8 xHCI driver + USB HID (boot keyboard, absolute pointer via report
  descriptor), polled.
- N9 Host app `ojjyOS.app` (`vmapp/`): window + VZVirtualMachineView, native
  full screen, 2x Retina scanout, NAT network, USB input, console log file,
  scripts `scripts/build-vmapp.sh`, `scripts/run-vm.sh`.
- N10 Automated tests on VZ: host control channel (keys, text, clicks,
  screenshots) so the existing flows can run against the native app.
- N11 Docs + handoff; QEMU suites must keep passing at every step.

## Log

- 2026-10-18: probes above. Bootloader now saves `\ojjyboot.log` and dumps
  memory map, configuration tables, ACPI (MADT/MCFG/GTDT/FADT) and PCI.
- N1 done: ARM kernel linked at 0x71000000. QEMU TCG + HVF smoke --protected PASS (build/n1); under VZ the bootloader loads it and jumps (entry 0x71053800).
