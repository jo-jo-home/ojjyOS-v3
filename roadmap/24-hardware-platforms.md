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
- Phase: P2 · Size: L · Status: done
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
`docs/hw-13-memory-design.md` (decision D-026). All five parts are now
implemented, verified and delivered; see the dated Logs and part-5 matrix.

**Steps.**
- [x] (part 1) Memory maps measured and Decision written — done 2026-10-29 (verified: `build/hw-13-part1/memory-maps.txt`, all 242 descriptors from five private boots; complete maps/counts/BARs, `summary.json` and `preservation.json`; host + ASan/UBSan and both uninstrumented dev builds pass with zero warnings). Files: this card, docs/hw-13-memory-design.md, D-026, MEM-03/04 preparation boxes and context. Trap: removed temporary memory.c logging; VZ 4/8 full test clock failures remain explicitly reported as BUG-1008, not hidden.
- [x] (part 2) In memory.c/h and new tests/test_pmm.c, bootstrap runtime region/bitmap metadata plus a LOW copy of the firmware map, reserve all live ranges, add LOW/HIGH/ANY APIs with high disabled, preserve legacy LOW wrappers and emit `[PMM] <total> MB RAM, <usable> MB usable` with defined low/high/free counts. Tests: fake sparse/unsorted/many-region/>4-GiB/64-GiB maps, malformed/overlap/overflow/reservation/OOM/bad-free cases, random invariants normal + ASan/UBSan, both zero-warning builds. Trap: metadata cannot allocate itself through an uninitialized PMM; high PA is never an immediately dereferenceable pointer. Stop green without changing caller policies. — done 2026-10-29 (test: build/hw-13-part2/host-complete.log and sanitized-complete.log, 38 PMM checks plus 200000 random operations; both final builds zero warnings; TCG/HVF/Intel full smokes pass; native VZ8 memory boot verified, full clock gate still fails inherited BUG-1008 after NTP, explicitly not credited as a full VZ test pass).
- [x] (part 3) Implement MEM-03 HW13-A and MEM-04 HW13-A shared groundwork in ARM user_arch.c, memory.c/h and kernel/mm/kvm.c/h; LOW page tables, TTBR1 RAM-only alias, checked translation and mapped-buffer API; user_image.c high frames and complete load/copy/wipe/cache conversion; browser.c arenas/decode scratch and web/http.c TLS pool opt in. Tests: descriptor/translation host cases plus private VZ 4/8 GiB and QEMU HVF 4 GiB actual high PA write/read/free and high-backed protected execution with cleanup equality, forbidden kernel-alias/guard/MMIO/W^X accesses. Trap: original descriptor storage is itself high; remove conflicting high RAM Device identity aliases, never cache-map the PCI BARs or expose the alias to EL0. Tick matching MEM preparation boxes only after proof. — done 2026-10-29 (test: build/hw-13-part3/matrix.json, 51 PMM + 21 builder checks normal/fatal ASan/UBSan, both dev builds zero warnings; VZ4/VZ8/HVF4 each 51 high-backed protected runs with exact cleanup, permission faults on present aliases and high browser/TLS/PNG buffers; TCG/HVF/Intel/native smokes and HVF browser pass. Full VZ clock tests still FAIL known BUG-1008, untouched and retained for part 4).
- [x] (part 4) Implement MEM-03 HW13-X and MEM-04 HW13-X in paging.c/h and kernel/mm/kvm.c/h, real Intel high mapped allocations, supervisor/NX/PA-width guards, allocate all missing table levels and propagate failure. Add tests for simultaneous >2.5 GiB ownership (target 3 GiB), every-page PA-derived write/read verification, logged low/high peak bytes and high PA bounds, all frees/recovery; run on VZ/ARM HVF and Intel 4 GiB. Tests: both zero-warning builds, normal/sanitized hosts and full TCG/HVF/Intel/VZ protected/persistence/browser/TLS regression matrix. Trap: sum of repeated one-page allocations is not peak memory use; no fake Intel ring-3 claim. Repair BUG-1008 in tests/vz_boot.py with a deterministic NTP-aware clock assertion test, not a skipped clock gate; remove its tracker line only with verified fix. — done 2026-10-30 (test: build/hw-13-part4/matrix.json, final normal/fatal ASan/UBSan hosts incl. 28 Intel + 9 stress checks, both builds zero warnings; VZ8/HVF4/Intel4 each hold 3221225472 bytes/verify all 786432 pages/free/recover; full protected/persistence/browser/TLS/native matrix passes; BUG-1008 red/green 29-subcase fixtures and final VZ2/4/8 clock gates pass with actual NTP offsets; Intel EL0/network still unsupported. Explicit ARM unchanged-code receipt reuse in arm-final-identical.json; main/installed untouched).
- [x] (part 5) In compositor.c show measured RAM/managed/free correctly in About and Activity Monitor; remove LaunchWindow.swift temporaryMemoryNote only after actual high-memory proof; review light/dark screenshots, update docs/AGENTS/TODO/card/STATUS, retest combined source, fast-forward main and data-preserving delivery only with no running VM. Tests: serial/UI agreement at VZ 2/4/8 GiB, host/Swift/build/live regression gates, before/after saved-data/settings hashes, private delivered-image boot, ready_to_test note. Trap: requested launch RAM and highest physical address are not usable RAM; never modify an installed live disk. Only part 5 may land main. — done 2026-10-30 (test: implementation 921c482; build/hw-13-part5/matrix.json and preservation.json; VZ2/4/8 PMM/UI light/dark reviewed, full combined-source gates pass, main/checkout fast-forwarded, both installed builds/app zero warnings, ARM/Intel saved data + legacy/settings/EFI hashes unchanged, private delivered-vz8/report.json PASS, ready_to_test written).
**Done when.**
- [x] (part 2) Host fake-map and sanitizer tests prove dynamic metadata/reservations/zones/statistics without static growth or high-pointer dereferences; legacy callers still receive LOW pages. Files/tests/trap: memory.c/h + test_pmm.c, both builds; do not equate tracking high pages with using them. — done 2026-10-29 (test: 38 PMM assertions normal/sanitized, including exact 4-GiB LOW endpoint and disabled HIGH; build/hw-13-part2/native-memory-proof.json records VZ8 8191 MB RAM / 8171 MB usable with 1506699 HIGH pages tracked but disabled; all existing callers unchanged).
- [x] (part 3) Actual high frames and chosen high-backed CPU buffers work under VZ 4/8 GiB and ARM HVF 4 GiB with unchanged protected isolation and cleanup equality. Files/tests/trap: ARM/user_image/browser/http/kvm tests; a successful boot or printed total alone is insufficient. — done 2026-10-29 (test: build/hw-13-part3/{vz4-v1,vz8-v1,hvf4-v1}/report.json, PA 0x100000000 zero/write/read/free, 153 high-backed protected runs total, all cleanup counts equal, EL0 alias permission faults, HIGH 40/28/20/48-MiB buffers and 117496-byte TLS slot; HTTP/HTTPS/PNG screenshots read; full VZ clock gate remains known BUG-1008 failure, not a pass).
- [x] (part 4) Intel uses high RAM through valid mappings and the live peak-owned >2.5 GiB write/read/free test and complete regression matrix pass. Files/tests/trap: paging/kvm/live memory tests; every held page is distinct, firmware/Device addresses are never RAM. — done 2026-10-30 (test: build/hw-13-part4/stress-intel4-final/report.json and stress-{hvf4,vz8}-v1/report.json, each actual 3-GiB simultaneous peak/every-page PA-derived patterns/free counts equal/recovery; matrix.json records normal/sanitized/build/live regressions plus corrected full VZ clocks; Intel unsupported EL0 is not credited as a protected-app pass).
- [x] (part 5) Multiple VZ RAM settings produce matching real PMM/About/Monitor figures; temporary memory warning removed, reviewed screenshots and data-preserving installed delivery verified. Files/tests/trap: compositor/LaunchWindow/delivery; no UI-only placeholder claim. — done 2026-10-30 (test: build/hw-13-part5/{vz2,vz4-v2,vz8,delivered-vz8}/report.json, OCR/exact PMM/Monitor receipts, all 12 development captures reviewed plus installed About; 8191 MB RAM/8171 MB managed at 8 GB, real high pattern at PA 0x100000000, 51 high-backed protected runs with exact cleanup, 3-GiB stress on VZ8/HVF4/Intel4; preservation.json and ready note verified, implementation 921c482/delivery records 47060cc).
- [x] (part 5) All series boxes and matching MEM substeps have evidence; context current, strict checklist/roadmap clean, combined source tested and only then main fast-forwarded. Files/tests/trap: roadmap/context/regression logs; full MEM-01..05 completion is not implied. — done 2026-10-30 (test: build/hw-13-part5/matrix.json, preservation.json, checklist-final.log, roadmap check/status; four shared MEM-03/04 boxes retain parts 3/4 proof, fresh combined-source PMM/mapper/stress host and live tests pass, final context has no production-source changes; 921c482/47060cc/b5c2958 and final completion records).
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
**Log.** 2026-10-29 part 2: runtime PMM metadata implemented in memory.c/h,
legacy APIs and ANY remain LOW, HIGH fails closed. New 38-assertion host
suite covers 64 GiB, 260 regions, 200000 random operations, malformed maps,
reservations, low-only exhaustion/free and boundary checks. Both full host
and ASan/UBSan suites pass, both dev builds zero warnings, private full
TCG/HVF/Intel smokes pass. VZ8 reaches main loop/device/DHCP and logs
8191 MB RAM / 8171 MB usable; metadata 528384 bytes, LOW 585228 pages,
HIGH 1506699 tracked/disabled. The full VZ test still fails BUG-1008's
uncorrected clock expectation after a +1118 ms NTP correction; not fixed,
skipped or claimed green here. Evidence build/hw-13-part2/matrix.json and
native-memory-proof.json, plus host-complete/sanitized-complete logs.
No page tables/callers/UI/installed images/main changed.
**Log.** 2026-10-29 part 3 complete after explicit part-2 push approval
(remote hw-13 9b2e93f). LOW-table ARM RAM-only TTBR1 aliases, checked
translations/readiness, high protected frames and selected browser/TLS
buffers implemented. 51 PMM + 21 builder host checks, normal/fatal ASan/
UBSan, both zero-warning builds, TCG/HVF/Intel/native protected/persistence
smokes and HVF browser flow pass. VZ4/VZ8/HVF4 each prove HIGH pattern
access at 0x100000000, 51 high-backed protected runs/cleanup equality,
EL0 alias read/write/execute permission faults and high-buffer HTTP/HTTPS/
PNG. Screenshots read; exact-source evidence build/hw-13-part3/matrix.json.
Full VZ4/8 boot-clock tests still FAIL BUG-1008 after +1493/+1418 ms NTP
correction. Never count these as full boot-test passes; repair stays part 4.
Both part-3 and MEM-03/04 HW13-A boxes checked; MEM cards are doing for
this subset, not complete, with their full dependencies unchanged.
No installed image/settings/ready note/main changed; part 3 not pushed.
**Log.** 2026-10-30 part 4: checked Intel supervisor NX high aliases and
failure-safe mapper including missing PT implemented, CPU width distinct
from canonical alias span. Runtime-bounded admin memtest proves 3 GiB
simultaneous ownership with eight PA-derived words on every page, all
786432 pages verified and exact free counts/recovery on VZ8/HVF4/Intel4.
BUG-1008 fixed using sampled NTP/read-time bracket and stronger deterministic
clock fixtures (29 subcases); final full VZ2/4/8 clock gates pass with
+1230/+1376/+1533 ms corrections. Full normal/fatal ASan/UBSan hosts,
both zero-warning builds, TCG/HVF/Intel/native smokes, ARM browser/TLS,
strict network/SNTP and 2137 Swift + 7 pixel guards pass. Evidence
build/hw-13-part4/matrix.json and docs/hw-13-part4-plan.md. ARM kernel is
byte-identical across final rebuild; only two EFI timestamp bytes differ,
explicitly recorded for unchanged-code receipt reuse; fresh final-image
VZ2/4/8 and HVF4 high tests also pass. Intel stress/smoke rerun on final
image. Screenshots read. No main/installed/settings/UI-warning/ready note
change; full MEM cards remain doing, not complete. Implementation ac7acb5
committed locally, strict checklist/roadmap/privacy checks clean; board
f5c05f paused for hw13-5. New commits not pushed.
**Log.** 2026-10-30 pre-push double-check: parent + six fresh-context
static passes found no actionable issue; production source unchanged.
Full normal/fatal-sanitized hosts, 10 extra mapper probes, repeated
Intel4/HVF4/VZ8 3-GiB stress and VZ4/8 full clock tests pass; exact hashes,
privacy/checklist/roadmap verified. Evidence build/hw-13-part4-review/ and
docs/hw-13-part4-review.md (explicit reuse/tooling limits). With Jonas's
approval ac7acb5/bc39972 pushed to hw-13, GitHub verified at bc39972 before
these context-only records. Main/installed/settings untouched; hw13-5 next.
**Log.** 2026-10-30 part 5 implementation checkpoint: About/Monitor measured
RAM and managed/used/free/reserved projection verified; native temporary
warning removed. VZ2/4/8 light/dark PMM/OCR and reviewed screenshots agree
(2047/4095/8191 MB RAM; 2027/4075/8171 MB managed). Full normal/fatal ASan/
UBSan hosts, both zero-warning builds, 2138 Swift + 7 pixel guards,
TCG/HVF/Intel protected/persistence, native full smoke, 20 app-polish flows,
full VZ2/4/8 clocks, VZ8 high isolation/TLS and VZ8/HVF4/Intel4 3-GiB
stress pass. Evidence build/hw-13-part5/. Installed delivery/check-offs
are still pending; no main or installed data changed at this checkpoint.
**Log.** 2026-10-30 HW-13 COMPLETE: implementation 921c482, delivery
47060cc, UI acceptance b5c2958, followed by context-only completion.
All five parts and four shared MEM substeps verified. Full combined-source
matrix passes (build/hw-13-part5/matrix.json); reviewed VZ2/4/8 light/dark
About/Monitor show actual 2/4/8 GB with exact PMM figures. Both installed
builds/app zero warnings, kernels byte-identical to tested dev kernels.
With no VM running, main and Jonas's checkout fast-forwarded and builders
preserved ARM/Intel saved-data, legacy disk, launch settings and EFI store
hashes exactly (preservation.json). Private delivered-image VZ8 UI passes,
About screenshot reviewed, ready_to_test written. Limits: legacy/DMA LOW,
Intel EL0/network unsupported, launch menu up to 8 GB within half host RAM;
full MEM cards are not claimed complete. Jonas approved publication on
2026-10-30: origin/main and origin/hw-13 verified at 8c156a0 after a
normal fast-forward push; all 10 pending main commits passed gitleaks
and added-line privacy scans. Publication receipt changes context only.
**Handoff.** HW-13 is done and installed. Next session is **launch-3 /
HW-14**, not more HW-13. Read HW-14, HW-12's version-1 launch-settings
contract and ojjy Control AGENTS before touching the shared settings path.
Do not reset installed disks/settings. Keep private-copy tests and
NTP-aware clock checks. Current delivery evidence is build/hw-13-part5/;
Python+Pillow is build/hw-13-part3/venv/bin/python. Final completion
records change context only; installed kernels match tested source.
Part 5 is published with explicit approval; ask for new approval for
future work. Stop here, do not start launch-3 today.

### HW-14 — QEMU script and ojjy Control use the launch settings
- Phase: P2 · Size: S · Status: done
- Depends: HW-12
- Files: scripts/run-qemu.sh, ../ojjy-control/, tests/

**Context.** Launch series part 3 (launch-3). Native app owns the version-1
`build/launch-settings.json`; QEMU and ojjy Control still have separate defaults.
**Goal.** All three read/write the same file without losing user settings.
**Steps.**
- [x] Read HW-12 schema/validation and ojjy Control AGENTS.md; document override — done 2026-10-30 (test: baseline.log, contract-v2.log, runner-intel-{1440,1600}/report.json in build/hw-14/)
  precedence and QEMU scanout vs logical size for HVF/TCG/Intel.
- [x] Wire QEMU RAM/screen/network/fullscreen and Control editor to the same — done 2026-10-30 (test: scripts/qemu_launch.py, vmapp/LaunchSettings.swift, ojjy-control 8b1fac7; final2-host.log, final2-sanitize.log, final3-vmapp.log 2149 Swift guards)
  file with safe defaults, host clamps and atomic writes; preserve unknown keys.
- [x] Test missing/corrupt/partial files, overrides and cross-tool round trips; — done 2026-10-30 (test: 9/9 Swift/Python/Control cross-tool cases, final3-control-native/report.json light/dark captures, final2-runner-{tcg,hvf,intel}/report.json, final3-vz-launch/report.json 4-case matrix)
  live boot QEMU and verify Control changes appear in the native launch menu.
**Done when.**
- [x] No competing settings store; native app, QEMU and Control agree after — done 2026-10-30 (test: 9/9 cross-tool cases, final3-control-native/report.json, preservation.json saved-data/settings/EFI/legacy hashes unchanged, installed-vz-boot.log)
  restart; automated tests bypass menus and never modify installed user data.
- [x] Cross-project tests/context/checklists pass and evidence is recorded. — done 2026-10-30 (test: checklist.py --strict clean on both repos, roadmap check 698 cards 0 errors, OS 89bf21c + Control ba37569 fast-forwarded to main)
**Tests.** QEMU runner argument tests, Control unit/UI tests, native guard
round trips and private-disk QEMU/VZ smoke; strict checklists for both repos.
**Pitfalls.** Control is a separate private repo: claim/commit/update context
there too. Avoid absolute personal paths, shell injection, JSON truncation and
concurrent-writer loss; CPU stays 1 until SMP-01. Never reset user disks.
**Refs.** HW-12, HW-13, NATIVE_VM_PLAN.md; ojjy Control project context.
**Decision.** Keep HW-12 version 1 and a single build/launch-settings.json;
see docs/hw-14-launch-settings.md for override/scanout and cooperative
revision/lock rules. Never modify installed images while a VM is running.
**Log.** 2026-10-30 (board 046904 + Control 939692; OS commits `448b5c7`,
`29e076b`, `27d6f02`, `89bf21c`; Control commits `8b1fac7`, `d0c3f80`,
`084f22b`, `ba37569`): shared version-1 `build/launch-settings.json` across
native `ojjyOS.app`, `scripts/run-qemu.sh` (`scripts/qemu_launch.py`), and
ojjy Control (`GET|PUT /api/ojjyos/launch-settings` + React/classic Home
editors) with SHA-256 revisions, fail-fast `.launch-settings.lock`, unknown-key
preservation, bounded Intel `opt/ojjyOS/display` fw_cfg mode selection, and
backing-scale screenshot guards. Evidence: `build/hw-14/` (`final2-host.log`,
`final2-sanitize.log`, `final3-vmapp.log`, `final3-control-native/report.json`,
`final2-runner-{tcg,hvf,intel}/report.json`, `final3-vz-launch/report.json`,
`preservation.json`, `installed-vz-boot.log`). Limits: CPU stays 1 until
SMP-01; Intel networking unsupported; restart `:4321` once to activate Control
writes on the live dashboard.
**Verification follow-up.** 2026-10-30: fresh builds and full host/sanitizer/
Swift/Control suites, both private Home editors, cross-tool native menu,
TCG/HVF/Intel persistence, ARM4/8 + Intel4 launch RAM/display, four native
lifecycle cases, installed-image VZ6 boot, VZ8 About/Monitor and VZ8/Intel4
3-GiB peak ownership/recovery pass. Evidence: `build/hw-14-verify/` and
`docs/hw-14-verification.md`. Full installed image/settings/EFI/legacy
hashes unchanged. Restarted the old dashboard with preserved configuration;
live authenticated settings GET 200 and writable, so the restart above is
no longer pending. Production sources unchanged; live test now accepts
`--memory-mb` instead of hard-coding 3072 MiB.

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
