# HW-13: measured memory limits and implementation decision

Created by Jonas Lee. Part 1, 2026-10-29. **Design, not implemented.**
Baseline: `043894c`; branch `hw-13`, worktree `ojjyOS-v3-hw-13`.
The HW-13 card owns the five-part checklist and handoff. Main must not
move until part 5. The existing launch memory warning remains until then.

## 1. Measurement, not an allocation claim

`build/hw-13-part1/memory-maps.txt` contains **every descriptor**, including
base, end-exclusive, byte/page count, numeric UEFI type, attributes, map
address/stride, exact initial PMM counts, PCI BARs and clock diagnostics.
`summary.json` contains log hashes and machine-readable results.

| Guest | Descriptors | Current total label, MiB | Initial free bytes | Initial free pages | Reclaimable above 4 GiB, MiB |
|---|---:|---:|---:|---:|---:|
| VZ 2048 MiB | 30 | 2045 | 2,126,475,264 | 519,159 | 0 |
| VZ 4096 MiB | 32 | 4093 | 2,397,491,200 | 585,325 | 1789.542969 |
| VZ 8192 MiB | 33 | 8189 | 2,397,491,200 | 585,325 | 5885.542969 |
| QEMU ARM HVF 4096 MiB | 34 | 4090 | 3,202,805,760 | 781,935 | 1018.414063 |
| QEMU Intel TCG 4096 MiB | 113 | 4089 | 3,195,793,408 | 780,223 | 1024 |

Counts are at the end of `pmm_init`, before GPU and app allocations.
High reclaimable means the sum of type 3/4/7 descriptor bytes above 4 GiB,
not a claim that reserved/loader/firmware pages may all be freed.
The serial label says MB but divides by 1048576: these measurements are MiB.
Minor kernel/firmware reservation changes will change exact future counts.

VZ DRAM spans `0x70000000..0xf0000000`, `..0x170000000`,
`..0x270000000` for 2/4/8 GiB. QEMU ARM spans
`0x40000000..0x140000000` at 4 GiB; Intel has a PCI hole and a
`0x100000000..0x140000000` high RAM range, not one contiguous low span.
The observed ARM kernel is `0x71000000..0x72190008`; use linker symbols,
never the obsolete `0x41000000` examples in older docs.

VZ virtio BAR0 addresses are:

| RAM | GPU | Block | Console | Network | RNG |
|---|---|---|---|---|---|
| 2 GiB | `0x100000000` | `0x100010000` | `0x100020000` | `0x100030000` | `0x100040000` |
| 4 GiB | `0x180000000` | `0x180010000` | `0x180020000` | `0x180030000` | `0x180040000` |
| 8 GiB | `0x280000000` | `0x280010000` | `0x280020000` | `0x280030000` | `0x280040000` |

MSI-X BARs and xHCI remain around `0x50000000..0x50006000`.
**These high PCI BARs are not listed as RAM or as high MMIO descriptors
in these UEFI maps.** Preserve the platform's Device mapping window outside
RAM; a page not listed as RAM must never become allocatable by inference.

All five guests reached the main loop; VZ devices, USB enumeration and DHCP
markers are present. QEMU checks were boot/map checks, not the full smoke.
`vz_boot.py` passed at 2 GiB. Its 4/8 GiB invocations **failed** the strict
RTC-equals-firmware-epoch-plus-uptime assertion after NTP applied a
correction before the health sample (BUG-1008). The maps are complete;
do not call those two full VZ tests green or disable NTP to hide the gap.

The probe added only raw descriptor/count serial logging to `memory.c`;
`probe.patch` retains that exact diff locally. It was removed afterwards.
Both probe builds had no warnings. All tests used private disk copies;
VZ's 4/8 GiB script exited before its final hash check, so source hashes
were additionally checked independently after all five probes.

## 2. Why raising MAX_MEMORY_GB is unsafe

`memory.c` has a 128 KiB BSS bitmap indexed by **physical address**, capped
at 4 GiB. It counts some high descriptors in `total_memory` but does not
free or allocate their pages. Allocation immediately zeroes a physical
address through an identity pointer. Enlarging the bitmap would therefore
dereference inaccessible or Device-mapped RAM, not fix the mapping.

ARM `user_arch.c` identity-maps low RAM as Normal, then maps 4–16 GiB as
Device for VZ BARs. `app_l1[4]` is replaced for `USER_BASE=0x100000000`;
simply making all high RAM identity-mapped would collide with that region.
`user_arch_prepare` rejects physical pages >= 4 GiB and performs cache
maintenance through a physical-address cast. `user_image.c` also casts
physical addresses during load, checked copy and destruction. All these
paths must change together before choosing high user frames.

Intel `paging.c` identity-maps 4 GiB and has a limited on-demand mapper.
Its walker does **not** allocate a missing final PT under a newly-created
PD; its `void paging_map` also cannot report failure. Do not use it as a
working general mapper without addressing those paths in part 4.
Intel protected execution is explicitly unsupported (`user_unsupported.c`);
HW-13 must not claim or introduce Intel ring 3.

## 3. Decision: explicit zones plus a small MEM-04 direct-map subset

Keep the bitmap allocator for this card; do not implement a buddy
allocator, COW/refcounts, general vmalloc, upper-half kernel relinking,
SMP or an Intel user runner. A temporary single-page window would suffice
for copying user frames, but not for stable browser/TLS pointers. Choose
a privileged, RAM-only direct alias with explicit opt-in allocations.
This is the smallest chosen design that supports both kinds of caller
without a new persistent buffer-window manager.

### Part 2: runtime metadata, no high callers yet

1. Validate descriptor stride >= `sizeof(EfiMemoryDescriptor)`, complete
   records, map bounds and arithmetic before iterating. Read packed fields
   with `memcpy`; reject overflowing ranges and overlapping descriptors
   rather than counting/freeing a page twice. Accept unsorted valid maps.
2. Use **per-RAM-region** metadata, not an array sized by the highest MMIO
   address: two bitmaps (eligible/allocated) plus region bounds, offsets,
   cursors and counts. Split regions at 4 GiB. Metadata sizes derive from
   descriptor count and RAM pages, with checked arithmetic and a clear
   failure if bootstrap low RAM cannot hold them. Two bits per 64 GiB of
   pages cost about 4 MiB, independent of holes; no large BSS arrays.
3. Before PMM is ready, find conventional **low** pages for that metadata
   through a bounded raw-map scan. Exclude low 1 MiB, actual kernel image,
   original BootInfo/map storage, framebuffer, firmware/runtime regions
   and the metadata's own extent. Copy the descriptor array into this
   reserved low arena and repoint the owned BootInfo to it before changing
   ARM mappings: at 4/8 GiB its original pointer is already **above 4 GiB**
   (`0x16e514018` / `0x26e525018`). Keep all loader allocations reserved;
   reclaim only types 3/4/7 after ExitBootServices, never runtime-attributed
   memory. Preserve ACPI storage until its consumers permit reclamation.
4. Provisional API names: `pmm_alloc_page_zone(PMM_LOW|PMM_HIGH|PMM_ANY)`
   returns a **physical** address; corresponding contiguous allocation
   takes pages and zone. LOW never returns >= 4 GiB, HIGH never falls back,
   ANY prefers high and may fall back to low. The old `pmm_alloc_page`,
   `pmm_alloc_contiguous` and `pmm_alloc_bytes` remain LOW wrappers.
   Reject unaligned, reserved, non-owned, double, overflow and hole frees.
   Use per-region next-fit cursors so a large stress allocation does not
   rescan from page zero for every page.
5. High tracking exists in part 2 but high allocation stays disabled until
   the architecture publishes successful high-map readiness. Failure to
   build mappings must not silently enable high allocation. Count total
   **firmware-described RAM** (union of RAM types 1–10, not highest PA or
   requested launch RAM), managed capacity after exclusions, active free
   low/high, and reserved/allocated separately. Emit
   `[PMM] <total> MB RAM, <usable> MB usable`, with low/high free and active
   state on a second line; getters/documentation must define these units.
   The usable-capacity figure is not the same thing as current free bytes.

### Parts 3/4: safe CPU access to high frames

- Reserve `KPHYS_BASE=0xffff800000000000`, consistent with D-003. For known
  managed RAM, `phys_to_virt(pa)` returns the low identity pointer below
  4 GiB and `KPHYS_BASE+pa` above it, only after readiness. A checked
  `virt_to_phys` reverses those two known aliases; arbitrary pointers,
  Device addresses and PA outside CPU capabilities are refused. Every
  high zero/copy/wipe/cache-maintenance operation uses the alias, not PA.
- Build privileged WB Normal RAM aliases from validated RAM intervals.
  Use 2 MiB blocks only wholly covered by compatible RAM, 4 KiB leaves at
  boundaries; no rounding a hole, reserved Device range or BAR into RAM.
  Allocate intermediate tables with LOW PMM and reserve/count them.
  Map only managed RAM here, not firmware runtime regions or arbitrary
  types 0/11/12. No additional cacheable alias for MMIO.
- ARM: enable TTBR1 with 48-bit kernel VA, existing MAIR Normal attributes,
  valid CPU-derived IPS and privileged RW/PXN/UXN descriptors. TTBR1 is
  shared unchanged across kernel/app TTBR0 switches. Retain existing low
  identity aliases for kernel/DMA. Split the current 4–16 GiB Device
  window around actual high RAM: leave RAM identity slots unmapped, keep
  the non-RAM BAR window Device, and never create conflicting Normal/Device
  aliases of allocated RAM. Respect break-before-make and barrier/TLBI
  ordering; build new tables before activating them.
- ARM user PTEs still use the same user VAs, W^X, guarded stack and checked
  syscall ranges. Validate physical ownership, alignment, RAM membership
  and actual PA width instead of the old 4 GiB cutoff. Map only frames
  owned by that UserSpace; use the privileged alias for ELF load,
  `user_copy_from`, destruction/wipe and `dc cvau` before execution.
  TTBR1 aliases must fault at EL0. Preserve free-before/free-after cleanup,
  cache/TLB coherency, timeout, output bounds and return to kernel tables.
- Intel: add supervisor upper-PML4 mappings for the same alias without
  changing CR3's low identity region. Allocate every intermediate table,
  propagate map failures and use explicit PA masks. Enable/check NXE
  before setting NX; no USER bits at any direct-map level. Detect CPU PA
  width, reject impossible maps, and leave ring-3 unsupported. Device/GOP
  mappings retain their existing separate path and attributes.
- Add `pmm_alloc_mapped_bytes(bytes)` for CPU-only, physically contiguous
  buffers: ANY physical allocation + checked alias, zero through alias,
  return a stable **virtual pointer**. Do not silently change old APIs.
  High mappings must be published before enabling this function's high
  preference; on failure release any allocation and return NULL.

### Allocation caller audit (all current call sites)

| Callers | Policy for this series | Why |
|---|---|---|
| `paging.c:walk_to_pte` and new mapping tables | LOW | CPU walker currently dereferences table PA; avoid recursive mapper bootstrap |
| `arch/aarch64/xhci.c`: host, TRB rings, contexts, reports/data, DCBAA, ERST, events, scratchpad pointers/pages | LOW | DMA-visible pointer-as-physical contract; no DMA API conversion here |
| `arch/aarch64/virtio_console.c:console_buf` | LOW | device consumes the address directly |
| `arch/aarch64/virtio_gpu.c:pixels`, `BootInfo.fb_addr` | LOW | resource backing published as DMA PA; fb_addr is physical |
| `framebuffer.c:fb_back` | LOW in HW-13 | retain renderer/scanout contracts; audit separately before changing |
| `ui/compositor.c`: wp_cache/wp_blur/wp_glass, jpeg_buf, wallpaper destinations, glass_scratch, ctl_pool/ctl_black/ctl_white, wp_thumbs/jpeg, pv_src, layer_buf/ghost_buf | LOW in HW-13 | no implicit migration of long-lived renderer pointers |
| `ui/browser.c`: 40/28/20 MiB arenas and 48 MiB decode scratch | mapped ANY in part 3 | large CPU-only spans, stable alias; verify arena/image pointer arithmetic |
| `web/http.c:tls_pool` | mapped ANY in part 3 | CPU-only TlsSlot, network TX copies to low driver buffers |
| `ui/browser.c`: tab buf/sbuf/fetch, history scratch, Browser/WebItem/WebDoc/ResSlot, img_pool, per-resource buffers, zbuf | LOW in HW-13 | not needed for the initial high-RAM path; no blanket allocator replacement |
| `ui/browser_profile.c`: serialization buffer, Profile, wbuf | LOW in HW-13 | preserve existing VFS lifetime contracts |
| `ui/netcmd.c:curl_buf` | LOW in HW-13 | preserve existing command buffer contract |
| `user_image.c:add_page` | physical ANY in part 3 | pages have explicit user PTEs; change all physical dereference sites together |

Virtio rings, blk/RNG/input/network queue storage and packet buffers mostly
live in low kernel BSS, not PMM allocations. `virtio.c` publishes ring
pointers as physical addresses. `virtio_blk.c` bounces data via its own low
sector buffer; `virtio_net.c` copies TX frames into low BSS buffers. Keep
these contracts; a high TLS/browser **pointer** must never be placed in a
device descriptor without an explicit translation/DMA policy.
The heap's existing 512 KiB BSS arena is not enlarged or replaced here.

## 4. Relationship to existing roadmap work

- MEM-01 stays todo: runtime bitmap zones are not a buddy allocator and do
  not complete browser close/free or split/merge work.
- MEM-02 stays todo: eligibility/ownership bits are not the page-frame
  database/refcounts/owner objects requested there.
- MEM-03's **HW13-A / HW13-X** preparation boxes own reusable descriptor,
  address validation and failure-safe mapping groundwork. HW-13 references
  them; tick the same named box in both cards only after its actual tests.
  Full arch-neutral `pt_*` API/refactor and framebuffer WC remain future.
- MEM-04's **HW13-A / HW13-X** boxes own the RAM-only alias and checked
  translation helpers. General vmalloc and conversion of every allocation
  are not done. Its full MEM-03 dependency is not waived by preparatory work.
- MEM-05 stays todo: the image still executes low/identity-mapped; adding
  a kernel RAM alias does not relink it or remove its identity map.

## 5. Proof required in later parts

Host fake maps: sparse holes, unsorted/many descriptors, overlaps and
overflow rejection, invalid/truncated strides, >4 GiB, 64 GiB, bootstrap
metadata exhaustion, kernel/map/framebuffer/runtime reservations, aligned
and exact-end allocations, LOW/HIGH/ANY behavior, bad/double frees, stats,
and random alloc/free invariants under ASan/UBSan. Simulate high RAM on the
host via translated backing storage, never dereference a made-up PA.

ARM private VZ 4/8 GiB and QEMU HVF 4 GiB must log actual high PAs, write,
read, free and run protected pages from them. Add EL0 attacks on the kernel
alias, read-only/executable permissions, guard/MMIO checks and cleanup
equality. A boot or a larger printed total alone proves none of this.
Fix BUG-1008 with a deterministic host regression for the clock assertion;
never skip a clock gate or tolerate arbitrary drift.

Part 4 must **simultaneously own >2.5 GiB** (target 3 GiB) on a guest with
enough RAM. Use bounded runtime bookkeeping for blocks, touch/check a
PA-derived pattern in every page, report peak low/high bytes and high PA
bounds, free everything, compare free counts and prove recovery by boot/UI
or protected hello afterwards. Reusing one page 786432 times, merely
printing the requested size, or host-side physical writes is not proof.
Use the mapped allocator on Intel; do not pretend an Intel EL0 test passed.

Normal/sanitized hosts, both zero-warning dev builds, ARM TCG/HVF/Intel
smokes, VZ regression and affected browser/TLS/UI tours precede completion.
Part 5 compares About/Monitor real statistics against serial; removes
`LaunchWindow.swift:temporaryMemoryNote` only then, reviews screenshots,
fast-forwards main only after combined-source tests and safely delivers
with stopped VMs and before/after saved-data/settings hashes.

## 6. Reproduction and handoff

Always start with `export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"`.
`scripts/mkfs.sh` once in a new worktree, then `scripts/test.sh` (the default
system LibreSSL lacks the TLS fixture certificate date flags).

The ignored `build/hw-13-part1/` retains `probe.patch`, `summarize.py`,
`measure-qemu.py`, both probe images, private disks, per-boot serial logs,
VZ reports where the full check passed, and all build/host logs. To repeat
measurements, apply only the retained logging patch in an owned clean
worktree, build fresh probe images, and run:

```sh
python3 -B tests/vz_boot.py --image build/hw-13-part1/probe-arm/ojjyos3.img \
  --output build/hw-13-part1/vz-4096-new --memory 4096 --seconds 12
# Repeat separately with 2048 and 8192 so one failed clock gate cannot skip a case.
python3 -B build/hw-13-part1/measure-qemu.py
# Local QEMU script substitutes -m 4096 in the existing VM helper,
# retains exact command.json files, and only starts/stops its own child VMs.
```

Use new output directories; the local QEMU helper's existing paths must
also be renamed when rerunning. Remove the logging patch before the final
host/build checks. The raw evidence is intentionally ignored, not shipped
in git; this document preserves the results needed by another machine.
