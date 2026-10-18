# HANDOFF — Native VM (ojjyOS.app on Apple Virtualization.framework)

## Current correction (2026-10-18 resumed session)

N8 kernel USB component now exists and passes full QEMU xHCI input/persistence
smoke and pointer QA; VZ enumerates keyboard + absolute pointer. Native input
acceptance awaits the not-yet-built N9/N10 app/control channel. Read the newest
AGENTS.md section and NATIVE_VM_PLAN.md log before restarting any driver work.

The paused snapshot below is historical. The guest now boots VZ with all
five virtio devices and a real DHCP lease. Fresh evidence and current next
steps are in the newest `NATIVE_VM_PLAN.md` log entries and `AGENTS.md`.

Do NOT repeat the earlier marker deductions as facts: markers accidentally
left at earlier boot points invalidated downstream conclusions. In particular,
VZ reports **PARange=1 (36 physical bits)**, not >=2 (40 bits). The old
IPS fallback to 32 made every above-4GiB BAR inaccessible. PCI status requires
a separate ACK step; console DEVICE_READY's value is **1**, not 0; NET_F_MTU
must be negotiated; RNG legitimately has no DEVICE capability. The GPU's
QEMU-MMIO scan must be guarded by platform.qemu. These are actual code fixes
with fresh live serial evidence, not a new queue-layout guess.

Fast PCI test rig is now supported: `OJJY_PCI=1 python3 -B tests/qemu_smoke.py
--arch aarch64 --image build/dev-aarch64/ojjyos3.img --output <new-dir>
--fresh-data --protected`. It uses QEMU `highmem=off`, keeping ECAM and BARs
within the owned mappings without inventing a giant high-memory map.

Written 2026-10-18 by the previous AI session, which was stopped mid-task by
Jonas. You (the next AI) have NO other context: read this whole file, then
`AGENTS.md` (project rules + history), `NATIVE_VM_PLAN.md` (probe facts),
`RENDERER_UI_PLAN.md` (the just-finished renderer/UI work).

## ⚠️ PROGRESS UPDATE (paused again 2026-10-18, ~16:30) — READ FIRST

A second session continued from the WIP and got much further. State:
branch `native-vm`, commits `65c605f` (N2 done) and `b6e5aa3` (N3 done),
PLUS a large UNCOMMITTED working tree (N5/N6 + three bug fixes). Both
builds compile with 0 warnings and `bash scripts/test.sh` passes right
now. THE WORKING TREE IS BEING COMMITTED AS "N5/N6 WIP" — check
`git log`/`git status` for the exact commit after this file was written.

### Done and committed

- **N2** (`65c605f`): platform plumbing — `platform.c` ACPI parse,
  `platform_init()` first in kernel_main, `pci.h/.c` (ECAM via mmio.h,
  bus-0 enumerate, BAR decode, Memory+BusMaster enable, `pci_find()`).
  QEMU TCG+HVF smoke --protected + pointer PASS (build/n2-*).
- **N3** (`b6e5aa3`): owned page tables — 2 MiB blocks classified from
  the UEFI map (RAM=Normal, else Device), 4-8 GiB Device blocks for VZ's
  64-bit virtio BARs, IPS=40 when PARange>=2. All suites PASS (build/n3-*).

### Done, in the WIP commit (N5/N6, kernel side)

- `VirtioDev` transport abstraction in `virtio.h/.c`: one handle per
  device, mmio + virtio-pci modern (capability walk per spec 4.1;
  offsets in the code). All drivers (blk/net/rng/gpu/input) converted.
  `virtio_pci_init()` scans bus 0 for 1af4:1041/1042/1043/1044/1050.
- 16-bit `mmio_read16/mmio_write16` added to `mmio.h` (PCI queue fields).
- `virtio_console.c`: **Apple's console is FULL virtio-serial** (this
  took a day of marker debugging — read "VZ console facts" below).
  Linux layout: rx0=q0, tx0=q1, ctrl-in=q2, ctrl-out=q3. Protocol:
  DEVICE_READY(0) → PORT_READY(3,value 1) → PORT_OPEN(6,value 1) with
  `struct {u32 id; u16 event; u16 value}` on queue 3, THEN data on
  queue 1 actually reaches the host. Constants from Linux uapi
  `linux/virtio_console.h` (PORT_OPEN=6; the old guess "3" is PORT_READY).
- Early GPU probe also tries PCI (`pci_find(1af4,0x1050)`) so the VZ
  splash works; `virtio_gpu_owns()` now compares VirtioDev handles.
- Panic handler prints FAR_EL1 and the caller (x30 at exception entry —
  VEC_SYNC passes it as 4th arg). This found two real bugs (below).
- `virtio_dev_num_queues()` (PCI only; **VZ does not implement the
  num_queues register** — reads 0 — so detect the serial layout by
  device id, not by num_queues).

### Bugs found and fixed (in the WIP commit)

1. **N3 map bug**: `block_is_ram()` excluded type 0
   (EfiReservedMemoryType); QEMU's ramfb lives in a type-0 region, so
   the first framebuffer write took an alignment fault in memset.
   Fixed: types 0-7, 9, 10 (except 8) all count as RAM.
2. **PMM-before-init bug (the big one)**: the renderer's
   `fb_use_backbuffer(true)` allocates the back buffer with
   `pmm_alloc_bytes()` — but the boot splash calls it BEFORE
   `pmm_init()`. The zero-cleared bitmap says every page is free, so it
   "allocated" page 256 at 0x100000 (which the owned page tables map
   Device) and the zeroing memset faulted. The standard smokes pass
   because their GOP is 0x0 (no fb), so nothing allocated. Fixed:
   `pmm_ready` flag — `pmm_alloc_page`/`pmm_alloc_contiguous` return 0
   before init; the splash then draws direct to scanout
   (fb_use_backbuffer returns false; splash ignores the return value).
3. Panic diagnostics (FAR + caller) — keep them, they are gold.

### VZ console facts (measured, marker-proven)

- VZ console = 1af4:1043 on PCI 0:5.0, 4 queues: init(0),(1),(2),(3)
  all succeed. Control messages on q0/q2 never complete; **queue 3 was
  never actually tested with a control message** (earlier tests sent
  data-shaped messages to the wrong queues with wrong constants).
- Data on q1 completes (descriptor returns) but is DROPPED until the
  port is opened. Apple's binary (strings in the VZ XPC service) says:
  "Cannot transmit data for closed port id", "sent PORT_OPEN without
  DEVICE_READY first", "has_received_console_device_ready".
- probe.swift now prints `stopped-after N.NNs` when the VM stops
  (Timer polls state; .stopped/.error only). Kernel boot-to-console ≈
  0.75 s. The 1-bit marker technique: `arch_power_off()` in guest code
  → VM state .stopped(0) vs .running(1) at probe end. Multi-value:
  busy-wait value×500 ms then power off; decode from the timestamp.
- The mystery that remains: in the last VZ runs, console queue init
  appeared to fail/hang (marker never fired) — BUT a failed init
  (`return -1`) and a hang look identical through this probe. The
  PMM bug above may or may not explain it (VZ boots with fb=0x0, so
  probably NOT). Re-test after this commit; if init(1) still fails,
  bisect with markers/delay-readout, and consider: QEMU test rig (see
  next section) to iterate faster.

### Fast test rig for the console (USE THIS — minutes, not hours)

QEMU can emulate the SAME modern PCI console:
```
qemu-system-aarch64 -M virt,gic-version=2 -accel tcg -cpu cortex-a72 \
  -m 2048 -bios /opt/homebrew/share/qemu/edk2-aarch64-code.fd \
  -global virtio-mmio.force-legacy=false -device ramfb \
  -device virtio-keyboard-device -device virtio-tablet-device \
  -drive file=DISK,format=raw,if=none,id=hd -device virtio-blk-device,drive=hd \
  -chardev file,id=clog,path=/tmp/qconsole.log \
  -device virtio-serial-pci,disable-legacy=on,id=vs0 \
  -device virtconsole,bus=vs0.0,chardev=clog \
  -serial file:/tmp/qemu-serial.log
```
`disable-legacy=on` makes it modern (device id 1043, like VZ). Without
it you get legacy 0x1003 (not attached). QEMU/EDK2 DOES publish MCFG
(ECAM 0x4010000000, above 4 GiB!) — but `platform.c` force-zeroes
`platform.ecam` on QEMU, so `virtio_pci_init` never runs there. To use
this rig you must let ecam stand when MCFG exists (drop the force-zero
in `platform_init`, keep QEMU's virtio-mmio init as `platform.qemu`).
Verify the standard smokes still pass afterwards (they should: the PCI
scan only adds; mmio attach is unchanged). Also note QEMU's 64-bit BARs
land at 0x8000000000+ — the 4-8 GiB device blocks don't cover them;
if you keep this rig, map the ECAM/BAR window it reports (or extend
the device mapping to 128 GiB when IPS allows) — under VZ the BARs are
at 0x1_000x_0000 which IS covered.

### Remaining TODO (in order)

1. Re-run both builds + test.sh on this tree (was green at pause).
2. VZ probe (`vmapp/probe.swift`, build/run commands in §2 below):
   expect `/tmp/vz-serial.log` to fill with the boot log ending in
   "[INIT] Entering main loop" = N6 acceptance. Use the QEMU rig above
   for faster iteration if it fails.
3. If the console still fails on VZ: test control messages on queue 3
   with markers; check the ctrl message buffer/statics; consider
   posting the ctrl-in buffer BEFORE the handshake.
4. N7/N4: verify the menu-bar clock (boot_epoch from OJJYBOOT.LOG
   "[BOOT] Clock:") and timer ticks ([PERF] lines) under VZ.
5. N8: xHCI + USB HID (largest piece; full bring-up in §4 N8).
6. N9: the app (§4 N9). vmapp/probe.swift is the working reference
   (serial via VZVirtioConsoleDeviceSerialPortConfiguration — the
   isConsole/VZConsoleDeviceConfiguration variant was tried and
   reverted).
7. N10: --control socket + tests/vz_smoke.py.
8. N11: docs, full regression, merge native-vm into main, rebuild
   installed images + data-partition hash check.



## 0. What Jonas wants (his words, in order)

1. "qemu is laggy and i want it to feel smooth like a native virtualbox
   windows aarch64 vm" + "sure implement the rest".
2. Build our own Mac app that runs ojjyOS with Apple's
   **Virtualization.framework** (VZ) instead of QEMU: native window, native
   full screen, Retina, smooth input — like Parallels/UTM "Apple" mode.
3. Keep QEMU working (it is the automated test platform and the Intel path).

## 1. Non-negotiable project rules (from AGENTS.md — obey exactly)

- **Never push** to any remote. Commit locally only, as:
  `git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit`
  End every commit message with the line
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Custom kernel only (no Linux/BSD code). Credit "Jonas Lee" in user-facing
  text; never put AI names in user-facing strings. Keep the blue Tahoe theme.
- Run `bash scripts/test.sh` (must print ALL TESTS PASSED for every suite)
  and both builds with **0 warnings** before each commit.
- Never stop Jonas's own VM; never overwrite `build/aarch64/ojjyos3.img`
  while a qemu/ojjyOS process has it open (`pgrep -fl qemu-system`). Develop
  with `OJJY_BUILD_DIR=$PWD/build/dev-aarch64 ARCH=aarch64 bash scripts/build.sh`
  (separate image), test on private copies only.
- Keep large buffers out of BSS (allocate with `pmm_alloc_bytes`). The ARM
  kernel image must stay small (it is loaded at a fixed address).
- Work autonomously; log progress in `NATIVE_VM_PLAN.md` ("## Log") as you go.
- The untracked file `ojjyos-v3-bugs-macos-features-custom-kernel.json` must
  stay uncommitted.

## 2. Repository state when you start

- Branch `main` = last green state (commit `96358a9` + this handoff).
- Branch **`native-vm`** = `main` + the unfinished step N2 work (commit
  "WIP N2 ..."). **It does not build yet.** Do `git checkout native-vm` and
  continue there; merge into main (fast-forward) only when QEMU suites pass.
- Machine: Apple M2 Pro, macOS 26.6.2, Swift 5.8 / CommandLineTools SDK 13.3,
  QEMU 11.1.2 (Homebrew), Homebrew LLVM in /opt/homebrew/opt/llvm.

### What already works (do not redo)
- `scripts/run-qemu.sh` runs ARM under QEMU+HVF (GICv3, 2880x1800 virtio-gpu,
  2x HiDPI UI). All renderer/UI/animation work is done (see RENDERER_UI_PLAN.md).
- Bootloader (`boot/ojjy-boot.c`) writes everything it prints to
  `\ojjyboot.log` on the ESP and dumps memory map, ACPI (MADT/MCFG/GTDT/FADT)
  and PCI devices. Read it from any disk image with
  `python3 tools/esp_read.py <disk.img> OJJYBOOT.LOG`.
- Kernel is linked at **0x71000000** (`kernel/linker-aarch64.ld`), valid RAM
  on both QEMU and VZ. Under VZ the bootloader loads it and jumps.
- `vmapp/probe.swift` = minimal headless VZ host. Build/run:
  ```
  cd vmapp && swiftc -O probe.swift -o /tmp/ojjy-probe && \
    codesign --sign - --force --entitlements ojjyOS.entitlements /tmp/ojjy-probe
  cp build/dev-aarch64/ojjyos3.img /tmp/vz-disk.img
  /tmp/ojjy-probe /tmp/vz-disk.img /tmp/vz-vars.fd /tmp/vz-serial.log 20
  python3 tools/esp_read.py /tmp/vz-disk.img OJJYBOOT.LOG
  ```
  (args: disk, EFI variable store (created if missing), serial log file,
  seconds to run). `/tmp/vz-serial.log` receives the guest's virtio-console
  output once the kernel drives it (currently empty: nothing writes there yet).

### Uncommitted-then-WIP-committed N2 work on branch `native-vm`
- NEW `kernel/src/arch/aarch64/platform.{h,c}`: `Platform platform` +
  `platform_init(const BootInfo*)` parses RSDP->XSDT: MADT (GICD/GICC/GICR,
  version), MCFG (ECAM base, bus range), GTDT (virtual timer PPI), SPCR
  (PL011). RSDP OEM id "BOCHS" => QEMU (`qemu_defaults()`: GICD 0x08000000,
  GICC 0x08010000, GICR 0x080A0000, UART 0x09000000, RTC 0x09010000,
  ecam forced 0). Otherwise name "Apple Virtualization".
- `gicv2.h/.c`: GICD/GICC/GICR bases now `platform.gicd/gicc/gicr`; version
  from `platform.gic_version` (fallback: old PFR0/MIDR heuristic).
- `timer_a64.c`: uses the **virtual** timer (`cntv_tval_el0`, `cntv_ctl_el0`),
  PPI `platform.vtimer_ppi`; `timer_get_ticks()` now = (CNTVCT - start)/tick
  (counter based). `arch64.c` IRQ handler checks `intid == platform.vtimer_ppi`.
- `virtio.h` `virtio_now_ms()` and `user_arch.c` `user_arch_millis()` read
  `cntvct_el0`.
- `pl011.c`: `serial_putc` appends to a 64 KiB ring; writes the UART only if
  `platform.uart`; new `serial_set_sink(void (*)(char))` replays the ring into
  a late console and then forwards every byte.
- `pl031.c`: if `!platform.rtc`, time = `g_boot_info->boot_epoch` + uptime.
- `boot_info.h` + bootloader `BootInfo`: new field `uint64_t boot_epoch`
  (filled by `BootEpoch()` from EFI `RT->GetTime`). Both bootloaders build.
- `arch64.c a64_init()`: `if (platform.qemu) virtio_mmio_init(); if
  (platform.ecam) pci_init();` and includes `"pci.h"` — **pci.h/pci_init do
  not exist yet** (that is why it does not build).

## 3. Facts about the VZ machine (measured; full table in NATIVE_VM_PLAN.md)

- RAM 0x70000000..0xF0000000 (2 GiB). Firmware = Apple EFI, ACPI only (FACP,
  GTDT, APIC, MCFG; no SPCR, no DSDT devices you need). PSCI via HVC.
- GICv3: GICD 0x10000000, GICR 0x10010000 (MADT type 0x0E), MSI frame
  (type 0x0D, ignore). GICv3 CPU interface = system registers (already used).
- Timer PPIs: virtual 27, physical 30.
- PCIe ECAM 0x40000000, buses 0..255. Bus 0 devices, BARs ALREADY ASSIGNED by
  firmware (read them from config space, do not reassign):
  | dev | id | what | BAR0 (64-bit) | BAR2 |
  |---|---|---|---|---|
  | 0:0 | 106b:1a05 | host bridge | | |
  | 0:1 | 1af4:1041 | virtio-net | 0x1_0003_0000 | 0x5000_6000 |
  | 0:5 | 1af4:1043 | virtio-console | 0x1_0002_0000 | 0x5000_2000 |
  | 0:6 | 1af4:1042 | virtio-blk | 0x1_0001_0000 | 0x5000_5000 |
  | 0:7 | 1af4:1050 | virtio-gpu | 0x1_0000_0000 | 0x5000_4000 |
  | 0:8 | 1af4:1044 | virtio-rng | 0x1_0004_0000 | 0x5000_3000 |
  | 0:9 | 106b:1a06 | **xHCI** (class 0x0C0330) | bar0 0x5000_1000, bar1 0x5000_0000 (32-bit) | |
- No linear GOP framebuffer (virtio-gpu GOP is BltOnly) — kernel must drive
  virtio-gpu itself (driver exists for virtio-mmio: `virtio_gpu.c`).
- No UART: kernel log must go out through **virtio-console** (port 0).
- Keyboard/pointer are **USB HID behind xHCI** (VZ has no virtio-input).

## 4. Your task list — do these IN ORDER, each with its acceptance check

### N2 (finish) — platform plumbing, QEMU must stay green
1. Create `kernel/src/arch/aarch64/pci.h` + `pci.c` (see N6 for full design;
   for now `void pci_init(void)` may just enumerate bus 0 and print
   `[PCI] b:d.f vendor:device class` lines).
2. Add `src/arch/aarch64/platform.c` and `pci.c` to `ARCH_C_SOURCES` and to
   `ALL_CLEAN_OBJECTS` in `kernel/Makefile` (aarch64 section, next to
   `virtio_gpu.c`). Recipes need TABs.
3. In `kernel/src/main.c` `kernel_main()`: on aarch64 call
   `platform_init(boot_info);` as the VERY FIRST statement (before
   `serial_init`). Include `"arch/aarch64/platform.h"` under the existing
   `#else` (aarch64) include block. Print one line after the banner:
   `serial_printf("[PLATFORM] %s gic v%d gicd 0x%x ecam 0x%x\n", ...)`.
4. `pl031.c` still has `#define RTC_BASE 0x09010000UL` (unused now) — delete.
5. Build ARM + x86 (0 warnings), `bash scripts/test.sh`, then:
   `python3 -B tests/qemu_smoke.py --arch aarch64 --image build/dev-aarch64/ojjyos3.img --output build/n2-tcg --fresh-data --protected`
   and the same with `OJJY_ACCEL=hvf` (output dir `build/n2-hvf`), plus
   `tests/qemu_pointer.py --image ... --kernel build/dev-aarch64/kernel.bin --output build/n2-ptr`.
   **Acceptance:** all PASS. Commit "N2: ACPI platform description ...".
   Note: under QEMU+TCG the virtual timer must still tick (it does on QEMU);
   if the pointer/smoke tests show timing problems, check `timer_get_ticks`.

### N3 — page tables for VZ (file `kernel/src/arch/aarch64/user_arch.c`)
Current `user_arch_init()` maps L1[0] (0-1 GiB) as Device and L1[1..3]
(1-4 GiB) as Normal 1 GiB blocks, IPS=32-bit. On VZ that maps ECAM
(0x40000000) and BARs (0x5000_xxxx) as cacheable RAM (wrong) and cannot reach
virtio BARs above 4 GiB. Do:
1. Allocate four L2 tables (4 KiB each, static aligned arrays are fine) for
   L1[0..3]; fill 2 MiB block entries: Normal (`1|AF|SH_INNER|NORMAL|UXN`)
   if any byte of the block is RAM per the UEFI memory map in `g_boot_info`
   (EFI types 1-7, 9, 10; 7 = conventional), else Device
   (`1|AF|PXN|UXN`, attr index 0 = Device-nGnRnE). Keep L1[0] device-only
   semantics for QEMU (QEMU RAM starts 0x40000000, so it falls out
   naturally).
2. Map L1[4..7] (4-8 GiB) as Device 1 GiB blocks in `kernel_l1` (VZ virtio
   BARs at 0x1_000x_0000). NOTE: `user_arch_prepare()` copies `kernel_l1`
   into `app_l1` and then overwrites `app_l1[4]` for the user region
   (USER_BASE) — keep that: apps don't touch devices.
3. TCR: set IPS to 40-bit (`2ULL << 32`) if `ID_AA64MMFR0_EL1.PARange >= 2`.
   Check `user_arch_prepare` still rejects user pages >= 4 GiB physical.
4. **Acceptance:** QEMU TCG/HVF smoke --protected still PASS (EL0 fault
   tests included). Commit.

### N5/N6 — PCI + virtio-pci transport + virtio-console (gets you a log!)
1. `pci.c`: config access via ECAM: address =
   `platform.ecam + (bus<<20) + (dev<<15) + (fn<<12) + off`, use
   `mmio_read32/mmio_write32` from `mmio.h` (MANDATORY: plain LDR/STR or HVF
   aborts). Enumerate bus 0 (VZ has no bridges), enable Memory Space + Bus
   Master (command register bits 1 and 2). BAR decode: 64-bit if (bar & 6)==4.
2. virtio-pci modern transport (spec 4.1): walk the capability list (status
   bit 4, cap pointer 0x34); vendor caps (id 0x09) with `cfg_type` at +3,
   `bar` at +4, `offset` at +8, `length` at +12; `notify_off_multiplier` at
   +16 for type 2. Types: 1 COMMON, 2 NOTIFY, 3 ISR, 4 DEVICE.
   Common cfg layout (offsets): device_feature_select 0x00, device_feature
   0x04, driver_feature_select 0x08, driver_feature 0x0C, msix_config 0x10,
   num_queues 0x12, device_status 0x14 (u8), config_generation 0x15,
   queue_select 0x16, queue_size 0x18, queue_msix_vector 0x1A, queue_enable
   0x1C, queue_notify_off 0x1E, queue_desc 0x20 (u64), queue_driver 0x28,
   queue_device 0x30. Notify address = notify_bar_base + cap.offset +
   queue_notify_off * multiplier; write the queue index (u16).
   Add 16-bit and 8-bit MMIO helpers to `mmio.h` (`ldrh/strh`) — again plain
   instructions only. Set queue_msix_vector to 0xFFFF (no MSI-X; we poll).
3. Refactor `arch/aarch64/virtio.c` so a device is a `VirtioDev` handle
   (transport = MMIO or PCI, function pointers or a switch for: status
   get/set, features, queue setup, notify, config read8/16/32). Today drivers
   take `uint32_t base` and call `virtio_queue_init_index(vq, base, mem,
   idx)`, `virtio_dev_ready(base)`, `virtio_notify(vq)`, read config via
   `mmio_read32(base + VIO_CFG + off)`. Convert `virtio_blk.c`,
   `virtio_net.c`, `virtio_rng.c`, `virtio_gpu.c`, `virtio_input.c` to take
   `VirtioDev *`. Keep `tests/test_virtqueue.c` passing (it fakes MMIO via
   `virtio_host_mmio_read/write` under `OJJY_HOST_TEST`).
   VZ modern virtio requires VIRTIO_F_VERSION_1 (feature bit 32) — already
   negotiated by `virtio_dev_start`.
4. NEW `virtio_console.c` (device 0x1043): queues 0 = receiveq0, 1 =
   transmitq0. TX: copy bytes into a 4 KiB buffer, submit one descriptor,
   poll used ring (with `virtio_now_ms()` deadline, never spin counts).
   Buffer bytes in the sink and flush on '\n' or when 256 bytes queued, and
   from `a64_poll_input()`. Register with `serial_set_sink()` (replays the
   whole boot log). RX later (optional console input).
5. Init order on VZ: `pci_init()` happens inside `a64_init()`, which runs
   after `kheap_init`. The GPU is probed early (`virtio_gpu_display_init` in
   `main.c` right after `kheap_init`) — make that function also try PCI
   (call a `pci_find(0x1af4, 0x1050)` that works before `pci_init`) so the
   splash appears on VZ.
6. **Acceptance:** `/tmp/vz-serial.log` from the probe shows the kernel boot
   log ("[PLATFORM] Apple Virtualization ...", "[INIT] Entering main loop").
   QEMU suites still PASS. Commit after each sub-step that builds.
   Debug tip if it dies before the console works: in `platform_init`/early
   code you can call PSCI `SYSTEM_OFF` (`arch_power_off()`) as a 1-bit
   marker — the probe prints the VM state; VM stopping = marker reached.

### N7 — clock: done in the WIP (`boot_epoch`); verify the menu-bar clock is
correct under VZ (bootloader prints "[BOOT] Clock: <unix>" in OJJYBOOT.LOG).

### N4 — timer: done in the WIP (virtual timer). Verify ticks under VZ by the
"[PERF]" lines and that animations run (they are time based).

### N8 — xHCI + USB HID (largest piece; new files `xhci.c`, `usb_hid.c`)
Goal: keyboard + absolute pointer from `VZUSBKeyboardConfiguration` and
`VZUSBScreenCoordinatePointingDeviceConfiguration`, fed into the existing
input layer: keys -> the same path virtio-input uses
(`virtio_input_handle_key(code, value)` style / `input_post_*` in
`kernel/src/drivers/input.c`), pointer -> absolute position scaled to
`fb_get_width()/fb_get_height()` (see how `virtio_input.c` handles EV_ABS).
Polling only (no interrupts). Steps:
1. Map BAR0 (0x5000_1000? read it — VZ gave bar0 0x50001000 and bar1
   0x50000000; use the BAR that contains the capability registers: CAPLENGTH
   at +0 must be 0x20-ish and HCIVERSION at +2 = 0x0100/0x0110).
2. Standard xHCI bring-up: read HCSPARAMS1 (+4: MaxSlots, MaxPorts),
   HCCPARAMS1 (+0x10, CSZ bit 2 = 64-byte contexts), DBOFF (+0x14), RTSOFF
   (+0x18). Operational base = cap + CAPLENGTH: USBCMD +0, USBSTS +4,
   PAGESIZE +8, CRCR +0x18, DCBAAP +0x30, CONFIG +0x38, PORTSC at +0x400 +
   0x10*(port-1). Halt (USBCMD.RS=0, wait HCH), reset (HCRST), wait CNR=0,
   set CONFIG.MaxSlotsEn, allocate DCBAA (+ scratchpad buffers if
   HCSPARAMS2 asks), command ring (TRB ring with Link TRB, cycle bit),
   event ring + ERST (interrupter 0 at runtime +0x20: IMAN, IMOD, ERSTSZ
   +0x28, ERSTBA +0x30, ERDP +0x38), run (RS=1). All memory from
   `pmm_alloc_bytes` (identity mapped, 64-byte aligned, zeroed).
3. For each port with CCS: reset port (PR), wait PRC; Enable Slot command;
   Address Device (input context: slot ctx route/speed/root port, EP0 ctx
   with max packet size by speed); GET_DESCRIPTOR device (8 then 18 bytes),
   configuration descriptor (full), SET_CONFIGURATION; find the HID
   interface + interrupt-IN endpoint; Configure Endpoint command; for the
   keyboard send SET_PROTOCOL(boot=0) and SET_IDLE(0); for the pointer
   fetch the HID report descriptor (GET_DESCRIPTOR type 0x22 to interface)
   and parse X/Y (Usage Page Generic Desktop 0x01, usages 0x30/0x31,
   logical min/max, report size/count, report ID) and buttons (Usage Page
   0x09).
4. Keep one Normal TRB queued per interrupt endpoint; ring its doorbell
   (DB base + 4*slot, target = endpoint DCI); in `a64_poll_input()` drain
   the event ring (Transfer Event -> parse report -> requeue), update ERDP.
5. Keyboard boot report: [mods, 0, k1..k6]; convert USB HID usage codes to
   the kernel's `KeyCode` enum (see `kernel/src/drivers/input.h` and the
   evdev table in `virtio_input.c`; build a usage->KeyCode table). Generate
   press/release by diffing with the previous report; modifiers from byte 0
   (LCtrl, LShift, LAlt, LGUI = Cmd, and right-hand versions).
6. **Acceptance:** under the VZ app (N9) you can type at the setup screen
   and the pointer lands exactly where the macOS cursor is.

### N9 — the host app `vmapp/` -> `build/ojjyOS.app`
1. `vmapp/main.swift` (AppKit, no Xcode project needed): NSApplication +
   NSWindow (title "ojjyOS", min 960x600) whose contentView is a
   `VZVirtualMachineView` (`capturesSystemKeys = true` so Cmd+Space,
   Cmd+Tab reach the guest). Configuration like `vmapp/probe.swift` but:
   cpuCount 1 (kernel is single-CPU), memory 2 GiB, disk
   `build/aarch64/ojjyos3.img` (path argument / default next to the app),
   EFI vars `build/aarch64/efi-vars.fd`, serial log
   `build/aarch64/serial-vz.log`, virtio-gpu scanout **2880x1800**, NAT net,
   entropy, USB keyboard + USB screen-coordinate pointer. On macOS 14+ set
   `automaticallyReconfiguresDisplay` via
   `view.setValue(true, forKey: "automaticallyReconfiguresDisplay")` guarded
   by `responds(to:)` (SDK 13.3 lacks the symbol).
   Menu: ojjyOS (About "ojjyOS — created by Jonas Lee", Quit), VM (Start,
   Request Stop, Force Stop), View (Enter Full Screen = window
   `toggleFullScreen`). Refuse to start if the disk is locked by QEMU
   (`flock` test) — never corrupt Jonas's image.
2. `scripts/build-vmapp.sh`: `swiftc -O vmapp/main.swift -framework
   Virtualization -framework AppKit -o build/ojjyOS.app/Contents/MacOS/ojjyOS`,
   write `Info.plist` (CFBundleIdentifier com.jonaslee.ojjyos,
   CFBundleName ojjyOS, LSMinimumSystemVersion 13.0, NSPrincipalClass
   NSApplication), then `codesign --sign - --force --entitlements
   vmapp/ojjyOS.entitlements build/ojjyOS.app`.
   `scripts/run-vm.sh`: build if needed, `open build/ojjyOS.app`.
3. Icon: reuse `tools/mkicons.py` About icon (or render a 1024 squircle) into
   an `.icns` via `iconutil` (optional).
4. **Acceptance:** Jonas double-clicks/`run-vm.sh`, sees the ojjyOS splash
   and login at Retina sharpness, full screen works, input is smooth.

### N10 — automated tests on VZ
QMP doesn't exist here. Add a test mode to the host app: `--control
<unix-socket>` accepting newline commands: `key <usage> <down>`, `text
<utf8>`, `move x y`, `click x y`, `screenshot <png path>`, `quit`. Inject
input by sending synthesized `NSEvent`s to the `VZVirtualMachineView`
(keyDown/keyUp/mouseMoved/mouseDown/mouseUp with locations in view
coordinates). Screenshot via `CGWindowListCreateImage` for the app's own
window id or `view.bitmapImageRepForCachingDisplay` (verify which captures
the Metal content). Then port `tests/qemu_smoke.py`'s first-boot flow into
`tests/vz_smoke.py` (setup account, login, terminal commands, `sync`,
reboot, password login). Keep QEMU tests as they are.

### N11 — finish
Update `AGENTS.md` (LATEST section), `README.md` (how to run the app),
`NATIVE_VM_PLAN.md` log; full QEMU regression (TCG + HVF + x86) + VZ smoke;
rebuild the installed images with `bash scripts/build.sh` (it preserves the
data partition — verify the data-partition SHA-256 equals the newest file
in `build/<arch>/backups/`, offset 67584*512, length (262144-34-67584)*512).
Give Jonas a plain-English summary.

## 5. Gotchas already learned (read before debugging)
- HVF/VZ abort on MMIO instructions without syndrome (LDP/STP, post-index):
  always use `mmio.h` helpers. Skill: `~/.claude/skills/qemu-hvf-hobby-kernel-bringup`.
- Device waits must be wall-clock (`virtio_now_ms()`), never loop counts.
- Probing a missing GICv2 ID register faults; don't probe blind addresses
  on VZ either — only touch what ACPI/PCI describe.
- Large static arrays push the kernel image into firmware memory; allocate
  at runtime. Skill: `uefi-kernel-bss-firmware-collision`.
- `%p` in `serial_printf` already prints "0x".
- iCloud creates "file 2.o"-style duplicates in the build tree; they are
  gitignored — never `git add -A` blindly; review `git status`.
- Tests: `tests/qemu_*.py` honour `OJJY_ACCEL=hvf` and
  `OJJY_DISPLAY=virtio-gpu-device,xres=2880,yres=1800`.
