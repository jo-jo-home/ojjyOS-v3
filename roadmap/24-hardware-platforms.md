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
- Depends: HW-12, HW-13, HW-14, SMP-01
- Files: vmapp/main.swift, scripts/build-vmapp.sh, tests/test_vmapp.swift

**Context.** The app boots the custom kernel on VZ with fixed 2880×1800,
USB HID input, virtio devices (NATIVE_VM_PLAN.md).
**Goal.** Remaining preferences (more CPUs after SMP-01, disk size growth,
shared folder, sound on/off, bridged network), automatic display
resize (pairs with DRV-VIRTIO-04), pause/save state (VZ save/restore on
supported macOS), multiple VM profiles (e.g. "Daily", "Test"), start at
login option, clean "Shut Down" button sending ACPI power to the guest.
**Done when.** All options work and persist; `test_vmapp.swift` covers
them.
**Tests.** Native guard tests + `vz_smoke.py`.
**Pitfalls.** Never touch a disk while the VM runs (lock files exist).
**Refs.** Apple Virtualization framework docs.

### HW-12 — Launch menu in ojjyOS.app
- Phase: P2 · Size: M · Status: doing
- Depends: —
- Files: vmapp/main.swift, tests/test_vmapp.swift, tests/vz_boot.py, vmapp/probe.swift

**Context.** Part 1 of the launch series. The native Mac host starts immediately
with one CPU, 2 GiB, 2880x1800 scanout, NAT and fixed 1440x900 input geometry.
HW-10's larger host feature set is intentionally not part of this card.
**Goal.** Blue Liquid Glass launch window before starting the VM; persistent,
safe version-1 settings in `build/launch-settings.json` next to ojjyOS.app.
**Steps.**
- [ ] Step 1: settings model/limits/CLI in vmapp/main.swift and host guards in
  tests/test_vmapp.swift; test parse/default/clamp/unknown/broken/round-trip/
  atomic/fuzz and path aliases. Trap: never let settings overwrite disk/log/EFI.
- [ ] Step 2: native AppKit launch window and startup/running-settings flows in
  vmapp/main.swift; build/test guards and light/dark captures. Trap: pending
  settings must not change active VM input geometry or start a second VM.
- [ ] Step 3: wire RAM/network/Retina/aspect/input/fullscreen, extend
  vmapp/probe.swift and tests/vz_boot.py; private 2048/3072 boots and each screen
  screenshot/input proof. Trap: physical scanout is twice logical coordinates.
- [ ] Step 4: final normal/sanitized/Swift suites, both zero-warning builds,
  VZ smoke and QEMU TCG/HVF regression; log evidence, mark done and deliver.
  Trap: installed data/settings and concurrent QA work must survive unchanged.
**Done when.**
- [ ] RAM offers 2/3/4/6/8 GB only inside VZ min/max and <= half host RAM;
  one removable constant says "ojjyOS uses about 2.2 GB until its memory update".
- [ ] CPU shows disabled 1 and "More cores come with SMP-01". Default screen
  1440x900 Retina means 2880x1800 scanout; other sizes have inspected live proof.
- [ ] Option at launch restores the menu when show-at-start is off. VM > Launch
  Settings… (Cmd+,) reuses it; running VM only saves for next shutdown/start.
- [ ] Missing/broken/bad settings log and default/clamp, never crash. JSON schema:
  `{"version":1,"memoryMB":2048,"cpus":1,"screen":"1440x900","network":true,"fullScreen":false,"showAtStart":true}`.
- [ ] `--memory <MB>` and `--screen <WxH>` override file without overwriting it;
  builders/tests preserve this ignored user file. Native guards cover parsing,
  defaults/clamping, unknown keys, broken JSON, round-trip and atomic writes.
- [ ] Live private-disk VZ 2048/3072 MB boots show distinct [PMM] totals;
  screenshots for every offered screen and light/dark menu in `build/hw-12-*`.
**Tests.** `scripts/test-vmapp.sh`, `tests/vz_boot.py`, `tests/vz_smoke.py`,
both zero-warning dev builds, normal and sanitized host suites, strict checklist.
**Pitfalls.** No SMP or kernel memory changes here. VZ RAM starts 0x70000000;
current PMM/EL0 maps cap at 4 GiB physical (~2.2 GB usable). Screen GET_DISPLAY_INFO
drives guest size; host input must agree. Never touch installed disks during tests
or a running VM; build scripts must not replace launch settings.
**Refs.** NATIVE_VM_PLAN.md; launch-2 uses HW-13, launch-3 uses HW-14.

### HW-13 — ojjyOS uses all the memory it gets
- Phase: P2 · Size: L · Status: todo
- Depends: HW-12
- Files: kernel/src/memory.c, kernel/src/memory.h, kernel/src/arch/aarch64/user_arch.c, kernel/src/ui/compositor.c, tests/

**Context.** Launch series part 2 (launch-2). HW-12 supplies configurable RAM,
but MAX_MEMORY_GB/static bitmap and low-4-GiB EL0 mapping limit actual use.
Apple VZ RAM starts at 0x70000000, leaving only about 2.2 GB below that ceiling.
**Goal.** PMM tracks all RAM from the UEFI memory map; pages above 4 GiB
are really allocated/used and About ojjyOS displays the real usable memory.
**Steps.**
- [ ] Inspect UEFI maps for multiple RAM sizes and both platforms; plan bounded
  runtime PMM metadata rather than growing BSS into firmware allocations.
- [ ] Extend PMM and ARM kernel/user mappings for real RAM above 4 GiB;
  distinguish RAM from VZ PCI BARs and keep protected app isolation intact.
- [ ] Test allocations, writes/reads/frees and protected pages above 4 GiB;
  correct About memory and remove HW-12's single temporary memory-note constant.
- [ ] Run host+sanitized/build/live matrix, log evidence and safely deliver.
**Done when.**
- [ ] Multiple VZ RAM settings produce corresponding real PMM totals and
  demonstrably used pages above 4 GiB; About matches, no placeholder claim.
- [ ] Existing ARM/QEMU/Intel/protected-app regressions pass; all context current.
**Tests.** PMM/mapping host tests and fuzz boundaries, ASan/UBSan, both dev
builds, private-disk VZ 2/3/4/6/8 GB as host permits, QEMU TCG/HVF/Intel smokes.
**Pitfalls.** Physical address != RAM size; sparse maps/overflow/holes/reserved
regions matter. VZ BARs above 4 GiB stay Device. No huge static bitmap/BSS.
Do not weaken isolation or silently map device memory as user RAM.
**Refs.** NATIVE_VM_PLAN.md; HW-12 launch settings; memory-map UEFI specification.

### HW-14 — QEMU script and ojjy Control use the launch settings
- Phase: P2 · Size: S · Status: todo
- Depends: HW-12
- Files: scripts/run-qemu.sh, ../ojjy-control/, tests/

**Context.** Launch series part 3 (launch-3). Native app owns the version-1
`build/launch-settings.json`; QEMU and ojjy Control still have separate defaults.
**Goal.** All three read/write the same file without losing user settings.
**Steps.**
- [ ] Read HW-12 schema/validation and ojjy Control AGENTS.md; document override
  precedence and QEMU scanout vs logical size for HVF/TCG/Intel.
- [ ] Wire QEMU RAM/screen/network/fullscreen and Control editor to the same
  file with safe defaults, host clamps and atomic writes; preserve unknown keys.
- [ ] Test missing/corrupt/partial files, overrides and cross-tool round trips;
  live boot QEMU and verify Control changes appear in the native launch menu.
**Done when.**
- [ ] No competing settings store; native app, QEMU and Control agree after
  restart; automated tests bypass menus and never modify installed user data.
- [ ] Cross-project tests/context/checklists pass and evidence is recorded.
**Tests.** QEMU runner argument tests, Control unit/UI tests, native guard
round trips and private-disk QEMU/VZ smoke; strict checklists for both repos.
**Pitfalls.** Control is a separate private repo: claim/commit/update context
there too. Avoid absolute personal paths, shell injection, JSON truncation and
concurrent-writer loss; CPU stays 1 until SMP-01. Never reset user disks.
**Refs.** HW-12, HW-13, NATIVE_VM_PLAN.md; ojjy Control project context.

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
