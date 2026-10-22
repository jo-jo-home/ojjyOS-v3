# 11 — Drivers

Every driver is its own card. Drivers run in the kernel (D-001) on top of
the driver model (`DRV-CORE-*`), use the DMA API (`MEM-18`), the IRQ
framework (`IRQ-01`) and sleep on wait queues (`SCH-04`) — never busy
loops. Platform order (D-017): VZ → QEMU ARM → QEMU x86 → real PCs.

Universal driver rules (apply to every card here):
- MMIO via `arch/aarch64/mmio.h`-style accessors (single LDR/STR) on ARM,
  `in/out`/MMIO helpers on x86. Never touch device memory through the
  cacheable direct map.
- All device-provided data (descriptors, lengths, indices, report
  descriptors, EDID, ACPI tables) is untrusted: bounds-check and fuzz.
- Every wait has a wall-clock timeout and a clear error log line
  (`[USB] port 3 reset timed out`).
- A host test drives the driver's protocol logic with a fake device model
  where possible (pattern: `tests/test_virtio_pci.c`, `tests/test_xhci.c`).
- Each driver logs one summary line at probe (`[NVME] Samsung 970 512 GB, 4 queues`).
- Hot-unplug must never crash: in-flight requests complete with errors.

---

## Driver framework

### DRV-CORE-01 — Driver model v2 (buses, devices, matching)
- Phase: P1 · Size: M · Status: todo
- Depends: IRQ-01, MEM-06
- Files: kernel/src/drivers/driver.c → kernel/drivers/core/

**Context.** `driver.c` is a simple registry with IRQ dispatch; each bus
(virtio-mmio, PCI, USB) does its own ad-hoc probing in `platform.c`.
**Goal.** Generic `bus`, `device`, `driver` objects: drivers declare match
tables (PCI vendor/device/class, USB class/VID:PID, FDT `compatible`, ACPI
`_HID`); the core probes and binds; devices form a tree with parents
(PCI bridge → device, xHCI → hub → HID) and names (`pci0:00:03.0`).
**Steps.**
1. Define structures and the bind/unbind life cycle (probe, remove,
   suspend, resume).
2. Port virtio (mmio + pci), xHCI and x86 legacy devices to it.
3. `devices` Terminal command printing the tree.
**Done when.** All existing drivers bind through the core on all
platforms; device tree printed at boot with `log=debug`.
**Tests.** Host test of matching rules; full smoke matrix.
**Pitfalls.** Probe order dependencies (e.g. IRQ controller first) —
deferred probing when a dependency is missing.
**Refs.** —

### DRV-CORE-02 — Device events to user space (`devd`)
- Phase: P2 · Size: S · Status: todo
- Depends: DRV-CORE-01, IPC-06
- Files: kernel/drivers/core/devevents.c, services/devd

**Goal.** Kernel posts add/remove/change events; `devd` service forwards
them (mount USB disks, notify "Keyboard connected", update Settings).
**Done when.** Plugging a USB stick in QEMU (monitor `device_add`) shows a
notification and mounts the volume (with DRV-USB-05, FS-20).
**Tests.** QEMU hot-plug test.
**Pitfalls.** Coalesce bursts of events.
**Refs.** —

### DRV-CORE-03 — Device power management
- Phase: P9 · Size: M · Status: todo
- Depends: DRV-CORE-01
- Files: kernel/drivers/core/power.c

**Goal.** Ordered suspend/resume of the device tree (children before
parents), runtime power-down of idle devices (USB autosuspend, disk
spin-down), wake sources.
**Done when.** Suspend/resume cycle on hardware leaves all devices working
(with SVC-60).
**Tests.** QEMU `system_powerdown`/S3 tests where possible.
**Pitfalls.** Drivers that cannot resume must refuse suspend.
**Refs.** ACPI device power states.

### DRV-CORE-04 — Firmware files for devices
- Phase: P9 · Size: S · Status: todo
- Depends: DRV-CORE-01, FS-01
- Files: kernel/drivers/core/firmware.c, /System/Library/Firmware

**Goal.** `request_firmware(name)` loads blobs (Wi-Fi, Bluetooth, GPU
microcode) from the system volume with size limits; licences recorded in
`THIRD_PARTY.md` (redistributable firmware only).
**Done when.** Used by DRV-NET-03.
**Tests.** Host test of path validation.
**Pitfalls.** Firmware is data from vendors: never execute on the CPU.
**Refs.** —

### DRV-CORE-05 — Driver test harness (fake MMIO / PCI / USB devices)
- Phase: P1 · Size: M · Status: todo
- Depends: —
- Files: tests/fakehw/ (new), tests/test_virtio_pci.c, tests/test_xhci.c

**Goal.** Reusable host-side fakes: MMIO register files with callbacks,
PCI config space, DMA memory, IRQ injection, a fake xHCI that runs
command/transfer rings, so every driver card can add protocol tests.
**Done when.** virtio-pci and xHCI tests are ported to the harness.
**Tests.** The harness's own tests.
**Pitfalls.** Keep fakes faithful to specs, not to our driver's bugs.
**Refs.** —

### DRV-UMD-01 — User-mode driver framework (later)
- Phase: P9 · Size: L · Status: todo
- Depends: DRV-CORE-01, IPC-07, MEM-18
- Files: kernel/drivers/core/umd.c, lib/libdriver

**Goal.** Let selected driver classes (USB printers, scanners, exotic
gadgets) run as user processes: MMIO/DMA grants via handles, interrupts as
port messages, IOMMU protection where available.
**Done when.** A USB printer driver runs in user space.
**Tests.** QEMU USB device test.
**Pitfalls.** Latency; never for storage or display.
**Refs.** D-001.

---

## Buses and firmware interfaces

### DRV-PCI-01 — PCI/PCIe core (both arches)
- Phase: P1 · Size: M · Status: todo
- Depends: BOOT-07, DRV-CORE-01
- Files: kernel/src/arch/aarch64/pci.c → kernel/drivers/pci/

**Context.** ARM has ECAM enumeration for VZ/QEMU; x86 has no PCI
(TODO.md "Intel parity").
**Goal.** ECAM from MCFG (both arches; legacy port 0xCF8 fallback on x86),
recursive bridge enumeration with bus numbering, BAR sizing and
assignment when firmware left them unassigned, capability list walking
(MSI, MSI-X, PCIe, power management), command register management,
`lspci` command.
**Done when.** x86 QEMU lists its devices; virtio-pci works on x86.
**Tests.** Host test with fake config space (bridges, 64-bit BARs);
QEMU x86 smoke with virtio-pci disk/net.
**Pitfalls.** 64-bit prefetchable BARs; don't move BARs firmware already
assigned to the framebuffer.
**Refs.** PCI Express Base Spec (config space, capabilities).

### DRV-PCI-02 — MSI and MSI-X
- Phase: P2 · Size: S · Status: todo
- Depends: DRV-PCI-01, IRQ-02, IRQ-03
- Files: kernel/drivers/pci/msi.c

**Goal.** Allocate vectors, program MSI/MSI-X tables, per-queue interrupts.
**Done when.** virtio and NVMe use per-queue MSI-X vectors on both arches.
**Tests.** Driver suites.
**Pitfalls.** MSI-X table in a BAR must be mapped uncached.
**Refs.** PCI Local Bus Spec 3.0 §6.8.

### DRV-PCI-03 — PCIe hot-plug and AER
- Phase: P9 · Size: M · Status: todo
- Depends: DRV-PCI-02, DRV-CORE-02
- Files: kernel/drivers/pci/hotplug.c

**Goal.** Native PCIe hot-plug (Thunderbolt docks later), advanced error
reporting logged instead of hanging.
**Done when.** QEMU `device_add` of a virtio-pci device on a hot-plug port
binds it.
**Tests.** QEMU hot-plug test.
**Pitfalls.** Surprise removal with DMA in flight.
**Refs.** PCIe spec hot-plug chapter.

### DRV-ACPI-01 — Full AML interpreter
- Phase: P3 · Size: L · Status: todo
- Depends: BOOT-07
- Files: kernel/src/drivers/acpi_aml.c → kernel/drivers/acpi/aml/

**Context.** `acpi_aml.c` decodes only the `_S5_` package.
**Goal.** Original AML interpreter: DSDT/SSDT loading into a namespace,
all object types, control flow, operation regions (SystemMemory, SystemIO,
PCI_Config, EmbeddedControl), methods with arguments/locals, mutexes,
`_OSI` answering "Windows 2020" for vendor compatibility, evaluation API
`acpi_eval(path, args)`.
**Steps.**
1. Parser for the full AML grammar into a tree (no execution) with
   host tests against QEMU/OVMF and real laptop DSDT dumps.
2. Executor for methods; namespace with scopes and aliases.
3. Operation region handlers; `_PRT` interrupt routing; `_STA/_INI`.
**Done when.** Namespace of QEMU x86 and two real PCs loads; `_PRT`
routes PCI interrupts on x86 QEMU without hard-coded tables.
**Tests.** Host tests with captured DSDTs (stored as vectors); fuzzing.
**Pitfalls.** Infinite loops in firmware AML: bound execution time;
re-entrancy with SCI handlers.
**Refs.** ACPI 6.5 spec ch. 19–20 (AML).

### DRV-ACPI-02 — ACPI events (power button, lid, GPEs)
- Phase: P9 · Size: M · Status: todo
- Depends: DRV-ACPI-01, IRQ-03
- Files: kernel/drivers/acpi/events.c

**Goal.** SCI interrupt, fixed events (power button), general purpose
events with `_Lxx/_Exx` methods, Notify() delivery to drivers; power
button → "Shut down / Restart / Sleep" dialog.
**Done when.** QEMU `system_powerdown` shows the dialog on x86 and ARM.
**Tests.** QEMU test.
**Pitfalls.** Edge vs level GPEs.
**Refs.** ACPI spec ch. 4–5.

### DRV-ACPI-03 — Battery and AC adapter
- Phase: P9 · Size: S · Status: todo
- Depends: DRV-ACPI-01
- Files: kernel/drivers/acpi/battery.c

**Goal.** `_BIF/_BIX/_BST` battery info, `_PSR` AC state, events on
change → powerd (SVC-60) → menu bar battery icon.
**Done when.** QEMU `-device` battery (or real laptop) shows percentage.
**Tests.** Host test with fake namespace values.
**Pitfalls.** Units (mWh vs mAh).
**Refs.** ACPI spec §10.

### DRV-ACPI-04 — Thermal zones and fans
- Phase: P9 · Size: S · Status: todo
- Depends: DRV-ACPI-01
- Files: kernel/drivers/acpi/thermal.c

**Goal.** Read temperatures, trip points, passive cooling (CPU frequency
cap via SVC-60), critical shutdown.
**Done when.** Temperatures visible in System Information on hardware.
**Tests.** Host test.
**Pitfalls.** Never ignore critical trip points.
**Refs.** ACPI spec §11.

### DRV-ACPI-05 — Sleep states (S3, s2idle)
- Phase: P9 · Size: L · Status: todo
- Depends: DRV-ACPI-02, DRV-CORE-03, SMP-01
- Files: kernel/drivers/acpi/sleep.c, arch resume code

**Goal.** Suspend to RAM: save CPU state, park secondary CPUs, firmware
sleep (`_PTS`, SLP_TYP), resume vector, restore; s2idle on platforms
without S3.
**Done when.** Lid close/open on a real laptop sleeps and resumes.
**Tests.** QEMU x86 S3 test; hardware record.
**Pitfalls.** Device state loss; timekeeping across sleep (TIM-05).
**Refs.** ACPI spec ch. 16.

### DRV-FW-01 — UEFI runtime services
- Phase: P5 · Size: S · Status: todo
- Depends: MEM-05
- Files: kernel/drivers/efi/runtime.c

**Goal.** Call `GetVariable/SetVariable` (boot order, secure boot state),
`ResetSystem`, `GetTime` with proper virtual mapping
(`SetVirtualAddressMap`).
**Done when.** Settings > Startup Disk can change the next boot entry.
**Tests.** QEMU OVMF test.
**Pitfalls.** Buggy firmware: call with interrupts off, on one CPU.
**Refs.** UEFI spec ch. 8.

---

## Virtio (VZ and QEMU)

### DRV-VIRTIO-01 — Transport v2: MSI-X, event index, multiqueue
- Phase: P2 · Size: M · Status: todo
- Depends: DRV-CORE-01, DRV-PCI-02, MEM-18
- Files: kernel/src/arch/aarch64/virtio.c → kernel/drivers/virtio/

**Context.** Queue depth negotiation is fixed (AGENTS.md §4.6); drivers
poll or use one interrupt.
**Goal.** Shared transport for both arches (mmio + pci), packed rings
optional, `VIRTIO_F_EVENT_IDX` to cut notifications, per-queue interrupts,
indirect descriptors, proper reset on errors.
**Done when.** All virtio drivers use it; interrupts per second at idle
drop to near zero.
**Tests.** `test_virtqueue.c`, `test_virtio_pci.c` extended; smoke.
**Pitfalls.** Keep negotiated-depth masks (skill:
virtio-negotiated-ring-depth).
**Refs.** Virtio spec 1.2.

### DRV-VIRTIO-02 — virtio-blk asynchronous multi-request
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-VIRTIO-01, BLK-01
- Files: kernel/src/arch/aarch64/virtio_blk.c → kernel/drivers/virtio/blk.c

**Context.** One synchronous request at a time.
**Goal.** Up to queue-depth requests in flight, completion interrupts into
the block layer, FLUSH, DISCARD/WRITE_ZEROES, multiqueue, read-only flag.
**Done when.** Sequential read ≥ 1 GB/s and 4K random read ≥ 50k IOPS
under VZ (measured).
**Tests.** Block layer test suite; disk benchmark program.
**Pitfalls.** Barriers/flush ordering for filesystem consistency.
**Refs.** Virtio spec §5.2.

### DRV-VIRTIO-03 — virtio-net performance and features
- Phase: P5 · Size: M · Status: todo
- Depends: DRV-VIRTIO-01, NET-01
- Files: kernel/src/arch/aarch64/virtio_net.c → kernel/drivers/virtio/net.c

**Goal.** Pre-posted RX buffers pool, checksum offload, TSO/GSO, merged
RX buffers, link status, multiqueue, interrupt coalescing.
**Done when.** iperf-like test reaches ≥ 2 Gbit/s to the host under VZ.
**Tests.** Network benchmark (NET-90).
**Pitfalls.** VZ rejects MAC-only feature combos (skill:
apple-vz-custom-os-bringup).
**Refs.** Virtio spec §5.1.

### DRV-VIRTIO-04 — virtio-gpu display events (live resize, multi-scanout)
- Phase: P2 · Size: M · Status: todo
- Depends: DRV-VIRTIO-01
- Files: kernel/src/arch/aarch64/virtio_gpu.c

**Context.** Fixed 2880×1800 scanout; window resize of the host app or
QEMU window does not change the guest mode (TODO.md backlog).
**Goal.** Handle `VIRTIO_GPU_EVENT_DISPLAY`, re-read display info, resize
the scanout resource, notify WindowServer of the new size and scale;
multiple scanouts for multiple displays.
**Done when.** Resizing `ojjyOS.app`'s window (VZ) or QEMU's window
re-lays out the desktop at the new resolution within 200 ms.
**Tests.** QEMU test changing `xres/yres` via monitor; VZ test resizing the
window.
**Pitfalls.** VZ needs `automaticallyReconfiguresDisplay` set in the app
(`vmapp/main.swift`).
**Refs.** Virtio spec §5.7.

### DRV-VIRTIO-05 — virtio-input extended
- Phase: P3 · Size: S · Status: todo
- Depends: DRV-VIRTIO-01, DRV-IN-01
- Files: kernel/src/arch/aarch64/virtio_input.c

**Goal.** Keyboard LEDs, multi-touch (EV_ABS MT) for touch-capable hosts,
horizontal scroll, precise scroll deltas, device names in Settings.
**Done when.** Two-finger horizontal scroll works in Finder and ojjyWeb.
**Tests.** QMP input injection test.
**Pitfalls.** —
**Refs.** Virtio spec §5.8, Linux input event codes (numbers only).

### DRV-VIRTIO-06 — virtio-sound
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-VIRTIO-01, AUD-01
- Files: kernel/drivers/virtio/sound.c

**Goal.** PCM output and input streams (48 kHz stereo S16/F32), jack
info, volume via mixer; VZ `VZVirtioSoundDeviceConfiguration` enabled in
the app.
**Done when.** The startup chime plays under VZ and QEMU
(`-device virtio-sound-pci`); microphone recording works under VZ.
**Tests.** Live test capturing host audio output (QEMU wav backend).
**Pitfalls.** Period sizes; xruns under load (SCH-08).
**Refs.** Virtio spec §5.14.

### DRV-VIRTIO-07 — virtio-console multiport and host channels
- Phase: P2 · Size: S · Status: todo
- Depends: DRV-VIRTIO-01
- Files: kernel/src/arch/aarch64/virtio_console.c

**Goal.** Multiport: port 0 log, port 1 debugger (KDB-03), port 2 host
control (clipboard/notifications bridge, replacing QMP-only tests).
**Done when.** Log and debugger work simultaneously under VZ.
**Tests.** VZ test.
**Pitfalls.** VZ console offers 4 queues without MULTIPORT (AGENTS.md) —
detect features before assuming.
**Refs.** Virtio spec §5.3.

### DRV-VIRTIO-08 — Debug channel binding
- Phase: P2 · Size: S · Status: todo
- Depends: DRV-VIRTIO-07
- Files: kernel/core/gdbstub.c glue

**Goal.** Expose a byte-stream device usable by the GDB stub and a
`kdebug` console, independent of the main log.
**Done when.** KDB-03 runs over it.
**Tests.** QEMU test.
**Pitfalls.** —
**Refs.** —

### DRV-VIRTIO-09 — Shared folders with the Mac (virtio-fs)
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-VIRTIO-01, FS-01
- Files: kernel/drivers/virtio/fs.c, kernel/fs/virtiofs.c, vmapp/main.swift

**Goal.** Mount a Mac folder (chosen in `ojjyOS.app` preferences) at
`/Volumes/Mac` using FUSE-over-virtio requests (VZ
`VZVirtioFileSystemDeviceConfiguration`); read/write files, directories.
**Done when.** Finder shows the Mac's shared folder; copying a 100 MB file
works both ways.
**Tests.** VZ test with a temporary host folder; QEMU with virtiofsd.
**Pitfalls.** FUSE protocol versions; case-insensitive host filesystems.
**Refs.** Virtio spec §5.11; FUSE protocol (public kernel header docs).

### DRV-VIRTIO-10 — Memory balloon
- Phase: P5 · Size: S · Status: todo
- Depends: DRV-VIRTIO-01, MEM-14
- Files: kernel/drivers/virtio/balloon.c

**Goal.** Return free memory to the Mac host (VZ traditional balloon),
free page reporting.
**Done when.** Host-side memory use of the VM drops after closing apps.
**Tests.** VZ test reading the balloon target.
**Pitfalls.** Never inflate below the working set.
**Refs.** Virtio spec §5.5.

### DRV-VIRTIO-11 — vsock host channel (clipboard, drag-and-drop, time)
- Phase: P5 · Size: M · Status: todo
- Depends: DRV-VIRTIO-01, NET-10
- Files: kernel/drivers/virtio/vsock.c, vmapp/main.swift, services/hostbridged

**Goal.** Socket transport between guest and `ojjyOS.app`: shared
clipboard (text + images) both ways, host time, dragging files from macOS
into the VM window.
**Done when.** Copy on the Mac → paste in TextEdit inside ojjyOS works.
**Tests.** VZ test with scripted pasteboard.
**Pitfalls.** Make sharing opt-in (privacy) in Settings.
**Refs.** Virtio spec §5.10.

---

## USB

### DRV-USB-01 — USB core
- Phase: P3 · Size: L · Status: todo
- Depends: DRV-CORE-01, MEM-18
- Files: kernel/src/arch/aarch64/xhci.c, usb_hid.c → kernel/drivers/usb/

**Context.** xHCI enumerates directly attached HID devices only (AGENTS.md).
**Goal.** Host-controller-independent core: device objects, descriptor
parsing (device, config, interface, endpoint, string, BOS), address
assignment, configuration selection, asynchronous transfer requests (URBs)
for control/bulk/interrupt/isochronous, class driver matching,
`lsusb`-style listing.
**Done when.** HID moves to the core; device list shows names from string
descriptors.
**Tests.** Host tests with fake devices (DRV-CORE-05) incl. malformed
descriptors.
**Pitfalls.** Descriptor lengths lie; strings are UTF-16LE.
**Refs.** USB 3.2 spec ch. 9.

### DRV-USB-02 — xHCI complete
- Phase: P3 · Size: L · Status: todo
- Depends: DRV-USB-01, IRQ-02
- Files: kernel/drivers/usb/xhci/

**Goal.** Event-ring interrupts (no polling), port status change handling
(hot-plug), all speeds, multiple devices, bulk and isochronous endpoints,
streams (for UAS), error recovery (stall, babble), controller reset, x86
PCI xHCI (QEMU `qemu-xhci`, real PCs).
**Done when.** Plug/unplug of keyboard, mouse, disk, audio device in QEMU
works repeatedly on both arches; VZ input unchanged.
**Tests.** QEMU hot-plug test script; `test_xhci.c` extended.
**Pitfalls.** Two VZ traps already known (MaxSlotsEn before DCBAAP; no
CH-chaining of setup/data/status TDs) — keep them.
**Refs.** xHCI spec 1.2.

### DRV-USB-03 — USB hubs
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-USB-02
- Files: kernel/drivers/usb/hub.c

**Goal.** Hub class driver: port power, reset, status change polling via
interrupt endpoint, TT for low/full-speed devices behind high-speed hubs,
xHCI route strings.
**Done when.** Devices behind a 2-level hub chain in QEMU work.
**Tests.** QEMU `usb-hub` test.
**Pitfalls.** Over-current events; power budgets.
**Refs.** USB 2.0 spec ch. 11.

### DRV-USB-04 — HID class (full report descriptor support)
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-USB-01, DRV-IN-01
- Files: kernel/drivers/usb/hid.c, kernel/drivers/input/hidparse.c

**Context.** Boot keyboard + descriptor-driven absolute pointer exist.
**Goal.** Complete report descriptor parser (collections, usages, report
IDs, arrays/variables, physical units), mapping to input events for
keyboards (all keys incl. media keys), mice (relative + high-res wheel),
touchpads, pens, gamepads, consumer control; output reports (LEDs).
**Done when.** QEMU `usb-kbd`, `usb-mouse`, `usb-tablet` and a real
keyboard/mouse/gamepad (hardware record) work.
**Tests.** Host tests with recorded descriptors from many devices + fuzz.
**Pitfalls.** Vendor quirks table (`hid_quirks.c`).
**Refs.** USB HID 1.11, HID Usage Tables 1.4.

### DRV-USB-05 — USB mass storage (BOT and UAS)
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-USB-02, BLK-01
- Files: kernel/drivers/usb/storage.c

**Goal.** Bulk-only transport with SCSI commands (INQUIRY, READ
CAPACITY, READ/WRITE 10/16, TEST UNIT READY, REQUEST SENSE), UAS with
streams when available; block devices for the block layer.
**Done when.** A FAT32/exFAT USB stick mounts in Finder and files copy at
≥ 30 MB/s in QEMU.
**Tests.** QEMU `usb-storage` test; host SCSI command builder tests.
**Pitfalls.** Phase errors, stalled endpoints, cheap sticks with lying
capacity.
**Refs.** USB Mass Storage BOT 1.0; UAS spec; SCSI SBC-3.

### DRV-USB-06 — USB audio class
- Phase: P6 · Size: M · Status: todo
- Depends: DRV-USB-02, AUD-01
- Files: kernel/drivers/usb/audio.c

**Goal.** UAC1 and UAC2 playback/capture via isochronous endpoints, sample
rate selection, volume/mute units.
**Done when.** QEMU `usb-audio` plays; a real USB headset works (record).
**Tests.** QEMU test.
**Pitfalls.** Isochronous scheduling and clock drift.
**Refs.** USB Audio Class 1.0/2.0.

### DRV-USB-07 — USB video class (webcams)
- Phase: P6 · Size: M · Status: todo
- Depends: DRV-USB-02, MED-30
- Files: kernel/drivers/usb/video.c

**Goal.** UVC MJPEG/YUY2 streaming, format negotiation, controls
(exposure, focus) → camera service.
**Done when.** A real webcam shows video in the Camera app (record).
**Tests.** Host test of descriptor parsing; hardware record.
**Pitfalls.** Bandwidth; payload headers.
**Refs.** UVC 1.5.

### DRV-USB-08 — USB networking (CDC-ECM/NCM, RNDIS)
- Phase: P6 · Size: S · Status: todo
- Depends: DRV-USB-02, NET-01
- Files: kernel/drivers/usb/cdc_net.c

**Goal.** USB Ethernet adapters and phone tethering as network interfaces.
**Done when.** QEMU `usb-net` gets a DHCP lease.
**Tests.** QEMU test.
**Pitfalls.** RNDIS is Microsoft-specific: follow public spec.
**Refs.** USB CDC 1.2 ECM/NCM.

### DRV-USB-09 — USB serial (CDC-ACM, FTDI, CP210x)
- Phase: P7 · Size: S · Status: todo
- Depends: DRV-USB-02, DRV-TTY-01
- Files: kernel/drivers/usb/serial.c

**Goal.** Serial adapters and microcontroller boards appear as
`/dev/tty.usbserial*` for the terminal and tools.
**Done when.** QEMU `usb-serial` loopback works.
**Tests.** QEMU test.
**Pitfalls.** Vendor protocols differ per chip.
**Refs.** CDC-ACM spec.

### DRV-USB-10 — EHCI for older PCs
- Phase: P9 · Size: M · Status: todo
- Depends: DRV-USB-01
- Files: kernel/drivers/usb/ehci.c

**Goal.** USB 2.0 controller support for pre-2012 PCs (with companion
controllers ignored or OHCI/UHCI handoff).
**Done when.** QEMU `usb-ehci` with keyboard and storage works.
**Tests.** QEMU test.
**Pitfalls.** BIOS legacy handoff (USBLEGSUP).
**Refs.** EHCI spec 1.0.

### DRV-USB-11 — Gamepads
- Phase: P6 · Size: S · Status: todo
- Depends: DRV-USB-04, DRV-IN-06
- Files: kernel/drivers/usb/gamepad.c

**Goal.** HID gamepads plus Xbox-protocol controllers (public protocol
documentation), rumble.
**Done when.** A controller plays the bundled games (record).
**Tests.** Host tests with recorded reports.
**Pitfalls.** —
**Refs.** —

---

## Storage controllers

### DRV-STOR-01 — NVMe
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-PCI-02, BLK-01, MEM-18
- Files: kernel/drivers/storage/nvme.c

**Goal.** Admin queue, identify controller/namespaces, I/O queue pairs per
CPU, PRP lists, interrupts per queue, flush, TRIM (DSM), SMART log for
Disk Utility, timeouts with controller reset.
**Done when.** QEMU `-device nvme` disk usable as the system disk with
≥ 500 MB/s reads under HVF.
**Tests.** QEMU test; host test of PRP list builder.
**Pitfalls.** Doorbell stride; completion phase bit.
**Refs.** NVM Express Base Spec 2.0.

### DRV-STOR-02 — AHCI (SATA)
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-PCI-01, BLK-01
- Files: kernel/drivers/storage/ahci.c

**Goal.** HBA reset, port detection, command list/FIS, NCQ, ATAPI
detection; replaces PIO ATA on x86 QEMU (`-device ahci`).
**Done when.** x86 boots from an AHCI disk.
**Tests.** QEMU x86 test.
**Pitfalls.** BIOS/OS handoff bit; staggered spin-up.
**Refs.** AHCI 1.3.1.

### DRV-STOR-03 — Legacy ATA PIO cleanup
- Phase: P3 · Size: S · Status: todo
- Depends: BLK-01
- Files: kernel/src/drivers/ata.c

**Goal.** Port the existing PIO driver to the block layer as a fallback
(with IRQ-driven completion), or delete it once AHCI is default.
**Done when.** Decision logged; old QEMU IDE configs still boot or are
documented unsupported.
**Tests.** QEMU x86 IDE test.
**Pitfalls.** —
**Refs.** ATA-8 spec.

### DRV-STOR-04 — SD/MMC (SDHCI)
- Phase: P9 · Size: M · Status: todo
- Depends: BLK-01, BOOT-06
- Files: kernel/drivers/storage/sdhci.c

**Goal.** SD cards and eMMC on Raspberry Pi and laptops: card init,
4-bit/8-bit bus, high speed, ADMA2.
**Done when.** Raspberry Pi boots its system from SD (HW-20).
**Tests.** QEMU `sdhci-pci` test.
**Pitfalls.** Voltage switching; card quirks.
**Refs.** SD Host Controller Simplified Spec.

### DRV-STOR-05 — Optical media (ATAPI / USB)
- Phase: P7 · Size: S · Status: todo
- Depends: DRV-STOR-02, FS-24
- Files: kernel/drivers/storage/atapi.c

**Goal.** Read CDs/DVDs and ISO images (SCSI MMC commands), eject.
**Done when.** A QEMU CD-ROM with an ISO mounts in Finder.
**Tests.** QEMU test.
**Pitfalls.** —
**Refs.** SCSI MMC-6.

---

## Display

### DRV-DISP-01 — Display core (modes, EDID, hot-plug)
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-CORE-01, WS-01
- Files: kernel/drivers/display/core.c

**Goal.** Abstract displays/connectors/modes, EDID parsing (names,
preferred mode, physical size → scale factor), hot-plug events to
WindowServer, gamma/colour tables, brightness API.
**Done when.** Settings > Displays lists displays with real names and
modes for virtio-gpu and GOP.
**Tests.** Host tests with captured EDIDs + fuzz.
**Pitfalls.** EDID checksums and extension blocks (CTA-861).
**Refs.** VESA E-EDID 1.4.

### DRV-DISP-02 — UEFI GOP framebuffer driver
- Phase: P2 · Size: S · Status: todo
- Depends: DRV-CORE-01
- Files: kernel/src/framebuffer.c

**Goal.** The firmware framebuffer as a display-core device (single mode,
no hot-plug), used on PCs before a native driver exists.
**Done when.** x86 desktop runs on it via the display core.
**Tests.** x86 smoke.
**Pitfalls.** Pixel formats (BGR vs RGB, bitmask modes).
**Refs.** UEFI GOP.

### DRV-DISP-03 — QEMU std-VGA / Bochs display (x86 mode setting)
- Phase: P3 · Size: S · Status: todo
- Depends: DRV-DISP-01, DRV-PCI-01
- Files: kernel/drivers/display/bochs.c

**Goal.** Change resolution at runtime on x86 QEMU (DISPI registers),
so x86 gets live resize too.
**Done when.** Settings > Displays changes x86 resolution.
**Tests.** QEMU x86 test.
**Pitfalls.** —
**Refs.** Bochs VBE extensions docs.

### DRV-DISP-04 — Intel integrated graphics (display only)
- Phase: P9 · Size: L · Status: todo
- Depends: DRV-DISP-01, DRV-PCI-02
- Files: kernel/drivers/display/intel/

**Goal.** Native mode setting for Intel Gen9–Gen12 iGPUs: read VBT,
outputs (eDP, HDMI, DP), pipes/planes, link training, panel power,
backlight, hardware cursor plane. Rendering stays CPU/virtio until GPU-40.
**Done when.** A real Intel laptop runs at native resolution with
brightness keys working (record).
**Tests.** Hardware record; host tests of link-training state machine.
**Pitfalls.** Huge hardware surface: follow Intel's public PRMs only.
**Refs.** Intel Graphics PRMs (public).

### DRV-DISP-05 — Backlight control
- Phase: P9 · Size: S · Status: todo
- Depends: DRV-DISP-01, DRV-ACPI-01
- Files: kernel/drivers/display/backlight.c

**Goal.** ACPI `_BCL/_BCM` or native PWM brightness, brightness keys,
Control Center slider.
**Done when.** Brightness slider changes panel brightness on a laptop.
**Tests.** Hardware record.
**Pitfalls.** —
**Refs.** ACPI spec Appendix B.

---

## Input

### DRV-IN-01 — Input core v2 (events, layouts, gestures)
- Phase: P2 · Size: M · Status: todo
- Depends: DRV-CORE-01
- Files: kernel/src/drivers/input.c → kernel/drivers/input/

**Context.** Shared translation and repeat policy exist (AGENTS.md §4.6).
**Goal.** Device-independent event stream (key down/up with scancode +
keycode, pointer abs/rel, scroll with precise deltas, touch contacts,
pen, gamepad), per-device state, delivery to WindowServer through a port
(user-space layouts in I18N-02), timestamps in ns.
**Done when.** WindowServer receives all input through the new stream;
repeat policy tests still pass.
**Tests.** `test_input.c` extended; pointer tests.
**Pitfalls.** Keep latency (no extra copies); event coalescing for motion
only.
**Refs.** —

### DRV-IN-02 — PS/2 keyboard and mouse hardening
- Phase: P3 · Size: S · Status: todo
- Depends: DRV-IN-01, DRV-ACPI-01
- Files: kernel/src/drivers/ps2_*.c

**Goal.** i8042 detection via ACPI (`PNP0303/PNP0F13`), controller self
test, scancode set 2, mouse IntelliMouse wheel/5 buttons, touchpad
fallback mode.
**Done when.** Real PC laptop keyboards work without USB.
**Tests.** QEMU x86 tests.
**Pitfalls.** Some laptops lack an i8042.
**Refs.** —

### DRV-IN-03 — I2C controllers and I2C-HID touchpads
- Phase: P9 · Size: L · Status: todo
- Depends: DRV-ACPI-01, DRV-USB-04
- Files: kernel/drivers/i2c/designware.c, kernel/drivers/input/i2c_hid.c

**Goal.** Synopsys DesignWare I2C (most Intel/AMD laptops), ACPI GPIO
interrupts, HID-over-I2C protocol reusing the HID parser.
**Done when.** A real laptop touchpad moves the pointer (record).
**Tests.** Host tests of the HID-over-I2C state machine.
**Pitfalls.** GPIO interrupt controllers (Intel pinctrl) needed too.
**Refs.** HID over I2C protocol spec (Microsoft, public).

### DRV-IN-04 — Touchpad gestures
- Phase: P4 · Size: M · Status: todo
- Depends: DRV-IN-01, WS-10
- Files: services/windowserver/gestures.c

**Goal.** Multi-finger recognizer: two-finger scroll with momentum, pinch,
three-finger swipe (Spaces/Mission Control), four-finger app switch,
tap-to-click, palm rejection; settings in Trackpad pane.
**Done when.** Gestures work with a precision touchpad (hardware) and with
simulated multi-touch in QEMU.
**Tests.** Recorded touch traces replayed in host tests.
**Pitfalls.** Momentum physics must feel macOS-like (DS-12 motion).
**Refs.** —

### DRV-IN-05 — Touchscreens and pens
- Phase: P9 · Size: M · Status: todo
- Depends: DRV-USB-04, DRV-IN-01
- Files: kernel/drivers/input/touch.c

**Goal.** HID digitizers (touch and pen with pressure/tilt), on-screen
keyboard trigger (A11Y-08).
**Done when.** QEMU `usb-tablet` multi-touch simulation and a real touch
laptop work.
**Tests.** QEMU test.
**Pitfalls.** —
**Refs.** HID Usage Tables (Digitizers page).

### DRV-IN-06 — Game controller API
- Phase: P6 · Size: S · Status: todo
- Depends: DRV-IN-01
- Files: lib/libojjy/gamecontroller.c

**Goal.** Unified controller API (buttons, axes, triggers, rumble) with a
standard layout, used by games and the Windows/Linux compat layers.
**Done when.** Bundled games accept controllers.
**Tests.** Host tests.
**Pitfalls.** —
**Refs.** —

---

## Audio hardware

### DRV-AUD-01 — Intel High Definition Audio
- Phase: P3 · Size: L · Status: todo
- Depends: DRV-PCI-02, AUD-01
- Files: kernel/drivers/audio/hda/

**Goal.** HDA controller (CORB/RIRB, streams, BDL), codec enumeration,
widget graph parsing to find output/input paths (speaker, headphone jack
sensing, mic), volume amplifiers.
**Done when.** QEMU `intel-hda` + `hda-duplex` plays and records; one real
PC plays sound (record).
**Tests.** QEMU test; host tests of widget path finding with captured
codec dumps.
**Pitfalls.** Codec quirks (pin configs); unsolicited responses for jacks.
**Refs.** Intel HDA spec 1.0a.

### DRV-AUD-02 — HDMI/DisplayPort audio
- Phase: P9 · Size: S · Status: todo
- Depends: DRV-AUD-01, DRV-DISP-04
- Files: kernel/drivers/audio/hda/hdmi.c

**Goal.** Audio over HDMI/DP via the HDA HDMI codec with ELD parsing.
**Done when.** Sound plays through a monitor's speakers (record).
**Tests.** Hardware record.
**Pitfalls.** —
**Refs.** —

---

## Network hardware

### DRV-NET-01 — Intel e1000 / e1000e
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-PCI-02, NET-01
- Files: kernel/drivers/net/e1000.c

**Goal.** QEMU's default x86 NIC and many real Intel NICs: descriptor
rings, interrupts, link status, checksum offload.
**Done when.** x86 QEMU (default `-nic`) gets DHCP and loads web pages.
**Tests.** QEMU x86 network test.
**Pitfalls.** e1000 vs e1000e register differences.
**Refs.** Intel 8254x and 82574 datasheets.

### DRV-NET-02 — Realtek RTL8169/8125 Ethernet
- Phase: P9 · Size: M · Status: todo
- Depends: DRV-PCI-02, NET-01
- Files: kernel/drivers/net/r8169.c

**Goal.** Most common desktop Ethernet chips.
**Done when.** A real PC with Realtek Ethernet browses the web (record).
**Tests.** QEMU `rtl8139` variant for basics; hardware record.
**Pitfalls.** Many chip revisions; PHY init quirks from public docs only.
**Refs.** Realtek datasheets (public).

### DRV-NET-03 — Wi-Fi (Intel iwlwifi-class, split before work)
- Phase: P9 · Size: XL · Status: todo
- Depends: DRV-CORE-04, DRV-PCI-02, NET-40
- Files: kernel/drivers/net/wifi/

**Context.** No Wi-Fi exists in VMs; the Wi-Fi tile controls the virtual
Ethernet (TODO.md). Real laptops need it.
**Goal.** Driver for a common Intel Wi-Fi family with firmware loading,
scanning, WPA2/WPA3-Personal (with NET-40 supplicant), power save.
**Done when.** A real laptop joins a WPA2 network and browses (record).
**Tests.** Hardware record; host tests of 802.11 frame parsing.
**Pitfalls.** Enormous; firmware API is under-documented — split into
cards (PCIe transport, firmware boot, command interface, scan, connect,
data path, power) and research before committing to a chip family.
**Refs.** IEEE 802.11-2020.

### DRV-NET-04 — USB Wi-Fi dongle (alternative path)
- Phase: P9 · Size: L · Status: todo
- Depends: DRV-USB-02, NET-40
- Files: kernel/drivers/net/wifi/usb_*.c

**Goal.** One well-documented USB Wi-Fi chipset (e.g. a MediaTek or
Realtek family with public register docs) as a cheaper Wi-Fi path that
also works in QEMU via USB passthrough.
**Done when.** Dongle joins WPA2 networks (record).
**Tests.** Hardware record.
**Pitfalls.** Choose a chip with open documentation first.
**Refs.** —

---

## Bluetooth

### DRV-BT-01 — Bluetooth HCI transport and core
- Phase: P9 · Size: L · Status: todo
- Depends: DRV-USB-02
- Files: kernel/drivers/bluetooth/hci.c

**Goal.** HCI over USB (and UART later): reset, local version, LE and
BR/EDR scanning, connections, event handling; `bluetoothd` service owns
the stack above HCI (NET-60…).
**Done when.** QEMU passthrough of a USB Bluetooth dongle scans nearby
devices; Settings > Bluetooth lists them.
**Tests.** Host tests of HCI event parsing with recorded traces.
**Pitfalls.** Vendor firmware patches (Intel/Realtek) via DRV-CORE-04.
**Refs.** Bluetooth Core Spec 5.4 Vol 4.

---

## Character devices and misc

### DRV-TTY-01 — TTY and pseudo-terminal layer
- Phase: P2 · Size: M · Status: todo
- Depends: SYS-07, IPC-08
- Files: kernel/drivers/tty/

**Goal.** PTY pairs (`/dev/ptmx`), line discipline (canonical mode,
echo, erase/kill, signals on Ctrl-C/Z/\), termios, window size
(`TIOCGWINSZ`), serial/virtio console as TTYs.
**Done when.** Terminal.app runs `ojsh` on a PTY; full-screen programs
(an editor) work.
**Tests.** Host tests of the line discipline; live shell test.
**Pitfalls.** Many termios corner cases — needed by Linux programs too.
**Refs.** POSIX General Terminal Interface.

### DRV-RNG-01 — Hardware randomness sources
- Phase: P2 · Size: S · Status: todo
- Depends: DRV-CORE-01
- Files: kernel/src/random.c

**Goal.** ARM `RNDR` (FEAT_RNG), x86 `RDRAND/RDSEED`, TPM RNG, virtio-rng
— all mixed into the ChaCha20 DRBG with health tests.
**Done when.** Boot log lists entropy sources; DRBG reseeds periodically.
**Tests.** Host tests of mixing; live check.
**Pitfalls.** Never trust a single source exclusively.
**Refs.** NIST SP 800-90B (concepts).

### DRV-TPM-01 — TPM 2.0
- Phase: P5 · Size: M · Status: todo
- Depends: DRV-ACPI-01, DRV-CORE-01
- Files: kernel/drivers/security/tpm.c

**Goal.** TPM CRB/TIS interfaces, PCR extend for measured boot, sealed
keys for disk encryption unlock (SEC-30), RNG.
**Done when.** QEMU swtpm: PCRs extended at boot and a key unseals.
**Tests.** QEMU test with swtpm.
**Pitfalls.** Command marshalling must be bounds-checked.
**Refs.** TCG TPM 2.0 Library spec.

### DRV-RTC-01 — Real-time clock drivers unified
- Phase: P2 · Size: S · Status: todo
- Depends: DRV-CORE-01
- Files: kernel/src/arch/aarch64/pl031.c, kernel/src/drivers/rtc.c

**Goal.** PL031, CMOS RTC, EFI runtime `GetTime`, and VZ (no RTC — host
time via vsock/NTP) behind one API; write support.
**Done when.** Wall clock correct on every platform at boot.
**Tests.** Live tests per platform.
**Pitfalls.** CMOS BCD vs binary, century register.
**Refs.** —

### DRV-GPIO-01 — GPIO, I2C and SPI for boards
- Phase: P9 · Size: M · Status: todo
- Depends: BOOT-06
- Files: kernel/drivers/gpio/, i2c/, spi/

**Goal.** Raspberry Pi GPIO/I2C/SPI drivers with a user API for makers
(`/dev/gpiochip0`-like), used by HW-20.
**Done when.** An LED blinks from a shell command on a Pi (record).
**Tests.** Hardware record.
**Pitfalls.** Pin multiplexing.
**Refs.** BCM2712 peripherals docs (public).

### DRV-SENS-01 — Laptop sensors
- Phase: P9 · Size: S · Status: todo
- Depends: DRV-IN-03
- Files: kernel/drivers/sensors/

**Goal.** Ambient light (auto-brightness), accelerometer (screen rotation
on convertibles), lid angle via HID sensor hub.
**Done when.** Auto-brightness works on a supported laptop (record).
**Tests.** Hardware record.
**Pitfalls.** —
**Refs.** HID Sensor Usages.
