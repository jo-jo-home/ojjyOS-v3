# 10 — Kernel core

Boot, memory, threads, scheduling, SMP, interrupts, time, system calls,
processes, executable loading, IPC and kernel debugging. This is Jonas's
"it sets up everything else we build upon" — most later cards depend on it.

Architecture decisions used here: D-001 (hybrid), D-003 (address space),
D-004 (execution model), D-005 (ports), D-006 (syscalls), D-007 (ELF).

Conventions for this file:
- New kernel code goes into the target directories of D-018
  (`kernel/core`, `kernel/mm`, …) — create them as needed and add them to
  `kernel/Makefile` for both arches.
- Arch-specific pieces live under `kernel/src/arch/aarch64/` and a new
  `kernel/src/arch/x86_64/` (the x86 files currently at the top of
  `kernel/src` move there in `QA-21`).
- Every data structure that can be tested on the host (allocators, trees,
  queues, ELF parsing, scheduler policy) is written so `tests/test_*.c`
  can compile it with a tiny shim, as `tests/test_user.c` does today.

---

## Boot

### BOOT-01 — Boot protocol v2 (versioned BootInfo)
- Phase: P1 · Size: M · Status: todo
- Depends: —
- Files: boot/ojjy-boot.c, kernel/src/main.c (BootInfo struct), new kernel/core/bootinfo.h

**Context.** `BootInfo` today carries a memory map and one framebuffer; it
has no version field, no ACPI/DTB pointers, no command line, no initrd and
no random seed. Every later boot feature (SMP, ACPI, initrd, secure boot,
A/B slots) needs more fields.
**Goal.** A versioned, self-describing boot handoff both arches use.
**Steps.**
1. Define `BootInfoV2 { magic, version, size, flags, memmap{ptr,count,desc_size},
   framebuffers[4], acpi_rsdp, dtb, cmdline[512], initrd{base,size},
   kernel_phys, kernel_size, boot_slot, secure_boot_state, rng_seed[32] }`.
2. Bootloader: fill RSDP from the UEFI configuration table (ACPI 2.0 GUID),
   DTB from the DTB GUID if present, `EFI_RNG_PROTOCOL` seed when available.
3. Kernel: validate magic/version/size before use; reject unknown major
   versions with a clear panic message on the framebuffer and serial.
4. Keep a compatibility shim reading v1 until both builds use v2.
**Done when.**
- Both arches boot with v2; serial prints `[BOOT] BootInfo v2 rsdp=… dtb=…
  cmdline='…'`.
- The RNG seed is mixed into `random.c` before first use.
**Tests.** Host test of the validator (bad magic/size/version rejected);
`qemu_smoke.py` on TCG/HVF/x86; `vz_boot.py`.
**Pitfalls.** UEFI memory map descriptors are `desc_size` apart, not
`sizeof(struct)`; keep the map after `ExitBootServices` (call GetMemoryMap
again if the key changes).
**Refs.** UEFI spec §7.2 (memory map), §4.6 (configuration tables).

### BOOT-02 — Kernel command line and boot options
- Phase: P1 · Size: S · Status: todo
- Depends: BOOT-01
- Files: boot/ojjy-boot.c, new kernel/core/cmdline.c

**Context.** Debug switches are compile-time today.
**Goal.** `key=value` options read from `\EFI\ojjy\cmdline.txt` on the ESP
(and the boot menu), parsed by the kernel.
**Steps.**
1. Bootloader reads `cmdline.txt` (max 511 bytes) if present.
2. Kernel parser: `cmdline_get(key)`, `cmdline_bool(key, default)`.
3. Initial options: `log=debug|info|warn`, `safe=1` (no autostart apps,
   software rendering), `single=1` (console shell), `smp=N`, `nogpu=1`.
**Done when.** Options change behaviour and are logged at boot.
**Tests.** Host test of the parser (quotes, duplicates, overlong lines).
**Pitfalls.** Never trust the file: bound length, ASCII only.
**Refs.** —

### BOOT-03 — Initial ramdisk (initrd)
- Phase: P2 · Size: M · Status: todo
- Depends: BOOT-01, FS-02
- Files: boot/ojjy-boot.c, scripts/mkimg.sh, new kernel/fs/initrd.c

**Context.** The system image is mounted from a partition by the kernel's
own drivers; when drivers move or a new storage driver is needed early,
an initrd provides the first user programs (`ojjyd`, shell) and recovery.
**Goal.** The bootloader loads `\EFI\ojjy\initrd.ojfs` into memory; the
kernel mounts it read-only at `/System/Boot` before the real root.
**Steps.**
1. Reuse the OJFS format (tools/mkojfs) for the initrd.
2. Bootloader allocates `EfiLoaderData` pages, records base/size in BootInfo.
3. Kernel reserves those pages in the PMM, mounts, and frees them after the
   root filesystem is up (if not needed).
**Done when.** `ojjyd` (once it exists) starts from the initrd; recovery
shell works with the data partition missing.
**Tests.** QEMU test booting with a corrupted data partition reaches the
recovery shell.
**Pitfalls.** Keep the initrd small (< 8 MiB) for boot time.
**Refs.** D-013.

### BOOT-04 — Boot menu (slots, recovery, safe mode)
- Phase: P5 · Size: M · Status: todo
- Depends: BOOT-02
- Files: boot/ojjy-boot.c

**Context.** A/B system updates and recovery need a way to pick what boots.
**Goal.** Holding Option/Alt (or Esc) at boot shows a Liquid-Glass-styled
menu: "ojjyOS (slot A)", "ojjyOS (slot B — previous)", "Safe Mode",
"Recovery", "Firmware settings". Without a key press: boot the active slot
silently.
**Steps.**
1. Read boot slot state (`active`, `tries_left`) from an ESP file
   (format defined here; written by `updated`, SVC-40).
2. Draw the menu with GOP (simple font + rounded rects), keyboard navigation.
3. Decrement `tries_left` on each boot of a new slot; fall back on zero.
**Done when.** A broken slot automatically falls back after 3 failed boots;
manual selection works under VZ and QEMU.
**Tests.** QEMU test with a deliberately broken slot.
**Pitfalls.** VZ's EFI may not deliver key events early: also support a
`bootmenu=1` file trigger.
**Refs.** UEFI Simple Text Input Ex protocol.

### BOOT-05 — Flicker-free boot splash and handover
- Phase: P2 · Size: S · Status: todo
- Depends: WS-01
- Files: boot/ojjy-boot.c, kernel/src/main.c, services/windowserver

**Context.** Boot shows ASCII art and the console before the desktop
(Jonas likes the art; keep it on the serial log and in verbose mode).
**Goal.** Silent boot: firmware logo → ojjyOS logo with a progress bar
("Created by Jonas Lee" below) → login window fades in, with no flash of
console text. `log=debug` shows the classic console art instead.
**Done when.** Video capture of a VZ boot shows no console frame; boot
time unchanged ±50 ms.
**Tests.** `vz_smoke.py` capture sequence; pixel check that no frame is
mostly black text on blue.
**Pitfalls.** The framebuffer changes owner (kernel → WindowServer): hand
over the last frame so the first composited frame matches it.
**Refs.** —

### BOOT-06 — Device tree support (ARM platforms without ACPI)
- Phase: P1 · Size: M · Status: todo
- Depends: BOOT-01
- Files: new kernel/core/fdt.c, kernel/src/arch/aarch64/platform.c

**Context.** Device addresses (GIC, UART, virtio-mmio, PCI ECAM) are
hard-coded per platform in `platform.c`. QEMU virt and the Raspberry Pi
describe hardware with a flattened device tree (DTB).
**Goal.** A safe FDT parser and a platform layer that discovers devices
from the DTB when present, ACPI otherwise, hard-coded tables last.
**Steps.**
1. FDT parser: header checks, structure walk, `fdt_find_compatible()`,
   `fdt_get_reg()`, `fdt_get_interrupts()`, phandle lookup.
2. Platform discovery API: `platform_find("arm,gic-v3")` returning MMIO
   ranges/IRQs from DTB or ACPI (BOOT-07).
3. Convert GIC, timer, UART, PCI ECAM, virtio-mmio discovery.
**Done when.** QEMU virt boots with hard-coded addresses removed; VZ still
boots (ACPI path).
**Tests.** Host test parsing QEMU's dumped DTB (`-machine dumpdtb`), plus
fuzzing truncated/garbage blobs.
**Pitfalls.** All FDT integers are big-endian; strings block offsets must
be bounds-checked.
**Refs.** Devicetree Specification v0.4.

### BOOT-07 — ACPI table discovery (both arches)
- Phase: P1 · Size: M · Status: todo
- Depends: BOOT-01
- Files: kernel/src/drivers/acpi.c (split), new kernel/core/acpi_tables.c

**Context.** x86 has a minimal FADT/`_S5_` path; VZ exposes ACPI on ARM
(MADT/GTDT/MCFG/SPCR) but the kernel ignores it.
**Goal.** Parse RSDP→XSDT→tables with checksums; expose MADT (CPUs,
GIC/APIC), MCFG (PCI ECAM), FADT, GTDT, SPCR, HPET, DMAR/IORT (later).
**Steps.**
1. Table iterator with length/checksum validation.
2. MADT decoder for both GIC (ARM) and LAPIC/IOAPIC (x86) entries.
3. MCFG feeds PCI (DRV-PCI-01); MADT feeds SMP (SMP-01) and IRQs.
**Done when.** `[ACPI]` boot lines list CPUs, interrupt controllers and
ECAM ranges under VZ, QEMU ARM (with `-machine virt,acpi=on`) and x86.
**Tests.** Host tests on captured table dumps (store as test vectors),
fuzzed lengths.
**Pitfalls.** Do not trust table lengths beyond the mapped region; map
tables with the right attributes (normal, not device memory).
**Refs.** ACPI 6.5 spec §5.2.

### BOOT-08 — Boot time budget and measurement
- Phase: P2 · Size: S · Status: todo
- Depends: KDB-01
- Files: kernel/src/main.c, tests/vz_boot.py

**Context.** No one measures boot time; it will grow as services appear.
**Goal.** Timestamped boot milestones (`[BOOT +123ms] stage`), a summary
line, and a test that fails if VZ cold boot to login exceeds 3 s.
**Done when.** The budget test runs in the VZ suite.
**Tests.** `vz_boot.py --budget 3000`.
**Pitfalls.** Exclude host-side VM start time; measure from kernel entry.
**Refs.** —

---

## Memory management

### MEM-01 — Buddy page allocator with free
- Phase: P1 · Size: M · Status: todo
- Depends: —
- Files: kernel/src/memory.c → kernel/mm/pmm.c

**Context.** The bitmap allocator works but `pmm_alloc_bytes()` cannot
free, so every browser/UI buffer stays forever (TODO.md backlog item).
**Goal.** A buddy allocator (orders 0–10, 4 KiB–4 MiB) with
`pmm_alloc(order)`, `pmm_free(addr, order)`, contiguous allocations for
DMA, zones (DMA32 on x86), and accurate statistics.
**Steps.**
1. Build free lists from the UEFI map after reserving kernel, BootInfo,
   initrd, framebuffer and firmware regions.
2. Implement split/merge with a per-page order/flags byte.
3. Re-implement `pmm_alloc_bytes()`/`pmm_free_bytes()` on top; convert the
   browser image pool, sprite cache and big UI buffers to free on close.
4. `pmm_stats()` feeds Activity Monitor.
**Done when.** Opening and closing the browser 20 times returns free memory
to within 1% of the start value (logged by a live test).
**Tests.** Host test with a fake memory map: random alloc/free sequences,
invariants (no overlap, merge correctness), 100k operations under ASan.
**Pitfalls.** Some firmware ranges are reported as conventional memory but
are in use until `ExitBootServices`; ARM firmware memory near 0x44000000
must stay reserved (see AGENTS.md gotchas).
**Refs.** Knowlton's buddy system (any OS textbook).

### MEM-02 — Page frame database
- Phase: P1 · Size: S · Status: todo
- Depends: MEM-01
- Files: kernel/mm/page.c

**Context.** Shared memory, COW and page cache need per-page metadata.
**Goal.** `struct page { refcount; flags; order; owner/mapping; list }` for
every physical page, located in a region allocated at boot.
**Done when.** Allocator and later VM code use `page_ref_get/put`; stats
list pages by use (free, kernel, user, cache, DMA).
**Tests.** Host test for refcount rules.
**Pitfalls.** Keep it small (≤ 32 bytes per page = < 1% of RAM).
**Refs.** —

### MEM-03 — Arch-neutral page table API
- Phase: P1 · Size: M · Status: doing
- Depends: MEM-01
- Files: new kernel/mm/pt.h, kernel/src/arch/aarch64/mmu.c (from user_arch.c), kernel/src/arch/x86_64/mmu.c (from paging.c)
**Context.** ARM page-table code is embedded in the EL0 runner
(`user_arch.c`); x86 has `paging_map()` for the framebuffer only.
**Goal.** `pt_map(space, va, pa, size, prot)`, `pt_unmap`, `pt_protect`,
`pt_query`, TLB invalidation helpers, for 4 KiB pages and 2 MiB blocks,
with attribute sets: normal WB, device nGnRE, write-combine (framebuffer).
**Steps.**
- [ ] Extract ARM table walking from `user_arch.c` into `mmu.c`.
- [ ] Implement the x86_64 4-level version of the full arch-neutral pt_* API.
- [ ] Port the EL0 runner to the new API (no behaviour change).
**HW-13 shared preparation (not full MEM-03 completion).** These named
boxes own the reusable subset needed by HW-13; the HW-13 part boxes refer
here and must be ticked in both places with the same evidence. Full pt_*
extraction, WC framebuffer and this card's dependencies remain unchanged.
- [x] HW13-A (HW-13 part 3): ARM RAM/Device descriptor construction, PA-width/ownership validation and privileged high-map groundwork in user_arch.c and mapping helpers; test host encodings plus VZ/HVF high-backed protected isolation. Trap: preserve break-before-make, attributes and checked user VAs. — done 2026-10-29 (test: build/hw-13-part3/matrix.json, 21 builder + 51 PMM checks normal/fatal ASan/UBSan, both builds zero warnings; VZ4/VZ8/HVF4 each 51 high-backed protected runs with cleanup equality and present-alias permission faults. Full VZ clock test still fails known BUG-1008, explicitly not credited; full MEM-03 completion remains open).
- [x] HW13-X (HW-13 part 4): Intel supervisor descriptor/PA masks and failure-safe table construction in paging.c/h and mapping helpers; test every missing intermediate level, OOM, NXE and live high PA writes. Trap: current walker does not create a missing final PT; this is not proof of Intel ring 3. — done 2026-10-30 (test: build/hw-13-part4/matrix.json, 28 Intel host checks normal/fatal ASan/UBSan incl. missing PT/new-existing PD/OOM/NX/52-bit CPU vs alias span; final Intel4 3-GiB pattern/cleanup/recovery and full smoke pass, both builds zero warnings. Intel EL0 remains unsupported; full pt_*/WC work remains open).
**Done when.** The protected-app suite (19 programs) passes unchanged; the
framebuffer is mapped write-combining on both arches.
**Tests.** Live protected smoke (ARM); host test of descriptor encoding.
**Pitfalls.** ARM break-before-make rule when changing live mappings;
`dsb ishst; tlbi; dsb ish; isb` sequences.
**Refs.** Arm ARM D8 (VMSAv8-64); Intel SDM Vol 3 ch. 4.
**Log.** 2026-10-29 HW13-A and 2026-10-30 HW13-X preparatory subsets
verified (HW-13 part3/part4 matrices); full pt_* extraction/WC and the
dependency remain open. No full MEM-03 completion is claimed.

### MEM-04 — Kernel direct map and vmalloc area
- Phase: P1 · Size: M · Status: doing
- Depends: MEM-03
- Files: kernel/mm/kvm.c

**Context.** The kernel relies on identity/firmware mappings.
**Goal.** A direct map of all RAM at a fixed kernel virtual offset and a
`vmalloc()` area for large virtually-contiguous kernel buffers.
**Steps.**
- [ ] Complete the general direct map policy and all-caller phys_to_virt/virt_to_phys conversion (HW-13 covers only explicit HIGH opt-ins).
- [ ] Implement the vmalloc area for large virtually contiguous buffers and its range allocator tests.
**HW-13 shared preparation (not full MEM-04 completion).** Own the
RAM-only direct-alias subset below; HW-13 refers to these same boxes.
No vmalloc/all-caller conversion or waiver of the full MEM-03 dependency.
- [x] HW13-A (HW-13 part 3): ARM high RAM alias at 0xffff800000000000 + PA, shared privileged TTBR1 mappings and checked phys_to_virt/virt_to_phys helpers in kernel/mm/kvm.c/h; test Normal RAM only, Device exclusions and EL0 alias faults. Trap: metadata/tables stay LOW and no high RAM Device identity alias may conflict. — done 2026-10-29 (test: build/hw-13-part3/matrix.json, 21 builder + 51 PMM checks normal/fatal ASan/UBSan; VZ4/VZ8/HVF4 prove HIGH read/write/free, 153 high-backed protected runs total, EL0 alias permission faults and HIGH browser/TLS buffers. Full VZ clock test still fails known BUG-1008, explicitly not credited; full MEM-04 completion remains open).
- [x] HW13-X (HW-13 part 4): same RAM-only supervisor alias and checked translation on Intel using upper PML4 entries, with all table levels LOW; test high allocation/write/read/free plus mapper rollback and stats. Trap: virtual alias is not a device DMA physical address. — done 2026-10-30 (test: build/hw-13-part4/matrix.json, checked supervisor NX upper-PML4 publication, 28 Intel host mapping/rollback/capability checks normal/fatal ASan/UBSan; final Intel4 stress holds 3 GiB with 1071644672 HIGH bytes, verifies every page, exact LOW/HIGH cleanup and whoami/desktop recovery. VZ/HVF stress and regression matrix pass; legacy/DMA stay LOW, no vmalloc/full-card completion claim).
**Done when.** All kernel allocations go through the direct map or vmalloc;
`phys_to_virt`/`virt_to_phys` used everywhere instead of casts.
**Tests.** Live smoke; host test of the vmalloc range allocator.
**Pitfalls.** Device MMIO must never be accessed through the cacheable
direct map; use `ioremap()`.
**Refs.** D-003.
**Log.** 2026-10-29 HW13-A ARM and 2026-10-30 HW13-X Intel RAM-only
alias/translation subsets verified (HW-13 part3/part4 matrices); vmalloc
and all-caller migration remain open. Full MEM-04 stays incomplete and
the full MEM-03 dependency is unchanged.

### MEM-05 — Upper-half kernel (both arches)
- Phase: P1 · Size: L · Status: todo
- Depends: MEM-04, BOOT-01
- Files: kernel/linker-aarch64.ld, kernel/linker.ld, entry.S, entry.asm, boot/ojjy-boot.c

**Context.** ARM kernel links at `0x41000000` physical, x86 at 64 MiB.
User address space needs the low half (D-003).
**Goal.** Kernel linked at `0xffff_ffff_8000_0000`-style addresses, early
boot code builds initial tables (identity + high) and jumps high, TTBR1
(ARM) / upper PML4 entries (x86) hold the kernel.
**Steps.**
1. Position-independent early assembly; boot page tables in the image.
2. Bootloader loads by physical address from the ELF `p_paddr`.
3. Remove identity mapping after the jump (catches stray low pointers).
**Done when.** Both arches boot all suites with the identity map removed;
NULL and low addresses fault in kernel mode.
**Tests.** Full smoke matrix (TCG, HVF, x86, VZ).
**Pitfalls.** BootInfo and framebuffer pointers are physical; convert.
Firmware runtime services (if ever used) need their own mapping.
**Refs.** Arm ARM TTBR1/TCR_EL1.T1SZ; Intel SDM 4.5.

### MEM-06 — Slab allocator and `kmalloc` size classes
- Phase: P1 · Size: M · Status: todo
- Depends: MEM-04
- Files: kernel/mm/slab.c, kernel/src/memory.c (heap)

**Context.** `kmalloc` is a 512 KB first-fit heap; it will not scale to
thousands of threads, handles and messages.
**Goal.** Object caches (`kmem_cache_create`) and `kmalloc` size classes
(16 B–8 KiB) backed by pages; big allocations go to vmalloc. Debug mode
with red zones and poisoning.
**Done when.** Old heap removed; `kheap_validate()` replaced by
`slab_validate()`; Activity Monitor shows per-cache usage.
**Tests.** Host stress test (millions of random alloc/free, ASan clean);
live smoke.
**Pitfalls.** IRQ-context allocations need a non-sleeping flag.
**Refs.** Bonwick, "The Slab Allocator" (1994).

### MEM-07 — Kernel W^X and guard pages
- Phase: P1 · Size: S · Status: todo
- Depends: MEM-05
- Files: kernel/mm/kvm.c, linker scripts

**Context.** `BETTER_OS_PLAN.md` §1: kernel text is writable.
**Goal.** Text RX, rodata R, data/bss RW+NX; guard page below every kernel
stack; red zones in debug slab.
**Done when.** A test command that writes to kernel text faults with a
clear panic in debug builds.
**Tests.** Live test `kdebug wx-test` (debug builds only).
**Pitfalls.** Self-modifying code (none expected); alternative patching
must use a temporary writable alias.
**Refs.** —

### MEM-08 — Address spaces and regions (VMAs)
- Phase: P1 · Size: M · Status: todo
- Depends: MEM-03, MEM-06
- Files: kernel/mm/vm_space.c

**Context.** Only the single EL0 program has an address space.
**Goal.** `vm_space` objects with a balanced tree of regions
(start, size, prot, backing object, offset, flags), `vm_map()`,
`vm_unmap()`, `vm_protect()`, ASLR placement, per-space page tables.
**Done when.** Two user address spaces coexist and switch on context switch.
**Tests.** Host tests for region insertion/splitting/merging and ASLR
bounds.
**Pitfalls.** Splitting regions on partial unmap/protect; ASID management
on ARM (16-bit ASIDs, rollover flush).
**Refs.** D-003.

### MEM-09 — Page fault handler and demand paging
- Phase: P1 · Size: M · Status: todo
- Depends: MEM-08
- Files: kernel/mm/fault.c, arch exception vectors

**Goal.** Anonymous memory is allocated on first touch (zero-filled);
faults outside regions deliver an exception to the process (PROC-08),
never panic the kernel.
**Done when.** A program touching 64 MiB sparse memory uses only touched
pages (Activity Monitor/stats); bad accesses terminate only that program.
**Tests.** Protected-program additions: sparse touch, out-of-region
access, write to read-only.
**Pitfalls.** Faults while the kernel copies from user memory (MEM-13).
**Refs.** Arm ARM ESR_EL1 decoding; Intel #PF error code.

### MEM-10 — Copy-on-write
- Phase: P2 · Size: M · Status: todo
- Depends: MEM-09, MEM-02
- Files: kernel/mm/fault.c, kernel/mm/vm_object.c

**Goal.** Private mappings share pages until written; needed for `fork`
(PROC-03) and private file mappings.
**Done when.** `fork` of a 100 MiB process copies only touched pages.
**Tests.** Host test of the refcount/COW state machine; live fork test.
**Pitfalls.** TLB shootdown on all CPUs when write-protecting (SMP-02).
**Refs.** —

### MEM-11 — Memory objects and shared memory handles
- Phase: P1 · Size: M · Status: todo
- Depends: MEM-08, SYS-02
- Files: kernel/mm/vm_object.c

**Goal.** `vmo_create(size)`, map into several processes, resize, pass as
a handle over IPC (IPC-05). Basis for window surfaces and audio buffers.
**Done when.** Two processes exchange a 4 MiB buffer with zero copies.
**Tests.** Live test program pair.
**Pitfalls.** Cache attributes must match across mappings.
**Refs.** D-005.

### MEM-12 — Memory-mapped files and the page cache
- Phase: P3 · Size: L · Status: todo
- Depends: MEM-11, BLK-04
- Files: kernel/mm/filemap.c

**Goal.** `mmap` of files with shared/private semantics, unified with the
block page cache, write-back of dirty pages, `msync`.
**Done when.** A program maps a 200 MiB file and reads it at disk speed;
edits through a shared mapping persist after reboot.
**Tests.** Live test with ojjyFS.
**Pitfalls.** Truncation of mapped files; coherence with `read`/`write`.
**Refs.** —

### MEM-13 — Safe user-memory access (copyin/copyout)
- Phase: P1 · Size: S · Status: todo
- Depends: MEM-09
- Files: kernel/mm/usercopy.c (generalise user_copy_from in user.c)

**Context.** `user_copy_from()` validates ranges against the single EL0
image; it cannot handle demand paging or concurrent unmaps.
**Goal.** `copy_from_user`, `copy_to_user`, `strncpy_from_user` with fault
recovery tables (a fault inside them returns `-EFAULT`), PAN on ARM /
SMAP on x86 enabled so the kernel never touches user memory accidentally.
**Done when.** All syscalls use these helpers; PAN/SMAP enabled.
**Tests.** Protected programs passing bad pointers get `-EFAULT`, kernel
survives (extend `badptr.elf`).
**Pitfalls.** Exception table must be sorted; unaligned accesses.
**Refs.** Arm ARM PSTATE.PAN; Intel SMAP.

### MEM-14 — Memory pressure, reclaim and OOM policy
- Phase: P3 · Size: M · Status: todo
- Depends: MEM-12
- Files: kernel/mm/reclaim.c

**Goal.** Evict clean page-cache pages under pressure, notify processes
(`memorystatus` event like macOS) so apps drop caches, and kill the
lowest-priority background process when out of memory — never the
WindowServer.
**Done when.** A memory-eating test program is killed with a notification,
desktop keeps running.
**Tests.** Live stress test.
**Pitfalls.** Reclaim must not deadlock on filesystem locks.
**Refs.** —

### MEM-15 — Compressed memory (zram-like)
- Phase: P9 · Size: M · Status: todo
- Depends: MEM-14
- Files: kernel/mm/compress.c

**Goal.** Compress inactive anonymous pages in RAM (LZ4-like, original
implementation) before killing anything; optional swap file on ojjyFS.
**Done when.** A 2 GiB VM runs a working set of 3 GiB of compressible
pages without OOM.
**Tests.** Host test of the compressor; live stress.
**Pitfalls.** CPU cost on interactive threads; compress in background.
**Refs.** —

### MEM-16 — Large mappings for framebuffers and DMA buffers
- Phase: P4 · Size: S · Status: todo
- Depends: MEM-03
- Files: kernel/mm/pt.c

**Goal.** 2 MiB block mappings where alignment allows (framebuffer,
surfaces) to cut TLB misses; measured composite-time improvement.
**Done when.** Benchmark shows fewer TLB misses or faster full-screen blit.
**Tests.** Perf test before/after.
**Pitfalls.** Splitting blocks when partial unmap happens.
**Refs.** —

### MEM-17 — Per-process memory accounting
- Phase: P2 · Size: S · Status: todo
- Depends: MEM-08, PROC-01
- Files: kernel/mm/vm_space.c, Activity Monitor

**Goal.** Resident, shared, private, compressed, GPU memory per process;
system totals (wired, cache, free). Activity Monitor shows real numbers
instead of estimates.
**Done when.** Numbers in Activity Monitor match a test program's known
usage within 5%.
**Tests.** Live test.
**Pitfalls.** Shared pages counted once in totals.
**Refs.** —

### MEM-18 — DMA API (and IOMMU later)
- Phase: P2 · Size: M · Status: todo
- Depends: MEM-04
- Files: kernel/mm/dma.c

**Context.** Drivers use physical addresses directly from the PMM.
**Goal.** `dma_alloc_coherent`, `dma_map_single/sg` with cache maintenance
on ARM (clean/invalidate), bounce buffers for 32-bit devices; IOMMU
(SMMUv3/VT-d) hooks for later (`SEC-60`).
**Done when.** virtio, xHCI and NVMe drivers use the DMA API.
**Tests.** Driver suites unchanged.
**Pitfalls.** VZ/QEMU devices are cache-coherent; real hardware may not be.
**Refs.** Arm ARM cache maintenance instructions.

---

## Threads and synchronization

### THR-01 — Threads, kernel stacks and context switch
- Phase: P1 · Size: L · Status: todo
- Depends: MEM-06
- Files: kernel/core/thread.c, kernel/src/arch/aarch64/switch.S, kernel/src/arch/x86_64/switch.S

**Context.** One kernel stack, one main loop; EL0 programs run
synchronously from the main loop.
**Goal.** `thread` objects with a 16 KiB kernel stack (guard page below),
saved callee-saved registers, FP/SIMD state saved lazily for user threads,
`switch_to(prev, next)` for both arches.
**Steps.**
1. Define `struct thread` (id, state, stack, context, owner process, prio).
2. Write `switch.S` per arch; test switching between two kernel threads.
3. FP/SIMD: kernel stays general-regs-only; user FP state saved on switch
   (trap on first use to save lazily).
**Done when.** Two kernel threads ping-pong 1,000,000 times without
corruption; context switch cost logged (< 1 µs under HVF).
**Tests.** In-kernel self-test (`kdebug threads`); live smoke.
**Pitfalls.** ARM: `SP_EL0`, `TPIDR_EL0`, `TPIDRRO_EL0` per thread;
x86: `FS/GS` bases, `swapgs` discipline.
**Refs.** AAPCS64; System V AMD64 ABI.

### THR-02 — Kernel thread API
- Phase: P1 · Size: S · Status: todo
- Depends: THR-01, SCH-01
- Files: kernel/core/kthread.c

**Goal.** `kthread_create(fn, arg, name, prio)`, `kthread_exit`,
`kthread_join`, `kthread_should_stop`; named threads visible in
Activity Monitor.
**Done when.** Used by SCH-05 workers.
**Tests.** Self-test creating/destroying 1,000 threads (no leaks).
**Pitfalls.** Freeing a stack while running on it — reap from another
thread.
**Refs.** —

### THR-03 — Locks and atomics
- Phase: P1 · Size: M · Status: todo
- Depends: THR-01
- Files: kernel/core/sync.c, kernel/core/atomic.h

**Goal.** IRQ-safe spinlocks (ticket locks), sleeping mutexes with
priority inheritance, reader/writer locks, semaphores, condition
variables, completion objects; atomics with explicit memory ordering
(C11 `__atomic` builtins); lock debugging (owner, deadlock detection in
debug builds, "sleeping while holding spinlock" checks).
**Done when.** Global state used by more than one thread is protected;
debug build reports lock-order violations.
**Tests.** Host tests of the algorithms with pthreads shims; kernel
self-test with contention.
**Pitfalls.** ARM needs `ldaxr/stlxr` (or LSE atomics); WFE/SEV in spin
loops for power.
**Refs.** Arm ARM B2 memory model.

### THR-04 — Thread-local storage for user threads
- Phase: P2 · Size: S · Status: todo
- Depends: THR-01, ELF-02
- Files: kernel/core/thread.c, lib/libc

**Goal.** Per-thread TLS register (TPIDR_EL0 / FS base) set by the loader
and thread creation; ELF TLS (`PT_TLS`) initial image copied per thread.
**Done when.** A test program reads distinct `__thread` values from 8
threads.
**Tests.** Live test.
**Pitfalls.** Variant I (ARM) vs Variant II (x86) TLS layouts.
**Refs.** Drepper, "ELF Handling for Thread-Local Storage".

### THR-05 — User-space wait primitive (futex-like)
- Phase: P2 · Size: S · Status: todo
- Depends: SYS-05, SCH-04
- Files: kernel/core/futex.c

**Goal.** `futex_wait(addr, expected, timeout)` / `futex_wake(addr, n)` so
libc mutexes/condvars don't syscall when uncontended. Also used by the
Linux personality.
**Done when.** libc `pthread_mutex` passes stress tests with 16 threads.
**Tests.** Live stress program.
**Pitfalls.** Key futexes by (address space, page, offset) for shared
memory.
**Refs.** Drepper, "Futexes Are Tricky".

---

## Scheduling

### SCH-01 — Run queue and cooperative yield
- Phase: P1 · Size: S · Status: todo
- Depends: THR-01
- Files: kernel/core/sched.c

**Goal.** Ready queue, `sched_yield()`, `schedule()`, idle thread per CPU
(WFI/HLT). The main loop becomes "thread 1".
**Done when.** Two kernel threads plus the main loop share the CPU by
yielding; desktop works as before.
**Tests.** Live smoke + `qemu_perf.py` unchanged.
**Pitfalls.** The old main loop's sleep logic (`compositor_wants_fast_loop`)
must move into the idle decision.
**Refs.** —

### SCH-02 — Timer preemption
- Phase: P1 · Size: M · Status: todo
- Depends: SCH-01, IRQ-01
- Files: kernel/core/sched.c, timer drivers

**Goal.** Time slices (default 4 ms), `need_resched` set from the timer IRQ,
preemption on IRQ return and on `preempt_enable()`; preempt count for
critical sections.
**Done when.** A spinning kernel thread no longer freezes the desktop
(pointer and animation keep 60 fps under HVF).
**Tests.** New live test: start a CPU-burner thread, measure pointer gap
(`[PERF] ptr_gap_max_ms`) stays < 20 ms under HVF.
**Pitfalls.** Never preempt with a spinlock held; IRQ return path on ARM
must check the flag before `eret`.
**Refs.** —

### SCH-03 — Priority classes and interactivity boost
- Phase: P1 · Size: M · Status: todo
- Depends: SCH-02
- Files: kernel/core/sched.c

**Goal.** Classes realtime > interactive > normal > background (D-004),
priorities within classes, a boost for the focused app and WindowServer,
starvation protection for background work.
**Done when.** Building something heavy in Terminal (later) or a CPU burner
at normal priority leaves window dragging smooth.
**Tests.** Live perf test with burner threads at each class.
**Pitfalls.** Priority inversion: mutexes need inheritance (THR-03).
**Refs.** —

### SCH-04 — Wait queues, sleep and timeouts
- Phase: P1 · Size: S · Status: todo
- Depends: SCH-01, TIM-02
- Files: kernel/core/waitq.c

**Goal.** `wait_event(wq, cond, timeout)`, `wake_up(wq)`, `sleep_ms`,
interruptible waits (for signals/process kill).
**Done when.** Drivers stop busy-waiting; idle CPU goes to WFI.
**Tests.** Self-tests for timeout accuracy (±1 tick).
**Pitfalls.** Lost-wakeup race: check condition after queueing.
**Refs.** —

### SCH-05 — First payoff: background kernel workers
- Phase: P1 · Size: M · Status: todo
- Depends: SCH-02, THR-02, SCH-04
- Files: kernel/src/net/*, kernel/src/fs/persist.c, kernel/src/ui/compositor.c

**Context.** BETTER_OS_PLAN §1: network, autosave and rendering stall each
other in the single loop.
**Goal.** Network RX/TX and timers run in a `netd` kernel thread;
persistence snapshot writing in a `persistd` thread; the compositor loop
in its own interactive thread; block I/O waits sleep instead of spinning.
**Done when.** Dragging a window during a 1 MiB save and a page download
never drops below 50 fps under HVF (measured).
**Tests.** New `tests/qemu_concurrency.py` (download + autosave + drag).
**Pitfalls.** Shared data between UI and net (browser fetch state) needs
locks or message queues.
**Refs.** —

### SCH-06 — Tickless idle
- Phase: P3 · Size: S · Status: todo
- Depends: SCH-04, TIM-02
- Files: kernel/core/sched.c, timer drivers

**Goal.** No 1 kHz tick while idle: program the timer for the next event.
**Done when.** Idle desktop wakes < 20 times/second (counter probe).
**Tests.** Counter probe test (`qemu-kernel-counter-probe` skill method).
**Pitfalls.** Lost time accounting; input polling must become IRQ-driven
first (IRQ-04).
**Refs.** —

### SCH-07 — Scheduler statistics and CPU usage
- Phase: P2 · Size: S · Status: todo
- Depends: SCH-03, PROC-01
- Files: kernel/core/sched.c, Activity Monitor

**Goal.** Per-thread runtime, wakeups, context switches, run-queue length;
real CPU % in Activity Monitor and `top`.
**Done when.** CPU % of a burner thread reads ~100% of one core.
**Tests.** Live test.
**Pitfalls.** Use the monotonic nanosecond clock, not ticks.
**Refs.** —

### SCH-08 — Realtime class for audio and input
- Phase: P3 · Size: M · Status: todo
- Depends: SCH-03, AUD-01
- Files: kernel/core/sched.c

**Goal.** Deadline-ish scheduling for the audio mix thread (period 5–10 ms)
and input threads, with a CPU-time cap to prevent lockups.
**Done when.** Audio plays without glitches during heavy CPU load.
**Tests.** Audio glitch counter test.
**Pitfalls.** A runaway realtime thread must be demoted automatically.
**Refs.** —

### SCH-09 — SMP load balancing and affinity
- Phase: P2 · Size: M · Status: todo
- Depends: SMP-02, SCH-03
- Files: kernel/core/sched.c

**Goal.** Per-CPU run queues, periodic balancing and work stealing, CPU
affinity masks, migration-safe threads.
**Done when.** Four burner threads on a 4-CPU VM use ~400% CPU.
**Tests.** Live test with `smp=4`.
**Pitfalls.** Cache-hot migration costs; keep interactive threads local.
**Refs.** —

---

## SMP and interrupts

### SMP-01 — Secondary CPU bring-up
- Phase: P2 · Size: M · Status: todo
- Depends: BOOT-07, THR-01, MEM-05
- Files: kernel/src/arch/aarch64/smp.c, kernel/src/arch/x86_64/smp.c

**Context.** `-smp 1`; extra cores parked by MPIDR.
**Goal.** ARM: PSCI `CPU_ON` for each MADT/DTB CPU; x86: INIT-SIPI-SIPI via
LAPIC with a real-mode trampoline. Each CPU gets its stack, vectors,
timer and idle thread.
**Done when.** `smp=4` boots with 4 online CPUs on VZ, QEMU ARM and x86.
**Tests.** Smoke with `-smp 4`; `vz_boot.py --cpus 4`.
**Pitfalls.** Caches/MMU state on secondary entry; x86 trampoline must be
below 1 MiB.
**Refs.** PSCI spec (Arm DEN0022); Intel SDM 8.4.

### SMP-02 — Per-CPU data, IPIs and TLB shootdown
- Phase: P2 · Size: M · Status: todo
- Depends: SMP-01
- Files: kernel/core/percpu.c, IRQ controllers

**Goal.** Per-CPU variables (TPIDR_EL1/GS base), inter-processor
interrupts (reschedule, call-function, TLB flush), broadcast TLB
invalidation (`tlbi ...is` on ARM inner-shareable; IPIs on x86).
**Done when.** Unmapping memory in a multi-threaded process is safe on 4 CPUs.
**Tests.** Stress test of map/unmap with threads on all CPUs.
**Pitfalls.** IPI deadlocks with interrupts disabled.
**Refs.** GIC SGIs; Intel LAPIC ICR.

### SMP-03 — Concurrency audit of existing subsystems
- Phase: P2 · Size: L · Status: todo
- Depends: SMP-02, THR-03
- Files: all of kernel/src

**Goal.** Every global in drivers, VFS, network and UI code reviewed and
protected (lock, per-CPU, or confined to one thread). Lockdep-style
checker in debug builds.
**Done when.** 24-hour soak test with `smp=4` runs without panics or
lock warnings.
**Tests.** `tests/qemu_soak.py` (new).
**Pitfalls.** Old code assumes no concurrency everywhere — budget time.
**Refs.** —

### IRQ-01 — Interrupt framework
- Phase: P1 · Size: M · Status: todo
- Depends: —
- Files: kernel/core/irq.c, kernel/src/drivers/driver.c

**Goal.** Interrupt descriptors, `request_irq(irq, handler, flags, name)`,
shared IRQs, threaded handlers (top/bottom halves), per-IRQ statistics
(counts visible in a `interrupts` command).
**Done when.** All drivers use the framework on both arches.
**Tests.** Live smoke; `interrupts` output sanity check.
**Pitfalls.** Level vs edge triggered; EOI ordering on GIC.
**Refs.** —

### IRQ-02 — Full GICv3 with ITS (MSI)
- Phase: P2 · Size: M · Status: todo
- Depends: IRQ-01
- Files: kernel/src/arch/aarch64/gicv2.c → gic.c

**Goal.** Redistributor setup per CPU, SGIs/PPIs/SPIs, LPIs via the ITS so
PCI devices can use MSI/MSI-X (NVMe, modern virtio).
**Done when.** virtio-pci devices use MSI-X under HVF/VZ.
**Tests.** Driver suites with MSI enabled.
**Pitfalls.** ITS command queue and tables need DMA-coherent memory.
**Refs.** Arm GICv3/v4 Architecture Specification (IHI 0069).

### IRQ-03 — x86 APIC, IOAPIC and MSI
- Phase: P2 · Size: M · Status: todo
- Depends: IRQ-01, BOOT-07
- Files: kernel/src/arch/x86_64/apic.c

**Goal.** Replace the 8259 PIC with LAPIC timer + IOAPIC routing (from MADT
overrides) and MSI/MSI-X for PCI.
**Done when.** x86 smoke passes with the PIC masked.
**Tests.** x86 smoke matrix.
**Pitfalls.** Interrupt source overrides (ISA IRQ0→GSI2).
**Refs.** Intel SDM Vol 3 ch. 10–11; IOAPIC datasheet.

### IRQ-04 — Interrupt-driven input and devices
- Phase: P2 · Size: S · Status: todo
- Depends: IRQ-01, SCH-04
- Files: virtio_input.c, xhci.c, usb_hid.c

**Context.** Input is polled every loop turn (and between frame stages).
**Goal.** Input devices raise interrupts; a high-priority input thread
queues events; polling removed. Latency equal or better.
**Done when.** `ptr_gap` metric unchanged or better; idle wakeups drop.
**Tests.** `qemu_pointer.py`, perf.
**Pitfalls.** Interrupt storms from misprogrammed rings.
**Refs.** virtio spec 1.2 §2.7.

### IRQ-05 — Exceptions to processes, kernel oops recovery
- Phase: P1 · Size: S · Status: todo
- Depends: PROC-01
- Files: arch exception handlers, kernel/core/exception.c

**Goal.** User faults (bad access, illegal instruction, alignment, FP)
become process exceptions (kill or deliver to a handler); kernel faults in
a syscall kill the calling thread with a logged "oops" when state is safe,
panic otherwise.
**Done when.** All 19 fault programs terminate only themselves with the
right reason under the real process model.
**Tests.** Protected-program suite.
**Pitfalls.** Faults with locks held → panic, not oops.
**Refs.** —

---

## Time

### TIM-01 — Unified clock sources
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: kernel/core/time.c, timer_a64.c, x86 timer.c

**Goal.** Nanosecond monotonic clock from the ARM generic counter
(CNTVCT) / x86 TSC (calibrated with HPET/PIT), `time_ns()`; ticks become
a derived value.
**Done when.** All timeouts use `time_ns`; drift < 1 ms/min vs host.
**Tests.** VZ clock delta test (existing timer report) extended.
**Pitfalls.** Unstable TSC on old CPUs; use invariant TSC flag.
**Refs.** Arm generic timer; Intel SDM 17.17.

### TIM-02 — High-resolution timers
- Phase: P1 · Size: S · Status: todo
- Depends: TIM-01, IRQ-01
- Files: kernel/core/hrtimer.c

**Goal.** Timer queue (min-heap or wheel) programming the next deadline;
callbacks in IRQ or thread context; used by sleep, timeouts, animation
frame ticks.
**Done when.** `sleep_ms(1)` accuracy ±0.2 ms under HVF.
**Tests.** Self-test statistics.
**Pitfalls.** Re-arming timers from their own callback.
**Refs.** —

### TIM-03 — Wall clock, time zones and locale time
- Phase: P2 · Size: M · Status: todo
- Depends: TIM-01, TIM-04
- Files: kernel/core/walltime.c, lib/libojjy/tz.c, /System/Library/Zoneinfo

**Context.** Guest clock is UTC; menu bar shows UTC-based time.
**Goal.** Kernel keeps UTC; user space converts with the IANA tz database
(compiled binary files, public domain data). Settings > Date & Time picks
the zone (auto from IP later), 12/24 h (exists).
**Done when.** Menu bar shows Jonas's local time (America/New_York)
including DST changes.
**Tests.** Host tests of tz conversion around DST transitions.
**Pitfalls.** Keep tzdata updatable via packages.
**Refs.** IANA tz database, RFC 8536 (TZif).

### TIM-04 — Clock sync at boot (SNTP)
- Phase: P0 · Size: S · Status: done
- Depends: —
- Files: kernel/src/clock.c/h (new), kernel/src/net/net.c, kernel/src/ui/netcmd.c, kernel/src/arch/aarch64/pl031.c, kernel/src/drivers/rtc.c, kernel/Makefile, scripts/test.sh, tests/test_clock.c (new), tests/qemu_net.py

**Context (2026-10-27).** Track N's N4 (`NET_WEB_PLAN.md`): the wall
clock is whatever the firmware/RTC says — no network correction. Auto
appearance and future certificate-validity checks depend on real time.

**Steps.**
- [x] 1. `kernel/src/clock.c/h`: unix<->civil conversion, the NTP correction offset, `clock_adjust_rtc()` applied at the end of both platform `rtc_read_time` implementations (signature unchanged, so the menu clock and Auto appearance pick corrected time up for free) and `clock_now_unix()`. Files: clock.c/h, pl031.c, rtc.c, kernel/Makefile, scripts/test.sh. Test: new `tests/test_clock.c` (conversions, offset apply, both directions). Trap: keep clock.h kernel-neutral; the adjust runs AFTER the platform fills the struct. — done 2026-10-27 (test: tests/test_clock.c conversion checks incl. leap day, weekday and round trips; both dev builds 0 warnings; the corrected-read path is asserted end to end by the state-machine case)
- [x] 2. SNTP pure core in `clock.c`: 48-byte client packet build (LI=0, VN=4, mode=3), reply parse + validation (mode 4, stratum 1-15, non-zero transmit timestamp, originate echo must match our transaction, |offset| <= 10 years, NTP-era 2036 rollover resolution), RFC 4330 offset math in ms. Test: `tests/test_clock.c` vectors + rejection cases + rollover. Trap: NTP timestamps are 1900-based seconds in 32.32 fixed point. — done 2026-10-27 (test: tests/test_clock.c 41/41 on host and under ASan/UBSan — build, 9 rejection cases, the 1970-boot big first correction, era rollover accepted at 2040 and rejected at 2026)
- [x] 3. SNTP client + hooks: resolve time.apple.com (fallback pool.ntp.org) over the existing UDP API, fixed local port, reply handler, retry/backoff when offline, resync every 6 h, start after the DHCP lease is acquired (inside net.c, no main.c change), poll hook in net_poll; log `[CLOCK] NTP offset %+d ms from <server>` and a clear giving-up line when offline. Test: build + `tests/test_clock.c` with stubbed net functions. Trap: dns_resolve is non-blocking (poll PENDING); never block the stack — the client is a polled state machine like DNS itself. — done 2026-10-27 (test: the state machine case drives DNS fail -> backoff, pending -> done -> send, reply -> corrected clock, and the 6 h resync guard, all through stubs; tests/test_net.c stubs the two hooks so its captured frames stay clean)
- [x] 4. Terminal commands: `date` prints the corrected time, `ntp` forces a sync (netcmd.c; add both to Tab completion). Test: build; live in step 5. — done 2026-10-27 (test: builds 0 warnings both arches; live receipts in step 5)
- [x] 5. Live: `tests/qemu_net.py` gains a `date` receipt (always) and an NTP step (internet, not a gate, must log either success or a clean give-up); smoke TCG + HVF re-run; screenshots looked at. Evidence `build/tim-04-*`. — done 2026-10-27 (test: qemu_net.py PASS with --internet under HVF: `date` logs "[CLOCK] date UTC 2026-10-27", `ntp` logs "[CLOCK] NTP offset +963 ms from time.apple.com" (a real sync over the internet), the follow-up date carries "(NTP ..."); smoke TCG+HVF PASS (build/tim-04-smoke-{tcg,hvf}); 05-date/06-ntp-date screenshots read)
- [x] 6. Docs: NET_WEB_PLAN.md N4 log, AGENTS.md LATEST, card done + Log. Test: roadmap check + checklist --strict clean. — done 2026-10-27 (test: roadmap check 698/0; checklist --strict 0 PROBLEM; NET_WEB_PLAN N4 DONE block written; What's New page gains the clock bullet)

**Context.** Owned by Track N as N4 in `NET_WEB_PLAN.md`.
**Goal.** SNTP query after DHCP; correct the wall clock; periodic resync.
**Done when.** As specified by N4.
**Tests.** As specified by N4.
**Pitfalls.** —
**Refs.** RFC 4330.
**Log.** Done 2026-10-27 (commits 54d3385 + test/docs tick): new
`kernel/src/clock.c/h` — the NTP correction is applied inside both
platform `rtc_read_time` implementations (signatures unchanged), so
the menu clock, Auto appearance and `clock_now_unix` all read synced
time. SNTP client starts after the DHCP lease (hook in net.c), queries
time.apple.com with pool.ntp.org fallback, validates hard (echo
transaction check, stratum, plausible window, 10-year skew cap with a
1970-boot exception, 2036-era rollover), resyncs every 6 h, backs off
offline with a `[CLOCK]` give-up log. Terminal gains `date` (+ one
`[CLOCK] date` serial line for headless tests) and `ntp`. Verified:
tests/test_clock.c 41/41 host + ASan/UBSan; qemu_net.py PASS under HVF
with --internet incl. a real `+963 ms from time.apple.com` sync; smoke
TCG+HVF; both dev builds 0 warnings. Evidence build/tim-04-*.
Limits: the 6 h resync is not persisted across boots (TIM-05 writes
back to the RTC); QEMU's RTC already carries host time so visible
offsets are small.

**Audit follow-up (2026-10-27, board 1674cc).** The original single-sync
test missed dead IDLE timers and a wall-time/uptime deadline mix. Fixed
automatic retry/6 h resync, idempotent DHCP start, peer/port/expiry checks,
fractional-offset carry, and independent receive/transmit era unfolding.
Missing/out-of-order server timestamps are refused. Added the missing
`date` serial receipt. Verified: 61 clock assertions normal + ASan/UBSan,
both dev builds zero warnings, TCG/HVF/Intel protected smokes, five VZ RAM
boot probes, four-case launch matrix and native protected/persistence smoke.
Full records: `REVIEW_COMPLETED_WORK.md`, `build/review/`. Initial external
NTP was unreachable; the final strict internet run passed with a real
-64 ms correction from pool.ntp.org (network-internet-strict.log).

### TIM-05 — Clock persistence and suspend/resume time
- Phase: P9 · Size: S · Status: todo
- Depends: TIM-03, SVC-60
- Files: kernel/core/walltime.c, RTC drivers

**Goal.** Write corrected time back to the RTC; account for sleep time on
resume (monotonic vs boottime clocks).
**Done when.** After sleep/resume on hardware, clock is correct.
**Tests.** Hardware test record.
**Pitfalls.** RTC in local time on Windows dual-boot machines.
**Refs.** —

### TIM-06 — Fast time page (vDSO-like)
- Phase: P2 · Size: S · Status: todo
- Depends: TIM-01, ELF-02
- Files: kernel/core/timepage.c, lib/libc/time.c

**Goal.** A read-only shared page with clock parameters so
`clock_gettime` needs no syscall.
**Done when.** 10 M `clock_gettime` calls take < 0.5 s under HVF.
**Tests.** Microbenchmark program.
**Pitfalls.** Seqlock for consistent reads.
**Refs.** —

---

## System calls and kernel objects

### SYS-01 — Native syscall entry/exit path
- Phase: P1 · Size: M · Status: todo
- Depends: THR-01, MEM-13
- Files: kernel/src/arch/aarch64/user_entry.S, kernel/src/arch/x86_64/syscall.S, kernel/core/syscall.c

**Context.** `user_syscall()` handles 6 calls for the bounded runner.
**Goal.** General entry path: save user context into the thread, dispatch
through a table, check pending work (reschedule, signals) on exit. x86:
`syscall/sysret` with MSRs (STAR/LSTAR/SFMASK).
**Done when.** Syscall round trip < 200 ns under HVF.
**Tests.** Microbenchmark program; protected suite.
**Pitfalls.** `sysret` with non-canonical RIP is a known exploit vector —
check before returning.
**Refs.** Intel SDM SYSCALL/SYSRET; Arm SVC.

### SYS-02 — Handles and kernel object model
- Phase: P1 · Size: M · Status: todo
- Depends: MEM-06
- Files: kernel/core/object.c, kernel/core/handle.c

**Goal.** Refcounted kernel objects (process, thread, vm_object, port,
event, file, socket, timer) with a per-process handle table, rights masks
(read, write, map, transfer, duplicate, wait), `handle_close`,
`handle_duplicate(rights)`.
**Done when.** Every new syscall uses handles; leaks detectable in debug.
**Tests.** Host tests of the handle table (generation counters to catch
stale handles).
**Pitfalls.** Use-after-free when a handle closes during a blocking call —
hold a reference.
**Refs.** Zircon handle model (concepts only).

### SYS-03 — Syscall table v1 and `libojjy` headers
- Phase: P1 · Size: M · Status: todo
- Depends: SYS-01, SYS-02
- Files: kernel/core/syscall_table.c, lib/libojjy/include/ojjy/syscall.h, docs/abi/syscalls.md

**Goal.** A documented, numbered, stable syscall list (groups: process,
thread, memory, handle, port, file, dir, socket, time, futex, debug,
system info), generated header + stubs from one definition file
(`kernel/core/syscalls.def`).
**Done when.** `docs/abi/syscalls.md` lists every call with arguments,
errors and rights required; stubs generated by `tools/gensyscalls.py`.
**Tests.** Generator test; ABI check script compares numbers across
releases (no renumbering).
**Pitfalls.** Never reuse a number; mark deprecated instead.
**Refs.** D-006.

### SYS-04 — Error codes
- Phase: P1 · Size: S · Status: todo
- Depends: SYS-03
- Files: lib/libojjy/include/ojjy/errors.h

**Goal.** Native error enum (negative returns) with names/strings,
mapping to POSIX errno and Linux errno and Win32 `GetLastError` codes
(tables used by compat layers).
**Done when.** Tables complete for every native error.
**Tests.** Host test that mappings are total.
**Pitfalls.** Keep values stable.
**Refs.** POSIX errno list.

### SYS-05 — Process and thread syscalls
- Phase: P1 · Size: M · Status: todo
- Depends: SYS-03, PROC-01, THR-02
- Files: kernel/core/sys_proc.c

**Goal.** `process_spawn`, `process_exit`, `process_wait`, `process_kill`,
`thread_create`, `thread_exit`, `thread_join`, `yield`, `sleep`,
`getpid/tid`, credentials queries.
**Done when.** A multi-threaded test program creates 64 threads and joins
them.
**Tests.** Live test programs.
**Pitfalls.** Exit of the last thread = process exit; reap order.
**Refs.** —

### SYS-06 — Memory syscalls
- Phase: P1 · Size: S · Status: todo
- Depends: SYS-03, MEM-08, MEM-11
- Files: kernel/core/sys_mem.c

**Goal.** `vm_map(handle|anon, addr hint, size, prot, flags)`, `vm_unmap`,
`vm_protect`, `vmo_create`, `vmo_resize`, `vm_query`.
**Done when.** libc `malloc` (USR-01) runs on them.
**Tests.** Live tests incl. W^X violation attempts.
**Pitfalls.** Refuse W+X mappings unless entitled (JITs, SEC-21).
**Refs.** —

### SYS-07 — File and directory syscalls
- Phase: P2 · Size: M · Status: todo
- Depends: SYS-03, FS-01
- Files: kernel/core/sys_file.c

**Goal.** File descriptors as handles: `open/openat`, `read/write`,
`pread/pwrite`, `seek`, `stat/fstat`, `readdir`, `mkdir`, `unlink`,
`rename`, `truncate`, `fsync`, `chmod/chown`, `link/symlink/readlink`,
`fcntl`, `ioctl` (restricted), current directory per process.
**Done when.** Coreutils (USR-10) pass their tests.
**Tests.** Live coreutils test script.
**Pitfalls.** Path resolution with `..` and symlinks across mounts
(TOCTOU); permission checks on every component.
**Refs.** POSIX.1-2017 System Interfaces.

### SYS-08 — Waiting on many objects
- Phase: P2 · Size: M · Status: todo
- Depends: SYS-02, SCH-04
- Files: kernel/core/wait.c

**Goal.** `object_wait_many(handles[], signals[], timeout)` and an
event-queue object (kqueue/epoll-like) for servers with many clients;
`poll/select` in libc built on top.
**Done when.** WindowServer waits on 100 client ports + input + timers
with one call.
**Tests.** Live microbenchmark with 1,000 handles.
**Pitfalls.** Spurious wakeups; level vs edge semantics documented.
**Refs.** —

### SYS-09 — System information calls
- Phase: P2 · Size: S · Status: todo
- Depends: SYS-03
- Files: kernel/core/sys_info.c

**Goal.** `sysinfo(kind, buf)`: version, uptime, CPU list, memory stats,
load, hostname, boot time; replaces direct kernel reads by the UI.
**Done when.** About window and Activity Monitor use it.
**Tests.** Live test.
**Pitfalls.** Versioned structs (size field first).
**Refs.** —

### SYS-10 — Remove the legacy `syscall_dispatch()`
- Phase: P2 · Size: S · Status: todo
- Depends: SYS-07, PROC-10
- Files: kernel/src/proc.c, proc.h, Terminal/Activity Monitor callers

**Context.** AGENTS.md: never expose it; it uses raw kernel pointers.
**Goal.** Delete it and the fake 13-call table; callers use real APIs.
**Done when.** `grep syscall_dispatch` finds nothing.
**Tests.** Full suites.
**Pitfalls.** Terminal's `ps/top/kill/heap` must keep working via SYS-09/PROC-09.
**Refs.** —

### SYS-11 — Syscall tracer (`ostrace`)
- Phase: P2 · Size: S · Status: todo
- Depends: SYS-03, KDB-05
- Files: kernel/core/trace.c, tools in /System/Library/Tools/ostrace

**Goal.** `ostrace <program>` prints every syscall with decoded args and
results (like `strace`/`dtruss`).
**Done when.** Tracing `ls` shows the expected calls.
**Tests.** Live test comparing traced output to a golden file.
**Pitfalls.** Tracing must require the same user or admin.
**Refs.** —

---

## Processes and executables

### PROC-01 — Process object and lifecycle
- Phase: P1 · Size: M · Status: todo
- Depends: MEM-08, THR-01, SYS-02
- Files: kernel/core/process.c (replaces proc.c bookkeeping)

**Goal.** Process = vm_space + handle table + threads + credentials +
parent/children + exit status + resource usage. States: running,
exiting, zombie. `process_wait` reaps.
**Done when.** Process table in Activity Monitor shows real processes.
**Tests.** Live test spawning/killing 200 processes without leaks.
**Pitfalls.** Orphans reparent to `ojjyd` (PID 1).
**Refs.** —

### PROC-02 — Spawn API with argv/env/auxv
- Phase: P1 · Size: M · Status: todo
- Depends: PROC-01, ELF-01, ELF-02
- Files: kernel/core/spawn.c

**Goal.** `process_spawn(path, argv, envp, handles_to_inherit, flags)` —
the native way to start programs (no fork needed); file actions for
stdin/out/err.
**Done when.** Terminal starts `hello.elf` with arguments and captures
output through a pipe.
**Tests.** Live test.
**Pitfalls.** Limit argv/env total size (e.g. 256 KiB).
**Refs.** POSIX `posix_spawn`.

### PROC-03 — `fork` and `exec`
- Phase: P2 · Size: M · Status: todo
- Depends: MEM-10, PROC-02
- Files: kernel/core/fork.c, exec.c

**Goal.** POSIX `fork` (COW), `execve` replacing the image; needed by
shells and Linux programs.
**Done when.** `ojsh` runs pipelines using fork/exec; Linux personality
reuses it.
**Tests.** Live fork-bomb-with-limit test, exec of missing files.
**Pitfalls.** Multi-threaded fork copies only the calling thread.
**Refs.** POSIX.

### PROC-04 — Credentials and identities
- Phase: P1 · Size: S · Status: todo
- Depends: PROC-01
- Files: kernel/security/cred.c, kernel/src/users.c

**Goal.** uid/gid/supplementary groups/effective IDs per process, derived
from `users.c` accounts at login; admin group `admin` (gid 80 like macOS).
**Done when.** Files created by a process carry its uid (with FS-01/ojjyFS).
**Tests.** Host tests; live login test.
**Pitfalls.** The UI login flow must spawn the user session with the
user's credentials (SVC-04).
**Refs.** —

### PROC-05 — Process groups, sessions, terminals
- Phase: P2 · Size: M · Status: todo
- Depends: PROC-03, DRV-TTY-01
- Files: kernel/core/session.c

**Goal.** Job control: foreground/background groups, Ctrl-C/Ctrl-Z to the
foreground group, `jobs/fg/bg` in the shell.
**Done when.** `sleep 100 &`, `fg`, Ctrl-C work in Terminal.
**Tests.** Live shell test script.
**Pitfalls.** SIGHUP on terminal close.
**Refs.** POSIX General Terminal Interface.

### PROC-06 — POSIX signals
- Phase: P2 · Size: M · Status: todo
- Depends: PROC-03, SYS-01
- Files: kernel/core/signal.c

**Goal.** Signal masks, handlers, `sigaction`, delivery on syscall/IRQ
return with a signal frame, `sigreturn`, default actions; native apps use
exception ports instead but libc/Linux need signals.
**Done when.** libc signal tests pass; Linux personality reuses the code.
**Tests.** Live tests.
**Pitfalls.** Interrupted syscalls (`EINTR`/restart); alternate stacks.
**Refs.** POSIX signals.

### PROC-07 — Resource limits
- Phase: P2 · Size: S · Status: todo
- Depends: PROC-01
- Files: kernel/core/rlimit.c

**Goal.** Max memory, open handles, threads, processes per user, CPU time;
`setrlimit`; defaults chosen so a runaway app cannot freeze the system.
**Done when.** A fork bomb is stopped by the per-user process limit.
**Tests.** Live test.
**Pitfalls.** WindowServer and services need higher limits.
**Refs.** —

### PROC-08 — Crash handling and reports
- Phase: P2 · Size: M · Status: todo
- Depends: IRQ-05, SVC-20
- Files: kernel/core/exception.c, services/crashreporterd

**Goal.** On crash: capture registers, backtrace (frame pointers),
loaded images; deliver to an exception port (debuggers) else to
`crashreporterd`, which writes `~/Library/Logs/Crashes/*.crash` and shows
"App quit unexpectedly — Reopen / Report".
**Done when.** Crashing a test app shows the dialog with a symbolicated
backtrace.
**Tests.** Live test with `crash.elf`.
**Pitfalls.** Don't include secrets (no memory dumps by default).
**Refs.** —

### PROC-09 — Process inspection API
- Phase: P2 · Size: S · Status: todo
- Depends: PROC-01, SCH-07
- Files: kernel/core/procinfo.c, /proc view (FS-12)

**Goal.** List processes/threads with name, user, state, CPU, memory,
handles, start time; per-process details for Activity Monitor and `ps/top`.
**Done when.** `ps aux`-like output matches Activity Monitor.
**Tests.** Live test.
**Pitfalls.** Permission: other users' processes show limited fields.
**Refs.** —

### PROC-10 — Replace the bounded EL0 runner with real processes
- Phase: P1 · Size: M · Status: todo
- Depends: PROC-02, SCH-02, IRQ-05, ELF-03
- Files: kernel/src/user.c, user_image.c, programs/, Terminal `run`

**Context.** `PROTECTED_APPS.md`: one synchronous program, 100 ms budget.
**Goal.** `run hello` spawns a process; programs run concurrently with the
desktop; the 100 ms budget is replaced by preemption + CPU-time limits;
all 19 test programs keep their expected outcomes.
**Done when.** `run spin` no longer needs a time budget to keep the UI
alive; protected suite green on ARM and **x86** (ELF-03).
**Tests.** Protected suite (ARM + x86).
**Pitfalls.** Keep the boundary rules from `PROTECTED_APPS.md` (clean
registers, no FP leakage).
**Refs.** PROTECTED_APPS.md.

### ELF-01 — Kernel ELF loader (static and PIE)
- Phase: P1 · Size: M · Status: todo
- Depends: MEM-08
- Files: kernel/core/elf.c (from user_image.c)

**Goal.** Validate and map ELF64 executables (ET_EXEC and ET_DYN/PIE) for
the current arch: segments with correct permissions, BSS zeroing, entry,
ASLR base for PIE, refusal of W+X segments.
**Done when.** All test programs load through it; malformed headers
rejected.
**Tests.** Existing 4000-mutation fuzz from `test_user.c`, extended to PIE.
**Pitfalls.** Overlapping segments, huge `p_memsz`, integer overflow.
**Refs.** ELF-64 Object File Format; System V gABI.

### ELF-02 — Initial stack and auxiliary vector
- Phase: P1 · Size: S · Status: todo
- Depends: ELF-01
- Files: kernel/core/elf.c

**Goal.** Standard initial stack: argc, argv, envp, auxv (AT_PHDR,
AT_PHNUM, AT_ENTRY, AT_BASE, AT_PAGESZ, AT_RANDOM, AT_HWCAP, AT_SYSINFO_EHDR,
AT_EXECFN), 16-byte aligned; random bytes for stack protectors.
**Done when.** A test program prints its argv/env/auxv correctly.
**Tests.** Live test.
**Pitfalls.** Alignment differences between arches.
**Refs.** System V psABI (AArch64, x86-64).

### ELF-03 — x86_64 user mode (ring 3)
- Phase: P1 · Size: M · Status: todo
- Depends: SYS-01, ELF-01, MEM-08
- Files: kernel/src/arch/x86_64/user.c, gdt.c, idt.c

**Context.** Intel reports "protected execution unsupported".
**Goal.** Ring 3 code/data segments, TSS with RSP0, syscall MSRs,
per-thread FS base, page-table switching — parity with ARM.
**Done when.** The 19 protected programs (x86 builds) pass on x86 QEMU.
**Tests.** Protected suite with `--arch x86_64`.
**Pitfalls.** IST stacks for double fault/NMI; SMEP/SMAP enabling.
**Refs.** Intel SDM Vol 3 ch. 5–7.

### ELF-04 — Interpreter support (`PT_INTERP`)
- Phase: P2 · Size: S · Status: todo
- Depends: ELF-02
- Files: kernel/core/elf.c

**Goal.** Load the requested interpreter (dynamic linker) as well as the
program and start at the interpreter with AT_BASE.
**Done when.** A dynamically linked hello world starts (with ELF-05).
**Tests.** Live test.
**Pitfalls.** Interpreter path must be on a trusted volume.
**Refs.** gABI.

### ELF-05 — Dynamic linker `ld-ojjy.so`
- Phase: P2 · Size: L · Status: todo
- Depends: ELF-04, USR-01, THR-04
- Files: lib/ld/

**Goal.** User-space dynamic linker: load `DT_NEEDED` libraries (search
paths from the bundle and `/System/Library/Libraries`), relocations
(RELATIVE, GLOB_DAT, JUMP_SLOT, ABS64, TLS relocs), symbol lookup with
GNU hash, lazy binding optional, `dlopen/dlsym/dlclose`, init/fini arrays,
RELRO.
**Done when.** libc and ojjyKit ship as shared libraries; a GUI app loads
in < 50 ms.
**Tests.** Live tests with multi-library programs; symbol interposition.
**Pitfalls.** Initialization order; TLS for dlopened libraries.
**Refs.** gABI dynamic linking; AArch64 ELF ABI relocations.

### ELF-06 — Library ABI and versioning policy
- Phase: P2 · Size: S · Status: todo
- Depends: ELF-05
- Files: docs/abi/libraries.md

**Goal.** Rules for sonames, symbol visibility (hidden by default), ABI
compatibility checks (a tool comparing exported symbols between builds),
deprecation process.
**Done when.** `tools/abicheck.py` runs in the test suite.
**Tests.** abicheck on libc/libojjy.
**Pitfalls.** —
**Refs.** —

### ELF-07 — Executable policy hook (signatures, quarantine, personalities)
- Phase: P2 · Size: S · Status: todo
- Depends: ELF-01, PROC-02
- Files: kernel/core/exec_policy.c

**Goal.** One place in `exec`/`spawn` that decides whether and how a file
may run: native vs Linux personality (LNX-01), signature/identity check
results (SEC-20 fills it in later; until then "unsigned, allowed"),
quarantine flag consultation (SEC-22 asks `appregd`), sandbox profile
attachment (SEC-21), and a clear error for unsupported formats (PE files
→ "Open with ojjyWin").
**Done when.** All program starts go through the hook; policy decisions
are logged at debug level.
**Tests.** Live tests for allowed/denied cases.
**Pitfalls.** Keep it fast: cache decisions per file identity.
**Refs.** D-014.

---

## Inter-process communication

### IPC-01 — Ports and messages
- Phase: P1 · Size: M · Status: todo
- Depends: SYS-02, SCH-04
- Files: kernel/core/port.c

**Goal.** `port_create`, `port_send(port, bytes ≤ 64 KiB, handles ≤ 16)`,
`port_receive(port, timeout)`, bounded queues with backpressure, peer
closed notifications.
**Done when.** Two processes exchange 1 M small messages without loss.
**Tests.** Live ping-pong benchmark (latency < 3 µs HVF).
**Pitfalls.** Queue limits must not let a client exhaust server memory.
**Refs.** D-005.

### IPC-02 — Handle transfer
- Phase: P1 · Size: S · Status: todo
- Depends: IPC-01
- Files: kernel/core/port.c

**Goal.** Messages move or duplicate handles (with reduced rights) into
the receiver's table atomically.
**Done when.** A client passes a vm_object handle to a server.
**Tests.** Live test incl. rights reduction.
**Pitfalls.** Handles in undelivered messages must be closed on port death.
**Refs.** —

### IPC-03 — Synchronous call/reply fast path
- Phase: P2 · Size: M · Status: todo
- Depends: IPC-01, SCH-03
- Files: kernel/core/port.c, sched.c

**Goal.** `port_call(port, request, reply_buffer)` blocking with direct
thread handoff (scheduler switches straight to the server thread).
**Done when.** Round trip < 2 µs under HVF.
**Tests.** Microbenchmark.
**Pitfalls.** Priority inheritance through calls.
**Refs.** L4 IPC design papers (Liedtke).

### IPC-04 — Bootstrap registry (service names)
- Phase: P2 · Size: S · Status: todo
- Depends: IPC-02, PROC-02
- Files: services/ojjyd/registry.c

**Goal.** Services register names (`ojjy.windowserver`), clients look them
up; on-demand launch when a name is requested but not running.
**Done when.** Apps find WindowServer through the registry.
**Tests.** Live test of on-demand launch.
**Pitfalls.** Name squatting: only `ojjyd`-launched processes can register
system names.
**Refs.** launchd concepts.

### IPC-05 — Shared memory through IPC
- Phase: P1 · Size: S · Status: todo
- Depends: IPC-02, MEM-11
- Files: lib/libojjy/shm.c

**Goal.** Library helpers to create/send/map shared buffers with fences
for producer/consumer (used by surfaces and audio).
**Done when.** WindowServer prototype maps a client surface.
**Tests.** Live test.
**Pitfalls.** Size changes must be negotiated, never resized under a mapper.
**Refs.** —

### IPC-06 — Event objects and async notifications
- Phase: P2 · Size: S · Status: todo
- Depends: IPC-01, SYS-08
- Files: kernel/core/event.c

**Goal.** Signalable event objects (manual/auto reset), port "signal"
messages (no payload), timers as objects.
**Done when.** ojjyKit run loop waits on ports + events + timers.
**Tests.** Live test.
**Pitfalls.** Coalescing semantics documented.
**Refs.** —

### IPC-07 — Interface definition language `ojidl`
- Phase: P2 · Size: M · Status: todo
- Depends: IPC-03
- Files: tools/ojidl/, docs/abi/ojidl.md

**Goal.** Small IDL (interfaces, methods, typed args, handles, arrays) and
a generator emitting C client/server stubs with versioning and input
validation, so every service protocol is typed and fuzzable.
**Done when.** WindowServer and notifyd protocols are generated.
**Tests.** Generator tests; generated decoders fuzzed on the host.
**Pitfalls.** Generated decoders must validate lengths before use.
**Refs.** —

### IPC-08 — Pipes and local sockets
- Phase: P2 · Size: S · Status: todo
- Depends: SYS-07, IPC-01
- Files: kernel/core/pipe.c, kernel/net/unix.c

**Goal.** Anonymous pipes, named FIFOs, UNIX-domain stream/datagram
sockets with `SCM_RIGHTS`-like handle passing (for POSIX/Linux code).
**Done when.** Shell pipelines and a UNIX socket echo test pass.
**Tests.** Live tests.
**Pitfalls.** `SIGPIPE`/`EPIPE` behaviour.
**Refs.** POSIX.

### IPC-09 — IPC performance budget
- Phase: P2 · Size: S · Status: todo
- Depends: IPC-03
- Files: tests/qemu_ipc_bench.py

**Goal.** Benchmarks for message latency/throughput, handle transfer,
shared memory, tracked over releases with thresholds.
**Done when.** Bench runs in the perf suite and fails on 20% regressions.
**Tests.** The bench itself.
**Pitfalls.** Run on HVF, quiet host.
**Refs.** —

---

## Kernel debugging and diagnostics

### KDB-01 — Kernel log ring buffer
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: kernel/src/serial_core.c → kernel/core/log.c

**Goal.** Levels (error/warn/info/debug), subsystem tags, timestamps, a
256 KiB ring readable by `logd` and a `dmesg` command; serial output stays.
**Done when.** `dmesg` shows boot logs; `log=` option filters.
**Tests.** Host test of the ring; live `dmesg` check.
**Pitfalls.** Logging from IRQ context must not block.
**Refs.** —

### KDB-02 — Better panics (backtrace, persisted panic log)
- Phase: P1 · Size: S · Status: todo
- Depends: KDB-01
- Files: kernel/src/panic.c

**Goal.** Frame-pointer backtrace with symbol names (symbol table embedded
at build time), registers, recent log lines; written to a reserved
sector so the next boot shows "Your computer restarted because of a
problem" with details.
**Done when.** A forced panic (`kdebug panic`, debug builds) shows the
report after reboot.
**Tests.** Live test.
**Pitfalls.** Panic path must not allocate or take locks.
**Refs.** —

### KDB-03 — Remote debugger stub
- Phase: P2 · Size: M · Status: todo
- Depends: KDB-01, DRV-VIRTIO-08
- Files: kernel/core/gdbstub.c

**Goal.** GDB remote protocol over virtio-console/serial: read/write
memory and registers, breakpoints, single step — usable under VZ where
QEMU's built-in stub is unavailable.
**Done when.** `lldb`/`gdb` attaches to a VZ guest kernel and sets a
breakpoint.
**Tests.** Scripted gdb session in QEMU.
**Pitfalls.** Must be disabled in release builds.
**Refs.** GDB Remote Serial Protocol docs.

### KDB-04 — Debug and release build configurations
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: kernel/Makefile, scripts/build.sh

**Goal.** `OJJY_DEBUG=1` builds enable assertions, lock checking, slab
poisoning, extra logging; release builds strip them. Tests run both.
**Done when.** Both configurations build with 0 warnings.
**Tests.** CI script builds both.
**Pitfalls.** Assertions must have no side effects.
**Refs.** —

### KDB-05 — Tracing and sampling profiler
- Phase: P2 · Size: M · Status: todo
- Depends: KDB-01, SCH-07
- Files: kernel/core/trace.c, tools/ojprof

**Goal.** Static trace points (scheduler, syscalls, IRQs, I/O, frames)
into per-CPU ring buffers; a sampling profiler (timer-driven PC samples
for kernel and user); export to Chrome trace JSON for viewing on the host.
**Done when.** A trace of a window drag can be opened in a trace viewer
and shows frame stages.
**Tests.** Live test that captures a trace and validates the JSON.
**Pitfalls.** Overhead < 2% when enabled, ~0 when disabled.
**Refs.** Chrome Trace Event Format.

### KDB-06 — Kernel leak and corruption detection
- Phase: P2 · Size: M · Status: todo
- Depends: MEM-06, KDB-04
- Files: kernel/mm/kdebug.c

**Goal.** Allocation tracking with call sites in debug builds; leak report
command; shadow-memory checking of slab red zones (KASAN-lite).
**Done when.** A deliberately leaked object is reported with its call site.
**Tests.** Live debug-build test.
**Pitfalls.** Memory overhead in debug only.
**Refs.** —

### KDB-07 — Watchdog and hang detection
- Phase: P2 · Size: S · Status: todo
- Depends: SCH-02
- Files: kernel/core/watchdog.c

**Goal.** Detect threads stuck in the kernel > 10 s and CPUs with
interrupts off > 1 s; log backtraces; WindowServer heartbeat → restart
if hung (SVC-01).
**Done when.** An injected hang is reported with a backtrace.
**Tests.** Debug-build live test.
**Pitfalls.** False positives during long legitimate operations (fsck).
**Refs.** —

### KDB-08 — Loadable kernel modules (decision card)
- Phase: P5 · Size: M · Status: todo
- Depends: ELF-05, SEC-20
- Files: docs, kernel/core/module.c

**Goal.** Decide (decision record) whether drivers can be loaded at
runtime as signed ELF relocatable modules; if yes, implement loading with
symbol export lists and signature checks. Default: drivers built in.
**Done when.** Decision recorded; implementation if accepted.
**Tests.** Module load/unload test if implemented.
**Pitfalls.** Stable in-kernel ABI is costly; prefer built-in drivers.
**Refs.** D-001.
