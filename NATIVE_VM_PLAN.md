# Native VM Plan — ojjyOS.app on Apple's Virtualization framework (2026-10-18)

HW-14 / launch-3 delivered (2026-10-30, implementation `89bf21c` + Control
`ba37569`): `ojjyOS.app`, `scripts/run-qemu.sh` (`scripts/qemu_launch.py`), and
ojjy Control share `build/launch-settings.json` with SHA-256 revisions,
fail-fast `.launch-settings.lock`, unknown-key preservation, bounded Intel
`opt/ojjyOS/display` fw_cfg mode selection, and `window.backingScaleFactor`
screenshot guards. Installed ARM/Intel/app builds passed with zero warnings and
byte-identical saved-data/settings/EFI/legacy hashes (`build/hw-14/preservation.json`).

HW-13 part 5 delivered (2026-10-30, implementation 921c482): runtime PMM and ARM/Intel high-RAM
aliases are implemented; About/Monitor now show measured RAM and separate
managed/used/free/reserved counters. Temporary launch warning removed.
Private VZ2/4/8 light/dark OCR and visual review agree with PMM at
2047/4095/8191 MB RAM (build/hw-13-part5/matrix.json). Main/checkout
fast-forwarded after full gates; installed ARM/Intel/app builds zero
warnings, saved data/legacy disk/launch settings/EFI hashes unchanged.
Private delivered-image VZ8 UI passes, screenshot reviewed, ready note written.
Historical N3/HW-12 limits below describe their original checkpoints.

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

- HW-14 / launch-3 (2026-10-30, board 046904 + Control 939692, branches `hw-14`
  → `main` at `89bf21c` / Control `ba37569`): completed the 3-part launch
  series. `scripts/run-qemu.sh` delegates to `scripts/qemu_launch.py` reading
  `build/launch-settings.json` with nonpersistent `OJJY_*` overrides and
  `--dry-run`; Intel UEFI `boot/ojjy-boot.c` + `boot/display_mode.h` honour
  bounded `opt/ojjyOS/display` fw_cfg modes; `vmapp/` and ojjy Control share
  SHA-256 revisions, fail-fast `.launch-settings.lock`, and unknown-key
  preservation; `VMHost.screenshot` / `LaunchWindow.capture` reject scaled
  mid-animation WindowServer surfaces (`abs(sx - backingScaleFactor) < 0.05`).
  Evidence: `build/hw-14/` (`final3-control-native/report.json` light/dark menu
  OCR, `final2-runner-{tcg,hvf,intel}/report.json`, 4-case
  `final3-vz-launch/report.json`, `preservation.json`, `installed-vz-boot.log`).

- HW-12 launch series (2026-10-27, board 76a30d, branch hw-12): split HW-10
  into HW-12/13/14; native launch/settings window, version-1 atomic JSON,
  bounded 2/3/4/6/8 GB choices, one CPU, 1440x900/1280x800/1600x900 Retina,
  NAT on/off, fullscreen, Option recovery and Cmd+, save-only while running.
  CLI start/control skip the menu; memory/screen overrides never persist.
  No high-RAM allocator change: the single temporary memory note stays for
  HW-13. Two measured boot prerequisites: retain packed BootInfo before the
  owned tables (3 GB firmware header above 4 GiB, LDUR boot_epoch alignment
  panic in rtc_read_time), map Device BARs through 16 GiB (8 GB moves BAR0
  to 0x2800x0000, outside the old 4–8 GiB window). Before/after evidence:
  build/hw-12-boot-3072{,-fixed}, hw-12-boot-8192-{before,fixed}/efi.log.
  All five RAM probes pass; PMM total 2045 vs 3069 MB for 2048/3072 allocation,
  actual free still tops out around 2286 MB. Four-case live launch matrix
  (build/hw-12-launch-verified) passes all three screens, pixel pointer bounds,
  network-off, automatic full screen, direct startup and pending-only Save.
  Screenshots inspected. Startup fullscreen is coordinated on AppKit's event
  loop (inline completion toggle left busy=1/stage=0 for 45s); pointer gates
  account for centered letterboxing without changing the 3px bound. Native
  three-boot protected/persistence smoke and QEMU ARM TCG/HVF protected smokes
  pass. Both dev builds zero warnings, normal/sanitized suites green.
  Final source: build/hw-12-launch-final-source/report.json, hw-12-vz-final-source,
  hw-12-ui-opaque (light/dark + error captures), 2137 Swift and seven pixel
  gates. Capture guards reject transparent WindowServer show frames. Rebased
  onto parallel QA ac5eb6c with no VM/kernel/test source differences; both
  zero-warning builds and host/sanitized/Swift gates rerun green. Installed
  app/ARM build delivered after VM-process checks; executable/kernel match
  tested artifacts byte-for-byte; user-data/settings/legacy disk and existing
  release.html hashes unchanged (hw-12-installed-before-verified.json).
  Private delivered-image 3 GB boot passes (hw-12-delivered-3072/report.json).
  HW-12 done, READY_TO_TEST note written; next HW-13 / launch-2, then HW-14.

- Integration final receipts (2026-10-18): the delivered-image native smoke
  failed three times at the host full-screen step. Measured root cause, not
  a guest problem and not load flakiness: `styleMask` reports `.fullScreen`
  before the Space animation settles, so the smoke screenshotted a
  windowed-size window while believing it was full screen, and the opposite
  `toggleFullScreen` issued in that window was silently dropped by AppKit.
  A minimal probe proved fresh toggles work (0.6–0.7 s) while the smoke's
  hide/screenshot churn broke them: unconditional
  `makeKeyAndOrderFront`+`activate` during a settling Space fires stray
  will/did callbacks, and a transition dropped mid-flight left the in-flight
  count stuck, poisoning every later toggle. Host fix (test assertions
  unchanged): `prepareScreenshot` fronts the window only when it is not
  already visible+key+active; the `fullscreen` control command now settles
  any in-flight transition (self-healing a stale count after 2.5 s without
  an event), toggles, waits for the NSWindowDelegate completion callback,
  and re-issues up to three times if macOS dropped the request. Evidence:
  `build/vz-resume/fs-minimal-2` (four transitions incl. post-churn, correct
  3024x1898 fullscreen vs 2240x1400 windowed geometry) and
  `merged-delivered-native-final/report.json` — native three-boot smoke with
  USB setup/clicks, pixel pointer alignment, Retina + true full-screen
  captures, 256 editing cycles, all 19 protected-app cases, sync/shutdown,
  cold login and corrupt-checkpoint fallback, source image unchanged.
  Combined-source matrix all green: host+sanitized suites, warning-free
  ARM/Intel builds with the 17-picture pack, 102 Swift + 5 pixel-gate
  guards, QEMU TCG/HVF/Intel smokes, pointer/apps/browser/files/net/
  network-UI/wallpapers/animation/design/perf suites, installed-image VZ
  boot, and installed user-data SHA-256 equal to pre-build values and
  retained backups (`merged-installed-data-verified.json`). Local merge of
  the UI job's commits follows; nothing pushed.

- Integration correction: local `main` had independently advanced to
  `1eaf5fe` with browser Inter text, Liquid Glass, JPEG/desktop-picture support
  and handoffs. A fast-forward refused safely. Per TODO.md and the native
  prompt's explicit normal-merge allowance, merged main into native-vm with
  no commit pending combined-source verification. Preserved both Makefile
  additions, both host-test lists and both doc streams. The sibling worktree
  and branch are unchanged. Reused its existing wallpaper originals/cache
  read-only into this checkout's git-ignored local assets (no downloads).
  Combined ARM/Intel development builds include all 17 pictures, are warning-
  free, and normal/sanitized host suites pass (`merged-{arm,x86,host,sanitized}.log`).
  Native fresh-data smoke now clears only LBA 67584..102399, not the picture
  pack at LBA 102400. Running combined QEMU/VZ + app/browser/file/wallpaper
  regressions before installing combined images or advancing main.

- N11 final exact-source gate passes: `n11-host-final.log`,
  `n11-app-guards.log` (102 Swift + five Python capture probes),
  `n11-{arm,x86}-final.log` (zero warnings/errors) and
  `n11-native-final/report.json` (three native cold boots, protected apps,
  hidden-window capture, setup/input/fullscreen/persistence/fallback).
  Final native test leaves the installed source disk unchanged. Cursor
  generator output remains byte-identical after the Pillow API update.
  All native milestones complete; finish with the reviewed N11 local commit
  and fast-forward `main`. No remote push; conversation export stays untracked.

- N11 delivery verification (2026-10-18): standard builds updated both
  installed images without changing either data partition. SHA-256 equals
  pre-build values and newest full backups (`installed-data-verified.json`):
  ARM `ce09a93f33b046a2abed527cdc91e20547f1bf480d88f5cc5a3da1d702fcaa8d`,
  Intel `2910f9e463394546790a465e60ae9abde773a0820966a5452bb91b38d6602f3f`.
  Legacy image unchanged. Installed kernel binaries equal tested development
  binaries. `installed-vz-boot/report.json` boots an unchanged private copy
  with restored state, five virtio devices, two USB HID devices, real DHCP
  and timer/clock assertions. Native `delivered-native-alpha/report.json`
  passes all three cold boots plus the shared protected-app suite (19
  programs, fault classes, watchdog/output/syscall limits, equal free pages,
  clean successes after faults and Spotlight→Terminal execution).
  QEMU TCG/HVF/Intel smokes pass (`final-{tcg,hvf,intel}.log`); network,
  local HTTP/HTTPS browser and network-UI checks pass. App/file/design/
  animation tours completed and representative captures reviewed. Perf
  receipt `final-perf/perf.json` records Terminal/Settings/Web/Calculator
  opens at 231–243ms under HVF; no hard real-time claim.

- N11 capture regression: an extended native run exposed fully transparent
  WindowServer/Metal captures; hide/show could also return resized or
  transparent-edge frames. A fixed sleep and disabling show animations were
  insufficient. Capture now fronts its own window, yields the main queue
  and waits with a 3s deadline for four opaque edge midpoints, ignoring only
  rounded corners. No PNG is published until readiness; opaque black images
  still fail the independent nonblank gate. Native smoke explicitly hides
  its window before capture. Pointer pixel comparison ignores tiny RGB
  resampling differences and locates the contrasting stroke. Five fixture
  tests cover correct/noisy, missing, 10px offset, large offset and changed
  geometry; tolerance stays 3 logical pixels. `test-vmapp.sh` runs those
  plus the existing 102 host checks. A Pillow iteration deprecation in the
  cursor fixture's generator was updated without changing generated pixels.

- N9/N10 complete evidence: `build/vz-resume/native-complete/report.json`
  passes three native cold boots with click-based setup (uppercase/spaces),
  pixel-confirmed absolute pointer, Retina CG captures, full-screen entry/exit,
  256 editing cycles, sync/shutdown, saved-password login and damaged-newest-
  checkpoint fallback. Window-only full-screen/desktop captures visually
  reviewed; branding, blue theme, sharp menu/Dock preserved. Headless boot
  clock/IRQ evidence plus live clock/animation frames establish N4/N7 visual
  acceptance. Live disk+builder lock retention checks pass. 102 host guard
  checks pass; final host/sanitizer suites and ARM/Intel builds are clean.
  Kernel hashes before/after final builds are identical (pre/post-final-kernel-
  hashes.txt), so previous live QEMU TCG/HVF/Intel receipts remain applicable.
  Limits: fixed 2880x1800 scanout with aspect-preserving view scaling, no live
  guest mode changes, US-ASCII control text, no exhaustive manual focus-loss
  testing. Guest calendar uses UTC. Next is N11 regression/docs/data-preserving
  installed-image update and local merge; nothing pushed or installed yet.

- N9/N10 implementation underway: AppKit host + VZVirtualMachineView, native
  menus/fullscreen, 2880x1800 scanout, same 2GiB/1vCPU/NAT/virtio configuration,
  disk and builder locks, EFI store and serial paths. Explicit owner-only Unix
  control socket sends events only to this VM's view and captures this window
  via CGWindowListCreateImage (Metal contents verified in images). Host unit
  safeguards verify ASCII mappings, hard-link alias rejection and separate-
  process lock exclusion. `native-guards/report.json` passes native setup,
  uppercase/Shift+Esc, 256 editing cycles, sync/shutdown and cold login; live
  disk+builder locks were checked while VZ ran. New pointer/fullscreen/fallback
  smoke is pending (`native-complete*`). No installed disk touched yet.
  Host bugs fixed: flagsChanged must include NX_DEVICE left/right bits, not
  just aggregate Shift/Cmd flags; iCloud restores FinderInfo between xattr
  removal and signing, so sign outside iCloud then install the signed bundle.
  Controlled test view ignores ordinary physical input to isolate events.
  A pixel pointer assertion initially compared bitmap shadow origin with the
  hotspot; fixed its geometry using CURSOR_HOT_X/Y=2, not a looser threshold.

- N8 kernel component verified (native interaction acceptance pending N9):
  `xhci.c` / `usb_hid.c` implement bounded polling rings, direct-port HID
  enumeration, boot keyboard transitions, absolute X/Y report parsing with
  IDs/logical ranges, buttons/wheel and central repeat handling. Runtime DMA
  storage remains pinned for the boot even for failed slots. No hubs or
  hot-plug enumeration. BAR0 verified at 0x50001000, CAPLENGTH=0x20,
  xHCI=1.10, 16 slots/16 ports, 32-byte contexts, DB=0x940, RT=0x520.
  VZ ports 9/10 enumerate 05ac:8105 keyboard and 05ac:8106 absolute pointer.
  Root causes during bring-up: CONFIG must precede DCBAAP (first VZ crash
  was cached-array bounds at native image offset 0x232ef0); each control
  stage is its own TD, not CH-chained (second crash in transfer handling).
  Crash reports plus focused local disassembly informed these two fixes;
  REA native providers were unavailable, so no decompiler evidence claimed.
  Fresh VZ boot passes after both changes, with IRQ/clock progress and DHCP.
  QEMU USB keyboard at slow service intervals lost transitions under the
  unchanged 256-cycle 5ms stress, leaving keys held (FIFO pressure inferred,
  not measured). Capping FS/HS HID service to <=1ms
  fixes this without relaxing the test. Original and corrected evidence:
  `usb-qemu.log` (fails) vs `usb-interval-qemu.log` and `n8-usb-qemu.log` (pass).
  The full USB smoke includes protected app faults/timeouts, persistence,
  shutdown, rebuild, saved login and corrupt-checkpoint recovery. USB pointer
  QA and standard virtio-input HVF smoke pass, as do normal/ASan/UBSan host
  suites and warning-free ARM/Intel builds. Final receipts:
  `build/vz-resume/n8-{usb-qemu,usb-pointer,hvf,host,sanitized,arm,x86}.log`,
  `n8-vz-checked/report.json`. Tests use private disks only. Next: N9 app,
  N10 native input/screenshot/persistence smoke; installed images untouched.

- N8 implementation plan: inspect assigned xHCI BAR capability registers
  on the private VZ boot rig before resetting hardware. Then implement
  bounded, polled command/event/transfer rings with runtime DMA allocation;
  enumerate directly connected USB HID devices (no hubs/isochronous USB in
  this milestone). Use boot keyboard reports and parse pointer report
  descriptors (X/Y logical ranges, buttons, wheel, report IDs). Feed existing
  input APIs; keep QEMU virtio-input unchanged. Host regressions cover malformed
  HID descriptors, keyboard transitions and absolute scaling; QEMU xHCI
  injection exercises actual DMA transport; native input acceptance follows
  in N9/N10's app/control tests. Failures must retain device-owned buffers
  until the controller/slot is stopped; no spin-count waits or blind probes.

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

- 2026-10-18 (resumed after d4f2252): N5/N6 now reaches the main loop on
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
