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
- Phase: P2 · Size: M · Status: done
- Depends: —
- Files: vmapp/main.swift, vmapp/LaunchSettings.swift, vmapp/LaunchWindow.swift, vmapp/probe.swift, tests/test_vmapp.swift, tests/test_launch_window.swift, tests/launch_vm_checks.swift, tests/vz_boot.py, tests/vz_launch.py, tests/vz_smoke.py, tests/test_vz_capture.py, scripts/build-vmapp.sh, scripts/test-vmapp.sh, scripts/test-vmapp-ui.sh, kernel/src/main.c, kernel/src/arch/aarch64/user_arch.c

**Context.** Part 1 of the launch series. Before this card the native Mac host started immediately
with one CPU, 2 GiB, 2880x1800 scanout, NAT and fixed 1440x900 input geometry.
HW-10's larger host feature set is intentionally not part of this card.
**Goal.** Blue Liquid Glass launch window before starting the VM; persistent,
safe version-1 settings in `build/launch-settings.json` next to ojjyOS.app.
**Steps.**
- [x] Step 1: settings model/limits/CLI in vmapp/main.swift and vmapp/LaunchSettings.swift, host guards in tests/test_vmapp.swift — done 2026-10-27 (test: scripts/test-vmapp.sh, 2132 checks + five pixel gates; app build warnings-as-errors)
  Test parse/default/clamp/unknown/broken/round-trip/
  atomic/fuzz and path aliases. Trap: never let settings overwrite disk/log/EFI.
- [x] Step 2: native AppKit launch window and startup/running-settings flows in vmapp/main.swift + vmapp/LaunchWindow.swift — done 2026-10-27 (test: seven AppKit interaction checks, light/dark captures inspected, warnings-as-errors app build)
  Build/test guards and light/dark captures. Trap: pending
  settings must not change active VM input geometry or start a second VM.
- [x] Step 3: wire RAM/network/Retina/aspect/input/fullscreen, extend vmapp/probe.swift and tests/vz_boot.py — done 2026-10-27 (test: all five RAM probes, four-case tests/vz_launch.py, inspected screenshots in build/hw-12-launch-final-source)
  Private 2048/3072 boots and each screen
  screenshot/input proof. Trap: physical scanout is twice logical coordinates.
  Live-discovered prerequisites: retain the packed BootInfo header in main.c
  before owned page tables (3 GB epoch alignment panic); extend only the
  high Device/MMIO window to 16 GiB in user_arch.c (8 GB BARs at 0x2800x0000).
  No PMM/high-RAM Normal mappings, allocator changes or removal of memory note.
- [x] Step 4: final normal/sanitized/Swift suites, both zero-warning builds, rebase/retest onto parallel QA main and installed delivery — done 2026-10-27 (test: build/hw-12-baseline/rebased-*.log, native/QEMU smoke reports, installed-before-verified.json, delivered 3 GB private boot, strict checklist)
  VZ smoke and QEMU TCG/HVF/Intel regression passed; log evidence, mark done and deliver.
  Trap: installed data/settings and concurrent QA work must survive unchanged.
**Done when.**
- [x] RAM offers 2/3/4/6/8 GB only inside VZ min/max and <= half host RAM; — done 2026-10-27 (test: LaunchLimits guards and all five private VZ probes)
  one removable constant says "ojjyOS uses about 2.2 GB until its memory update".
- [x] CPU shows disabled 1 and "More cores come with SMP-01". Default screen — done 2026-10-27 (test: light/dark captures and live scanout/pointer assertions)
  1440x900 Retina means 2880x1800 scanout; other sizes have inspected live proof.
- [x] Option at launch restores the menu when show-at-start is off. VM > Launch — done 2026-10-27 (test: startup decision guards, AppKit fixtures and four real-VM pending-only Save checks)
  Settings… (Cmd+,) reuses it; running VM only saves for next shutdown/start.
- [x] Missing/broken/bad settings log and default/clamp, never crash. JSON schema: — done 2026-10-27 (test: 2137 Swift guards, 2000 mutations, atomic inode/old-reader/failure checks)
  `{"version":1,"memoryMB":2048,"cpus":1,"screen":"1440x900","network":true,"fullScreen":false,"showAtStart":true}`.
- [x] `--memory <MB>` and `--screen <WxH>` override file without overwriting it; — done 2026-10-27 (test: CLI precedence/alias guards and byte-identical build/launch-settings.json sentinel across builders/tests)
  builders/tests preserve this ignored user file. Native guards cover parsing,
  defaults/clamping, unknown keys, broken JSON, round-trip and atomic writes.
- [x] Live private-disk VZ 2048/3072 MB boots show distinct [PMM] totals; — done 2026-10-27 (test: 2045 vs 3069 MB in build/hw-12-launch-final-source/report.json; every offered screen inspected; build/hw-12-ui-opaque light/dark captures)
  screenshots for every offered screen and light/dark menu in `build/hw-12-*`.
**Tests.** `scripts/test-vmapp.sh`, `tests/vz_boot.py`, `tests/vz_smoke.py`,
both zero-warning dev builds, normal and sanitized host suites, strict checklist.
**Pitfalls.** No SMP or high-RAM allocator changes here. VZ RAM starts 0x70000000;
current PMM/EL0 maps cap at 4 GiB physical (~2.2 GB usable). Screen GET_DISPLAY_INFO
drives guest size; host input must agree. Never touch installed disks during tests
or a running VM; build scripts must not replace launch settings.
**Refs.** NATIVE_VM_PLAN.md; launch-2 uses HW-13, launch-3 uses HW-14.
**Log.** 2026-10-27: done (steps `b460fc8`, `83100d1`, `6c58913`; final
integration follows `4adf8ec`). All ten card boxes verified; both rebased
builds zero warnings, host/ASan/UBSan/Swift gates green; VZ all five RAM presets,
all three screens + pointer/fullscreen/network/pending settings, native protected
three-boot smoke and QEMU TCG/HVF/Intel pass. Evidence `build/hw-12-*` in
`ojjyOS-v3-hw-12`. Main fast-forwarded; installed app + ARM image rebuilt only
with no VM running; data/settings/legacy disk and existing release.html hashes
unchanged. Installed-image 3 GB private boot passes; READY_TO_TEST note written.
Limit: actual allocatable RAM still ~2.2 GB until HW-13; QEMU/Control sharing
is HW-14. No SMP, high-RAM allocator, live guest display resize or remote push.

### HW-13 — ojjyOS uses all the memory it gets
- Phase: P2 · Size: L · Status: doing
- Depends: HW-12
- Files: docs/hw-13-memory-design.md, kernel/src/memory.c/h, kernel/src/user_image.c, kernel/src/arch/aarch64/user_arch.c, kernel/src/paging.c/h, kernel/mm/kvm.c/h (new), kernel/src/ui/browser.c, kernel/src/web/http.c, kernel/src/ui/compositor.c, vmapp/LaunchWindow.swift, tests/

**Context.** Launch series part 2 (launch-2). HW-12 supplies configurable RAM,
but MAX_MEMORY_GB/static bitmap and low-4-GiB EL0 mapping limit actual use.
Apple VZ RAM starts at 0x70000000, leaving only about 2.2 GB below that ceiling.
**Goal.** PMM tracks all RAM from the UEFI memory map; pages above 4 GiB
are really allocated/used and About ojjyOS displays the real usable memory.
**Decision.** 2026-10-29, part 1, base `043894c`: retain a bitmap allocator,
but replace fixed BSS metadata with map-sized, boot-reserved LOW storage
(per-RAM-region eligibility/ownership bitmaps, region counts/cursors and
an owned descriptor copy). Never size by the highest MMIO address. Add
explicit LOW/HIGH/ANY physical APIs; all old page/contiguous/byte APIs stay
LOW and high allocation is gated on successful architecture mapping.
Introduce the RAM-only privileged alias `0xffff800000000000 + PA` for high
CPU access, checked `phys_to_virt`/`virt_to_phys`, and an opt-in mapped-byte
allocator. ARM shares it via TTBR1 across app TTBR0 switches; remove high
RAM's old Device identity aliases, keep VZ BAR holes Device, validate high
user PTE PA/ownership/width and change every loader/copy/wipe/cache access
together. No identity RAM mapping at USER_BASE. Intel uses supervisor
upper-PML4 aliases, LOW table pages and failure-safe walkers (including
missing PT creation); Intel EL0 stays unsupported. Only protected frames,
audited browser arenas/decode scratch and TLS pool opt into high memory;
DMA/GPU/framebuffer/renderer/other legacy buffers stay LOW. MEM-03 and
MEM-04 own the named HW13-A/HW13-X preparation boxes below; cross-check
them when implemented, never claim those full cards done. No buddy,
page-frame DB, vmalloc or kernel relink is needed here. Exact interfaces,
reservation rules, caller inventory, proof requirements and measurements:
`docs/hw-13-memory-design.md` (decision D-026). This is a design, not runtime
implementation.

**Steps.**
- [x] (part 1) Memory maps measured and Decision written — done 2026-10-29 (verified: `build/hw-13-part1/memory-maps.txt`, all 242 descriptors from five private boots; complete maps/counts/BARs, `summary.json` and `preservation.json`; host + ASan/UBSan and both uninstrumented dev builds pass with zero warnings). Files: this card, docs/hw-13-memory-design.md, D-026, MEM-03/04 preparation boxes and context. Trap: removed temporary memory.c logging; VZ 4/8 full test clock failures remain explicitly reported as BUG-1008, not hidden.
- [ ] (part 2) In memory.c/h and new tests/test_pmm.c, bootstrap runtime region/bitmap metadata plus a LOW copy of the firmware map, reserve all live ranges, add LOW/HIGH/ANY APIs with high disabled, preserve legacy LOW wrappers and emit `[PMM] <total> MB RAM, <usable> MB usable` with defined low/high/free counts. Tests: fake sparse/unsorted/many-region/>4-GiB/64-GiB maps, malformed/overlap/overflow/reservation/OOM/bad-free cases, random invariants normal + ASan/UBSan, both zero-warning builds. Trap: metadata cannot allocate itself through an uninitialized PMM; high PA is never an immediately dereferenceable pointer. Stop green without changing caller policies.
- [ ] (part 3) Implement MEM-03 HW13-A and MEM-04 HW13-A shared groundwork in ARM user_arch.c, memory.c/h and kernel/mm/kvm.c/h; LOW page tables, TTBR1 RAM-only alias, checked translation and mapped-buffer API; user_image.c high frames and complete load/copy/wipe/cache conversion; browser.c arenas/decode scratch and web/http.c TLS pool opt in. Tests: descriptor/translation host cases plus private VZ 4/8 GiB and QEMU HVF 4 GiB actual high PA write/read/free and high-backed protected execution with cleanup equality, forbidden kernel-alias/guard/MMIO/W^X accesses. Trap: original descriptor storage is itself high; remove conflicting high RAM Device identity aliases, never cache-map the PCI BARs or expose the alias to EL0. Tick matching MEM preparation boxes only after proof.
- [ ] (part 4) Implement MEM-03 HW13-X and MEM-04 HW13-X in paging.c/h and kernel/mm/kvm.c/h, real Intel high mapped allocations, supervisor/NX/PA-width guards, allocate all missing table levels and propagate failure. Add tests for simultaneous >2.5 GiB ownership (target 3 GiB), every-page PA-derived write/read verification, logged low/high peak bytes and high PA bounds, all frees/recovery; run on VZ/ARM HVF and Intel 4 GiB. Tests: both zero-warning builds, normal/sanitized hosts and full TCG/HVF/Intel/VZ protected/persistence/browser/TLS regression matrix. Trap: sum of repeated one-page allocations is not peak memory use; no fake Intel ring-3 claim. Repair BUG-1008 in tests/vz_boot.py with a deterministic NTP-aware clock assertion test, not a skipped clock gate; remove its tracker line only with verified fix.
- [ ] (part 5) In compositor.c show measured RAM/managed/free correctly in About and Activity Monitor; remove LaunchWindow.swift temporaryMemoryNote only after actual high-memory proof; review light/dark screenshots, update docs/AGENTS/TODO/card/STATUS, retest combined source, fast-forward main and data-preserving delivery only with no running VM. Tests: serial/UI agreement at VZ 2/4/8 GiB, host/Swift/build/live regression gates, before/after saved-data/settings hashes, private delivered-image boot, ready_to_test note. Trap: requested launch RAM and highest physical address are not usable RAM; never modify an installed live disk. Only part 5 may land main.
**Done when.**
- [ ] (part 2) Host fake-map and sanitizer tests prove dynamic metadata/reservations/zones/statistics without static growth or high-pointer dereferences; legacy callers still receive LOW pages. Files/tests/trap: memory.c/h + test_pmm.c, both builds; do not equate tracking high pages with using them.
- [ ] (part 3) Actual high frames and chosen high-backed CPU buffers work under VZ 4/8 GiB and ARM HVF 4 GiB with unchanged protected isolation and cleanup equality. Files/tests/trap: ARM/user_image/browser/http/kvm tests; a successful boot or printed total alone is insufficient.
- [ ] (part 4) Intel uses high RAM through valid mappings and the live peak-owned >2.5 GiB write/read/free test and complete regression matrix pass. Files/tests/trap: paging/kvm/live memory tests; every held page is distinct, firmware/Device addresses are never RAM.
- [ ] (part 5) Multiple VZ RAM settings produce matching real PMM/About/Monitor figures; temporary memory warning removed, reviewed screenshots and data-preserving installed delivery verified. Files/tests/trap: compositor/LaunchWindow/delivery; no UI-only placeholder claim.
- [ ] (part 5) All series boxes and matching MEM substeps have evidence; context current, strict checklist/roadmap clean, combined source tested and only then main fast-forwarded. Files/tests/trap: roadmap/context/regression logs; full MEM-01..05 completion is not implied.
**Tests.** PMM/mapping host tests and fuzz boundaries, ASan/UBSan, both dev
builds, private-disk VZ 2/3/4/6/8 GB as host permits, QEMU TCG/HVF/Intel smokes.
**Pitfalls.** Physical address != RAM size; sparse maps/overflow/holes/reserved
regions matter. VZ BARs above 4 GiB stay Device. No huge static bitmap/BSS.
Do not weaken isolation or silently map device memory as user RAM.
**Refs.** NATIVE_VM_PLAN.md; HW-12 launch settings; memory-map UEFI specification.
**Log.** 2026-10-29 part 1: runtime code unchanged after a logging-only
probe. VZ 2/4/8 GiB free pages 519159/585325/585325; ARM HVF/Intel 4 GiB
781935/780223. VZ 4/8 GiB reclaimable high RAM 1789.543/5885.543 MiB;
BAR0s move from 0x1000x0000 to 0x1800x0000 and 0x2800x0000. All five
maps complete; VZ 4/8 full boot-test clock gate failed after NTP correction
(BUG-1008), despite main-loop/device/DHCP success. Final host/sanitized
suites and uninstrumented builds green. No installed delivery or main move.
**Handoff.** Next session is **hw13-2 only**, on `hw-13` in
`~/Developer/ojjyOS-kernel/ojjyOS-v3-hw-13`; take paused board `f5c05f`.
Read Decision plus docs/hw-13-memory-design.md and MEM-03/04 shared boxes.
Re-run `export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"` then
`bash scripts/test.sh` before coding; the default LibreSSL failed TLS
fixture date flags, Homebrew OpenSSL passes. Part 1 raw evidence and
logging patch/scripts are in ignored build/hw-13-part1; do not ship the
probe images or assume their maps are new allocator proof. Implement only
part 2 runtime metadata/LOW-safe APIs and fake-map tests, retaining high
allocation disabled until part 3 mappings. Check bounds, kernel/firmware/
map/framebuffer/self-reservations and owned frees. Original map addresses
are high at 4/8 GiB, so copy them LOW while firmware tables are still live.
Kernel links at 0x71000000 today, not old 0x41000000. Final checks:
normal/sanitized hosts, both zero-warning dev builds, roadmap check/status
and strict checklist. Leave future boxes open, update Handoff, commit and
pause; main/installed images stay untouched until hw13-5. BUG-1008 is a
known test-clock contract gap scheduled for part 4, not an allocator bug.

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
