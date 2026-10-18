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

- N4/N7 backend verified: native boot emits a once-only 5-second health
  sample outside the ISR; separate IRQ delivery count prevents a healthy
  counter from hiding broken interrupt routing. `timer-final-vz/report.json`
  records uptime 5000ms, 3889 delivered timer IRQs, firmware delta -0.088s,
  and correctly advanced RTC calendar. One boot also recorded the Finder
  animation's `[PERF]` frames. Visual menu-clock/animation acceptance remains
  part of N9 app QA, not something this headless check establishes. Both
  builds warning-free and host suite pass (`timer-{host,final-arm,final-x86}.log`).

- N5/N6 checkpoint receipts (2026-10-18): both development builds have
  zero warnings/errors (`build/vz-resume/checkpoint-{arm,x86}.log`), host
  and ASan/UBSan suites pass (`checkpoint-{host,sanitized}.log`). Live
  QEMU smokes pass: ARM TCG, HVF, modern PCI, ramfb, x86, plus pointer QA
  (`checkpoint-{tcg,hvf,pci,ramfb,x86-smoke,pointer}.log`). VZ boot check
  passes (`checkpoint-vz/report.json`), all five virtio devices, real DHCP,
  firmware clock offset 0.104s, source SHA-256 unchanged. No temporary
  power-off checkpoints remain. No installed disk touched; nothing pushed.

- N5/N6 follow-up verification: `build/vz-resume/console-offer-vz/serial.log`
  proves VZ num_queues=4 and offered low console features=0x30000000.
  MULTIPORT is **not offered** by this VZ serialPorts device; the four-queue
  handshake still works and is required for output. Do not describe this
  as MULTIPORT negotiated. QEMU does offer bit 1. Negotiation now intersects
  supported masks with offers after reset/ACK/DRIVER and records the result.
  Host PCI fixture covers reset completion/deadline, separate ACK, feature
  filtering/rejection, BARs above 4GiB, queues, notification and RNG's absent
  device config. QEMU PCI + HVF full protected/persistence smokes pass on
  this negotiation change (`feature-order-{pci,hvf}`); normal/sanitized host
  suites pass. VZ boot check passes with source image unchanged and clock
  within one second of the host. USB input, visible clock/animation QA and
  the app are not yet validated by these boot checks.

- 2026-10-18 (resumed after 1411d0e): N5/N6 now reaches the main loop on
  VZ, with all five virtio devices attached and DHCP 192.168.64.2.
  Evidence: `build/vz-resume/devices/serial.log`; PCI GPU 1440x900,
  GICv3 1024 lines, 24MHz virtual timer, disk checkpoint restore, RNG,
  console replay, network MTU 1500. Private disks/EFI stores only.
  Verified root causes: PARange=1 needs IPS=36 (the old >=2 test selected
  32 and faulted on 4GiB BARs); PCI reset/ACK/DRIVER must be sequenced
  (status readout changed 0 -> 11); DEVICE_READY value 0 means failed
  initialization, value 1 opens the console. VZ requires NET_F_MTU when
  offered (controlled probe: MAC-only 0x20 rejected, MAC+MTU 0x28 accepted);
  RNG has no device-config capability and must not be rejected for that.
  Guarded the early GPU's QEMU-only MMIO scan; console now precedes GPU/GIC.
  Historical "num_queues unimplemented" and downstream marker claims are
  unverified: old probes sometimes stopped at stale earlier markers.
  Added actual-transport host PCI tests and opt-in QEMU PCI smoke coverage,
  plus `tests/vz_boot.py` for repeatable private-disk VZ boot/clock/DHCP.

- 2026-10-18: probes above. Bootloader now saves `\ojjyboot.log` and dumps
  memory map, configuration tables, ACPI (MADT/MCFG/GTDT/FADT) and PCI.
- N1 done: ARM kernel linked at 0x71000000. QEMU TCG + HVF smoke --protected PASS (build/n1); under VZ the bootloader loads it and jumps (entry 0x71053800).
- N2 done (branch native-vm, commit to follow): pci.h/pci.c (ECAM config via
  mmio.h, bus-0 enumeration, BAR decode, Memory+BusMaster enable, pci_find());
  platform.c/pci.c added to kernel/Makefile; platform_init(boot_info) is now
  the first statement of kernel_main on aarch64 with a "[PLATFORM] ..." line
  after the banner; unused RTC_BASE removed from pl031.c. Evidence: both
  builds 0 warnings (build/n2-build-{arm,x86}.log), scripts/test.sh all
  suites passed (build/n2-tests.log), QEMU TCG smoke --protected PASS
  (build/n2-tcg), HVF smoke --protected PASS (build/n2-hvf), pointer test
  PASS with 1.9-3.4 ms overlay latency and zero scene repaints for desktop
  moves (build/n2-ptr).
- N3 done: user_arch_init maps the low 4 GiB at 2 MiB granularity with RAM
  blocks classified from the UEFI memory map (QEMU 0x40000000.., VZ
  0x70000000..0xF0000000), everything else Device; 4-8 GiB mapped as Device
  1 GiB blocks for VZ's 64-bit virtio BARs; IPS set to 40 bits when
  ID_AA64MMFR0.PARange >= 2; user pages still rejected >= 4 GiB physical.
  Evidence: builds 0 warnings (build/n3-build-*), tests pass (build/n3-tests),
  QEMU TCG + HVF smoke --protected PASS (build/n3-tcg, build/n3-hvf),
  pointer PASS (build/n3-ptr).
- N5/N6 (uncommitted at pause, see HANDOFF_NATIVE_VM.md "PROGRESS
  UPDATE"): VirtioDev transport (mmio + PCI modern), all drivers
  converted, 16-bit mmio accessors, panic prints FAR + caller. Console
  driver written for Apple's full virtio-serial (rx0/tx0/ctrl-in/ctrl-out
  = q0..q3; DEVICE_READY→PORT_READY→PORT_OPEN on q3, data on q1; constants
  from Linux uapi). Two real bugs found via the new panic diagnostics and
  fixed: (1) N3 mapped QEMU's type-0 ramfb region as Device → first fb
  write faulted; block_is_ram now treats types 0-7,9,10 as RAM.
  (2) fb_use_backbuffer allocates the back buffer from the PMM but the
  boot splash calls it BEFORE pmm_init → zeroed bitmap "allocated"
  0x100000 (Device-mapped) → fault; PMM now refuses allocations before
  pmm_init (pmm_ready), splash falls back to direct scanout.
  VZ probe progress: kernel boots through GICv3 + PCI scan + device
  attach; console control messages on queue 3 NOT yet verified end-to-end
  (earlier tests used wrong queues/constants). Evidence: build/n5-*,
  build/n6-build/test logs, /tmp/vz-serial.log runs.
- Paused 2026-10-18 ~16:30 at Jonas's request; handoff written into
  HANDOFF_NATIVE_VM.md "PROGRESS UPDATE" (VZ console facts, the QEMU
  virtio-serial-pci test rig, remaining TODO).
