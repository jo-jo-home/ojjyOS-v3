# ojjyOS v3: working context and milestone receipts

Captured: **2026-10-17**. Read this together with **AGENTS.md**.

## Session records (final write of this session, 2026-10-17 ~21:15)

- **`JONAS_MESSAGES.md`** — Jonas's 42 messages to the AI, verbatim, in
  order, extracted from the 9.7 MB session export he provided in the repo
  root (`ojjyos-v3-bugs-macos-features-custom-kernel.json`, intentionally
  uncommitted). Read it for requirements history and tone.
- HEAD at this write: `8cc8d5a` (bug/improvement pass — file-copy data loss,
  Finder Cmd+C, stale listings, forward history, username validation; see
  `IMPROVEMENTS.md`). Cursor/UI fix: `e649535` (receipts in
  `CURSOR_UI_PLAN.md`). Protected apps: `ef92f96` (`PROTECTED_APPS.md`).
  Main is 17 commits ahead of origin/main; **nothing pushed**.
- Multiple AI sessions edited this repo concurrently today; re-check git
  state before trusting any single handoff file, including this one.
- User-level skill `qemu-kernel-counter-probe` captures the live-counter
  verification technique (llvm-nm + QMP pmemsave) used throughout.

## DONE: cursor and UI request (see CURSOR_UI_PLAN.md receipts)

After `ef92f96`, Jonas first requested an OS status update, then reported the
guest pointer is offset from the host pointer and laggy, and requested UI
cleanup after the pointer fix. Scheduler design was only being considered;
no implementation edits were made. Latest task is cursor first, visuals second.
Read `CURSOR_UI_PLAN.md`. Reproduction on the unmodified image: injected guest
target (100,100) renders cursor at (512,582), target (200,150) at (712,682),
returning across an edge drifts to (0,0). Evidence `build/cursor-before/`.
Virtio tablet absolute values are converted into deltas, then compositor applies
mouse-speed gain. Full-scene repaint on every movement is also on the lag path.
The user's Cocoa QEMU is running against `build/aarch64/ojjyos3.img`; don't stop
it or replace its locked disk. Use isolated build/test candidates. Scope is
pointer pipeline, framebuffer overlay, UI cleanup, tests and associated docs.

## FINAL STATE: protected execution milestone complete (2026-10-17)

The user resumed; do not obey the old paused snapshot as a current stop request.
The bounded ARM64 EL0 milestone is implemented and verified. Read
`PROTECTED_APPS.md` for the actual ABI, mappings, limits and commands. Existing
GUI apps remain kernel-hosted, and Intel execution is explicitly unsupported.
No scheduling/GUI conversion/HVF/fullscreen/account-hardening claims are made.

Final verification after the last loader change:
- Both complete builds passed with zero warnings/errors.
- 1055 C assertions (849 baseline + 206 new) and 9 Python image tests pass,
  normally and with compatible Homebrew LLVM ASan/UBSan. The user test includes
  4000 deterministic malformed-header mutations, plus tampered-plan/OOM cleanup.
- `build/protected-runtime-arm-final.log`: all 19 distinct programs pass,
  correct fault classes, equal PMM free counts, successful runs after faults,
  12 extra hello runs, Terminal hello after Spotlight launch, full original
  input/persistence/rebuild/cold-boot/checksum-fallback suite. Spin stops in
  101 ms with zero syscalls, so its termination proves real timer interruption.
- `build/protected-runtime-intel-final-v2.log`: honest unsupported result and
  complete three-boot regression pass. Tests used isolated copies only.
- Screenshot: `build/protected-runtime-arm-final/desktop-after-app-faults.png`.
- Build/test receipts: `build/protected-build-arm.log`,
  `build/protected-build-intel.log`, `build/protected-host-tests.log`,
  `build/protected-sanitized-tests.log`.
- `build/protected-data-preservation.log` verifies both final saved-data partitions
  match full pre-rebuild backups (SHA-256
  `2910f9e463394546790a465e60ae9abde773a0820966a5452bb91b38d6602f3f`).
  Legacy whole-image SHA-256 is still
  `1594a5d572485d4a4df474a2a5fcfbfa0af7f33f3076289e01d23e04c6e72e24`.

Commit locally as Jonas; never push unasked. See current git log/status for the
commit and any newer work. No test VM is intentionally left running. Next likely
milestone is a planned scheduler/concurrent-task design, not pretending this
synchronous runner already supplies one. Continue updating repo context as Jonas
requested; preserve the evidence/limitations distinction used in this pass.

## Historical progress checkpoints (superseded by final state above)

This update supersedes the paused stopping point below. Jonas said **"continue"**
and later **"remember to update context as u go"**. The old sections are retained
as history, not a prohibition on the currently requested implementation.

The detailed scope is `PROTECTED_APPS_PLAN.md`. New files currently implement a
bounded ARM64-only EL0 runner, not a concurrent scheduler. `user.c` owns launch
and safe syscalls; `user_image.c` validates ELF and handles private pages/copying;
`arch/aarch64/user_arch.c` owns mappings and fault/watchdog handling;
`user_entry.S` saves full integer state on a private exception stack. FP/SIMD
traps deliberately. `programs/` contains real separately built ELFs. Console and
Terminal expose authenticated `run <name>` under `/System/Programs`.

Verification so far:
- Full builds for ARM64 and Intel passed; normal and sanitized host suites passed.
- ARM64's full original smoke flow passes with owned supervisor mappings.
- Live `hello` and `badptr` successfully execute at EL0 and release every page.
- Initial register test reached 257 syscalls at 8 ms instead of waiting 10 ms;
  its test busy wait was increased. Not an observed kernel register corruption.
- Intel passes the full three-boot input/storage/recovery flow and honestly reports
  protected execution unsupported. Saved source disks remain separate from tests.

Next: rerun ARM64 with `--protected`, finish all fault/timeout/register/Terminal
checks, inspect failures rather than weakening assertions, review memory and trap
boundaries, rerun both builds + host/sanitized tests, finalize docs and commit
locally as Jonas. No push. Evidence is `build/protected-*.log` and matching private
test directories. The old `2443b0a` remains last committed checkpoint for now;
inspect `git status` to see all active work.

Checkpoint 2: ARM64 register preservation and output-budget tests now pass.
Kernel read/write, code write, stack execute and null access fault safely and
each is followed by a successful hello. `privileged` also faulted safely; the
test incorrectly expected EC=0 (undefined instruction) for MSR DAIFSet. Actual
ESR `0x620793fe` reports EC=0x18 (trapped system-register access), the proper
classification. Updated that expectation, not the kernel fault policy.
Added MMIO access, kernel execution and user-SP pivot fault probes, header
mutation coverage and cleared user thread-pointer state. Still awaiting the
complete matrix; do not claim it passed based on these partial runs.

Checkpoint 3: **full ARM64 matrix passed** in
`build/protected-runtime-arm-v3/`, with transcript
`build/protected-runtime-arm-v3.log`. All 19 distinct samples passed expected
outcomes: hello, invalid-pointer/forged-call rejection, full-GPR/IRQ preservation,
output bound, kernel read/write/execute, code write, NX stack, null/guard,
privileged MSR, FP, nonzero SVC, WFI, MMIO, kernel-address SP pivot, infinite
loop timeout and syscall cap. Every primary run had identical PMM free memory
before/after; hello succeeds after each fault plus 12 extra repetitions.
Terminal launched through Spotlight and ran hello successfully; screenshot
`desktop-after-app-faults.png` visually confirms output and usable blue desktop.
The same test then passed sustained input, sync/shutdown, private rebuild,
cold-boot password login and corrupted-newest-slot recovery. Normal/sanitized
host suites pass with 4000 malformed-header mutations. Final Intel rebuild and
final documentation/review/commit remain; nothing pushed.

Final-review note: removed `memcmp` over `UserImage` structs in favor of field
comparisons and canonical revalidated metadata. C struct assignment does not
guarantee padding-byte preservation; although all observed runs passed, loader
correctness must not depend on it. Added a tampered-plan regression. Both final
builds/host suites/live flows will be rerun for this last code change. Intel's
prior final smoke run also passed before this narrow loader-only change.

## Historical paused snapshot begins here (not current state)

## 1. Exact stopping point at the earlier pause

Jonas said:

> pause. and write all context to the project. like a new ai can know whats happening

**Implementation is paused.** The completed reliability pass is committed as
`bac1dec` (`Fix kernel reliability: preserve disks, correct virtqueues, recover checkpoints`).
After Jonas said "alright continue", the assistant announced protected application
execution as the next kernel-first milestone and read the relevant code. Jonas
then paused the work. **No protected-app code, MMU changes, ELF app loader,
scheduler, syscall entry, or app binaries have been implemented in that follow-up.**

At the pause, the working tree was clean on `main`, 13 commits ahead of the local
`origin/main` tracking ref. The handoff may have its own later documentation-only
commit. Do not confuse that with another kernel implementation. No push was made.
The remote tracking ref was not refreshed during this work.

No new builds or runtime tests were needed just to save this handoff. Verification
below comes from the completed reliability pass, not a new test run at the pause.

## 2. Project, user, and non-negotiable requirements

- Project: **ojjyOS v3**, a hobby OS by **Jonas Lee** (`ojjy`, GitHub `jo-jo-home`).
- Repository: `https://github.com/jo-jo-home/ojjyOS-v3`.
- Actual git root on this machine:
  `~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3`.
- The parent `ojjyOS-kernel` is a workspace directory, **not** the git root.
- Keep the custom kernel. Do not substitute a Linux/BSD/macOS base. Such systems
  can be build hosts only. Preserve third-party licenses, including the font OFL.
- Credit **Jonas Lee** in README, About, boot/system information and other existing
  attribution. Do not insert AI co-author lines into user-facing branding.
- Preserve the blue, macOS Tahoe-inspired desktop. Reliability precedes polish.
- Priority order: **reliable boot -> input -> persistent storage -> protected apps
  -> dependable desktop -> visual polish**.
- Hardware: an **M2 Pro Mac**. ARM64 is the primary guest target. Keep Intel/AMD
  compatibility, but do not imply x86 VirtualBox guests run on Apple Silicon.
  QEMU TCG can emulate Intel guests on this Mac and was used successfully.
- For large tasks, Jonas likes an extensive written plan followed by autonomous
  execution and a precise summary, without repeated check-ins.
- Local commits as Jonas are authorized and expected. **Never push until explicitly
  told to.** Do not merge, reset, or discard other people's work.
- Latest instruction is to pause, not to implement the proposed next milestone.

## 3. What currently runs

The project boots its own UEFI-loaded kernel into a software-rendered desktop.
There are 11 built-in apps, accounts/login/setup, writable RAMFS mounts, disk
checkpoints, a heap allocator, and process/syscall bookkeeping.

### ARM64 runtime

- `qemu-system-aarch64`, QEMU `virt`, GICv2, one vCPU, TCG emulation.
- PL011 serial, PL031 RTC, ARM generic timer, ramfb, virtio-mmio keyboard/tablet/block.
- Kernel linked at `0x41000000`; guest RAM starts at `0x40000000`.
- Display: virtio-gpu at **1440x900** by default (ramfb 1024x768 still works with `OJJY_DISPLAY=ramfb`).
- The kernel still relies on UEFI's ARM64 mappings. It does not own a complete
  protected userspace mapping scheme yet.
- This is **not bare-metal Apple Silicon support**, and the runner does **not** use HVF.

### Intel runtime

- QEMU `pc`, UEFI GOP, GDT/IDT, identity paging, PIC/PIT, PS/2 input, ATA/IDE, ACPI.
- Kernel linked at **`0x04000000` (64 MiB)**, not the old 1 MiB address.
- Current tested GOP mode: **1920x1080**.
- Homebrew's split code/vars firmware is combined into a private writable pflash
  image by the runner; the split code file alone is not a valid `-bios` image.

### Important limits

The app windows run inside the kernel. `proc_spawn()` creates a record, not an
independent executable task. `proc_tick()` estimates ticks from focus/elapsed time;
it is not scheduler CPU measurement. The four "daemons" are bookkeeping entries.
`syscall_dispatch()` is an ordinary kernel function, not a safe user/kernel ABI.
There is no proven app crash isolation or protected EL0/ring-3 execution.

Accounts are functional login records, but home-directory separation is not file
authorization. Passwords use one fast salted SHA-256 operation with a small salt
derived from clocks/username/counter, not a strong password KDF with secure entropy.
Some account-management and fallback console paths have insufficient authorization.
Do not call this a hardened multi-user security boundary.

## 4. Completed reliability pass (`bac1dec`)

### 4.1 Safe rebuilding and architecture-specific disks

- `scripts/build.sh` now produces `build/aarch64/` or `build/x86_64/` outputs and
  refuses concurrent full builds because intermediate objects are shared.
- `scripts/mkimg.sh` assembles a separate candidate instead of truncating the live disk.
- `tools/finalize_image.py` validates expected GPT checksums/layout, refuses a locked
  source disk, saves a full backup, copies the entire data partition, fsyncs, and
  atomically replaces the destination.
- On the first per-architecture build, saved data is imported from legacy
  `build/ojjyos3.img` if present. That legacy image remains untouched.
- Existing architecture-specific data takes precedence over legacy data on later builds.
- Backups remain under `build/<arch>/backups/`. Do not remove them automatically.
- VirtualBox launch no longer regenerates an existing VDI or force-stops a running VM.
  A VDI has its own saved data; a raw image rebuild does not update an existing VDI.

**Older scripts or instructions that recreate `build/ojjyos3.img` are dangerous.**
Before this pass, rebuilding could erase accounts and documents.

### 4.2 Confirmed input defects

The most important discovery was **negotiated virtqueue depth vs backing capacity**:

1. The static guest storage held 128 descriptors.
2. This QEMU's input devices offered a maximum of 64 entries.
3. The old code negotiated 64 but wrapped available/used positions using 128.
4. After a ring wrap, the guest could read stale used entries and descriptor IDs.

The queue now retains its negotiated size, and every ring mask/allocation limit
uses that size. Input harvests are bounded; device-written indices are reloaded
with barriers. A temporary fault variant restoring the old capacity-based masks
fails the regression test; corrected code passes depths 8/64/128 and uint16 rollover.
Simply rejecting all queues smaller than 128 was tried and rejected: it removed
keyboard/tablet devices from the guest entirely.

Another confirmed defect was raw evdev `KEY_*` macros shadowing internal `KeyCode`
enum names. Examples before correction: Home 102 vs expected 73, Up 103 vs 74,
Delete 111 vs 85, with `KEY_MAX` 91. The conflicting macros were removed.

Shared input now tracks left/right modifiers independently. Duplicate downs become
editing repeats after a 450 ms delay, at least 35 ms apart. Enter and shortcuts do
not repeatedly submit/toggle. Pointer moves coalesce; transitions have reserved
capacity, and overflow is counted instead of producing a serial log flood.
PS/2 decoding is host-testable, including extended keys and the full Pause sequence.

**Correction to earlier handoffs:** host Cocoa autorepeat was previously described
as a proven cause of Jonas's repeated `o`/Delete/freeze reports. That was premature.
Also, the driver already read `events[desc_id]`, not always slot zero. Main-loop
events are batched before drawing; `mark_dirty()` does not render once per event.
Do not restore the old ignore-all-repeats proposal or claim every possible manual
focus-loss freeze has been reproduced and eliminated.

### 4.3 Boot, rendering and shutdown

- Standalone Intel UEFI uses Microsoft's ABI, while the ELF kernel expects SysV.
  `KernelEntry` now explicitly uses `sysv_abi` so BootInfo arrives in RDI, not RCX.
- The old low-address Intel BSS spanned OVMF ACPI NVS around 8-9 MiB and boot-services
  allocations. A live memory-map diagnostic confirmed the conflict. Relinking at
  64 MiB avoids the tested reserved ranges; allocation failures report overlaps.
- `update_animations()` now returns its boolean `changed` result. Previously it
  did not return at all despite documentation claiming zero warnings.
- Missing-return warnings now fail the kernel build.
- Re-entering the UI restores back buffering. Right-click marks the scene dirty.
- Panic paths draw directly to visible scanout, since a halted kernel cannot present
  its backbuffer. That change was inspected and built, not tested by forcing a panic.
- `[UI] Desktop presented` logs only after the login-to-desktop frame reaches scanout.
  Merely waiting for a credential-success log could capture a stale login screen.
- Intel S5 shutdown previously required AML BytePrefix and missed QEMU's actual
  ZeroOp values. A QMP dump showed `_S5_` as `08 5f 53 35 5f 12 06 04 00 00 00 00`.
  The bounded literal package parser now handles ZeroOp/OneOp, integer prefixes and
  multi-byte package lengths. It is not a full AML interpreter.

### 4.4 Recoverable persistence

- RAMFS `/Users` and `/Library` are snapshotted as flat instance bytes.
- Data partition begins at LBA **67584**. Two **8 MiB** slots begin at LBA **67584**
  and **83968** (`16384` sectors apart).
- Version 2 includes sizes/count, 64-bit generation, payload CRC-32 and header CRC-32.
- Saves write the inactive payload, flush, publish the commit header, then flush again.
- Restore validates in scratch first, checks RAMFS structure and checksums, and
  falls back to the older valid slot. Failed restore does not partially replace live RAMFS.
- Version 1 had **no checksum**, despite previous docs claiming one. It is accepted
  only after structural validation; first migration writes slot 1, retaining old slot 0.
- Autosave captures an immutable snapshot, then writes at most eight payload sectors
  per main-loop turn. Edits made after capture remain dirty. Failed saves remain dirty.
- Explicit sync/shutdown/restart wait for completion. Failed saves cancel power transitions.
- Virtio FLUSH is negotiated and issued; ATA exposes a checked flush and capacity.
- This is not hard real-time I/O: snapshot copy/CRC and individual block requests
  remain synchronous. CRC-32 is corruption detection, not authentication/encryption.
- RAMFS struct/layout changes need a deliberate persistence migration/version plan.
  Never silently wipe saves to accommodate a new struct.

## 5. Actual verification and evidence

Both full macOS builds completed with zero warnings/errors. Host tests passed:

| Test | Assertions/tests |
|---|---:|
| `tests/test_fs.c` | 37 |
| `tests/test_ramfs.c` | 81 |
| `tests/test_input.c` | 67 |
| `tests/test_persist.c` | 633 |
| `tests/test_virtqueue.c` | 10 |
| `tests/test_acpi_aml.c` | 21 |
| **C total** | **849** |
| `tests/test_image.py` | **9 Python tests** |

The host suite also passed with Homebrew LLVM ASan/UBSan. Apple clang 14's ASan
runtime aborts even on an empty `main` on this macOS; use Homebrew LLVM instead.
Sanitized tests prefix the freestanding libc symbols so they do not interpose on
sanitizer startup. Image tests include refusal to replace a disk locked by real QEMU.

Both delivered architectures passed a private-disk, three-boot live flow:

1. UI setup typing, erasing `oooo`, creating an account and logging in.
2. 256 `o`/Backspace cycles, console commands, sync and actual guest shutdown.
3. Rebuilding the private disk while preserving accounts.
4. Cold-boot authentication using the saved password.
5. Deliberately corrupting the newest checkpoint on the **private test disk**.
6. Booting from the older valid checkpoint, logging in and presenting the desktop.

Test account `tester` / `testpass` exists only in test copies. Do not confuse it with
Jonas's actual saved accounts. Tests stop only their own child processes.

Evidence directory on this machine (temporary; may eventually be cleaned):

`/private/var/folders/d1/1ykss2fs7_v0nx_r_8qd0_9c0000gn/T/opencode/ojjyos-reliability-build-3ujat0b1/`

Useful evidence:
- `deliver-build-aarch64.log`, `deliver-build-x86_64.log`
- `delivered-tests.log`, `tests-final-sanitized.log`
- `delivered-smoke-aarch64.log`, `delivered-smoke-x86_64.log`
- `vm-delivered-aarch64/`, `vm-delivered-x86_64/`: serial logs, PNGs, private disks/backups
- `ring-before.log`, `ring-after.log`: fault-injected red/green transport test
- `intel-map/`: actual OVMF allocation conflict
- `intel-aml/`: dumped ACPI tables and literal S5 bytes

Legacy `build/ojjyos3.img` was unchanged across delivery, verified by SHA-256:

`1594a5d572485d4a4df474a2a5fcfbfa0af7f33f3076289e01d23e04c6e72e24`

This is a historical before/after receipt, not a claim that a user can never change
the image later. Check current state before any future write.

Not revalidated: manual Cocoa focus-loss behavior, VirtualBox runtime, Linux GNU-EFI
builds, bare-metal hardware, HVF, or forced-live-panic display. No app isolation yet.

## 6. Build/run/test safely

Run from the actual git root. Quote paths containing spaces.

```bash
# Complete builds; first make sure the target VM is shut down.
ARCH=aarch64 bash scripts/build.sh
ARCH=x86_64 bash scripts/build.sh

# All host tests.
bash scripts/test.sh

# Instrumented host tests on this Mac.
HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 bash scripts/test.sh

# Normal ARM64 launch.
bash scripts/run-qemu.sh aarch64

# Normal Intel launch, or use the private headless tests below.
bash scripts/run-qemu.sh x86_64

# Fresh private test copy; choose a NEW output directory each time.
python3 -B tests/qemu_smoke.py --arch aarch64 \
  --image build/aarch64/ojjyos3.img --output build/smoke-next-arm --fresh-data
python3 -B tests/qemu_smoke.py --arch x86_64 \
  --image build/x86_64/ojjyos3.img --output build/smoke-next-intel --fresh-data
```

`--fresh-data` zeros ONLY the newly created private test copy, never the source.
The harness refuses to reuse an existing test disk. Its recovery step deliberately
damages the newest checkpoint on that private copy; never use such a test against
a user's live disk. Serial logs for normal runs are `build/<arch>/serial.log`.

The `VM` class in `tests/qemu_smoke.py` handles short private QMP sockets, greetings,
request IDs/events, screenshots, synthetic input and PID-specific cleanup. Extend
it rather than using broad `pkill`, shared `/tmp/qmp.sock`, or fixed RAM addresses.
Discover framebuffer addresses/pitch from each boot, or use QMP PNG screendumps.

## 7. Where the code investigation stopped

The follow-up read, but did not change:

- `kernel/src/proc.h`, `proc.c`: records, estimated accounting, raw kernel syscall dispatcher.
- `kernel/src/memory.h`: bitmap PMM and 512 KiB coalescing kernel heap.
- `kernel/src/main.c`: boot init order, console command parser and event loop.
- `kernel/Makefile`: architecture source selection and assembly setup.
- `kernel/src/arch/aarch64/entry.S`: boot privilege transition, vectors, IRQ save/restore.
- `kernel/src/arch/aarch64/arch64.c`: GIC IRQ dispatch and fatal EL1 exceptions.
- `kernel/src/arch/aarch64/timer_a64.c`: 1 kHz generic timer.
- `kernel/src/ui/compositor.c`: `terminal_exec()` and terminal-output helpers.

Relevant current behavior:

- ARM boot calls `pmm_init()` but retains firmware mappings; Intel calls `paging_init()`.
- ARM synchronous exception vectors currently lead to fatal kernel handling. There
  is no returning EL0 SVC path or per-user exception frame yet.
- ARM IRQ save/restore was designed around kernel execution and `-mgeneral-regs-only`.
  Introducing EL0 requires auditing full user register/state preservation.
- `syscall_dispatch()` accepts raw pointers/handles and calls VFS/kmalloc directly.
  **Never expose that dispatcher unchanged to untrusted user registers.**
- Terminal commands are in `terminal_exec()` around the `ps`, `kill`, `heap` cases.
  Console commands are in `main.c`'s `process_command()`.
- No protected runtime files, user program directory or app ELF build step have
  been added. Suggested names/designs below are proposals, not existing paths.

## 8. Proposed next milestone, NOT implemented or fully reviewed

Announced direction: start real protected application execution, ARM64 first,
retain Intel builds and current VM behavior. Keep fullscreen/HVF for later.
The following is a candidate plan for the next AI to review when Jonas resumes.

### Intended first outcome

A tiny freestanding app runs at ARM64 **EL0**, not as a function in the kernel.
It can print bounded output and exit through real `SVC` calls. Invalid memory
access, privileged instructions and infinite loops end that app and return to a
working kernel/desktop. Private pages and page tables are reclaimed afterward.

Do not claim a full multitasking desktop simply because one controlled app runs.
Existing GUI apps can remain kernel-hosted during this first milestone. If only
ARM64 execution is implemented, Intel must explicitly report unsupported rather
than pretending to run protected apps, while its build/boot regression tests pass.

### Candidate implementation pieces

1. **Own ARM64 translation tables.** Replace inherited firmware mappings carefully,
   keeping kernel code/stack/framebuffer/MMIO valid. Mark kernel/device mappings
   inaccessible to EL0. Audit TCR/MAIR/SCTLR, EL2->EL1 configuration and timer/GIC
   access; do not assume those firmware settings. A separate app virtual range
   above the current identity map was considered, but no address policy is final.
2. **Private app pages with permissions.** Use PMM pages, non-overlapping code/data,
   read-only executable code, writable non-executable data, and a guarded stack.
   No user access to kernel aliases, page tables or exception stacks. Handle TLB
   invalidation, instruction-cache coherency and teardown on every failure path.
3. **Validated static ELF64 loading.** Validate magic/class/endian/machine/type,
   exact header sizes, table bounds, all arithmetic, segment file/memory bounds,
   alignment, page overlap, resource limits and an executable entry. Reject dynamic
   loading, W+X and other unsupported features explicitly. Validate before allocation.
4. **Real user entry/traps.** Enter EL0 with clean registers and aligned private SP.
   Save full user registers, SP, PC, status and fault information on an EL1-only
   exception stack. Distinguish app faults from fatal kernel faults and restore the
   kernel caller safely on app exit. Do not skip the timer/GIC EOI on termination.
5. **Small safe syscall surface.** Initially allow only exit, bounded captured output,
   PID/UID and uptime. Use integer descriptors and checked copies; reject kernel
   pointers, unmapped/overflowing/cross-page ranges and unsupported calls. Do not
   expose raw VFS handles, kernel malloc/free, unrestricted kill or filesystem writes.
6. **Bound execution.** A timer watchdog and syscall/output budgets were considered
   so an infinite loop cannot monopolize the kernel. A one-app bounded runner is a
   possible first step, not a substitute for later preemptive scheduling. Choose and
   document limits after review; no timeout value has been committed.
7. **Usable test apps and launch command.** Build separate matching-architecture ELF
   samples, pack them into the system image, launch from console/Terminal, and report
   output, PID, exit/fault/timeout. Tie launch to an authenticated session. Do not let
   this accidentally expand current unimplemented file/account authorization.
8. **Host + live acceptance tests.** Malformed ELF rejection; permission/range-copy
   checks; OOM cleanup; valid print/exit; attempted kernel read/write; code-page write;
   stack execution; null access; privileged instruction; unknown/forged syscalls;
   infinite loop; repeated runs without page leaks; a valid run after each fault.
   Re-run the full boot/input/persistence suite for both architectures.

All architecture constants, API names, limits and sequencing above still need
validation. Do not present this design sketch as an implemented or approved ABI.

## 9. Other remaining work and known limitations

- Open. Resume protected-app architecture review only after Jonas asks to continue.
  The relevant entry/proc/memory/terminal source was inspected. (path read)
- Open. Run `bash scripts/test.sh` before future code changes; it previously exited 0
  with the counts above, but was not rerun merely for this pause. (path run)
- Open. Preserve target disk data through `scripts/build.sh` and
  `tools/finalize_image.py`; current writer code and successful migration were
  inspected/tested. Recheck actual target state before a new write. (code read)
- Open. Add protected-runtime modules and tests only after settling the design;
  their future paths are not created or finalized. (path assumed)
- Open. Add real context switching/scheduling after the minimal privilege boundary.
- Open. Enforce file/account authorization and use stronger password derivation.
- Open. Profile rendering/input/storage latency rather than promising hard bounds.
- Open. Investigate widescreen/fullscreen separately; ARM's 4:3 mode still letterboxes.
  The old claim that macOS Screen Sharing necessarily stretches away bars was not
  verified; no `--vnc` mode was added. (path read)
- Open. Investigate HVF as a separate tested platform change. The current runner is
  TCG; old near-native acceleration claims are incorrect. (path read)
- Open. Network stack, online Web.app, audio and GUI polish remain later priorities.

## 10. Miscellaneous handoff guidance

- Read **all** of AGENTS.md, but prefer its current-state section over historical
  session narratives. Older notes contain exaggerated completion/security claims.
- Do not mistake a stable screenshot or `Entering main loop` for functional input.
- Do not treat a search excerpt, passing mock, or agent agreement as a live test.
- Preserve checkpoints, font licensing, author credits and architecture-neutral APIs.
- A reusable local skill was extracted at
  `~/.claude/skills/virtio-negotiated-ring-depth/SKILL.md`. It may not exist on another
  machine; the essential discovery and verification are recorded above.
- GBrain was not pinned as a reliable code index for this worktree. Use local source
  searches, not an assumption that the shared brain contains the implementation.
- The full earlier session history remains in AGENTS.md and local git commits. The
  former Session-7 handoff `6f4a1f0` is useful history, not the current fix plan.
- When resuming, inspect `git status` and the actual source first. Do not overwrite
  changes a different AI or Jonas may have made after this snapshot.
