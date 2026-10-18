# Bounded ARM64 protected apps

Created by **Jonas Lee**. This is the first privilege-boundary milestone, not a
multitasking scheduler. Existing GUI apps still execute inside the kernel.

## Run

Build normally with `ARCH=aarch64 bash scripts/build.sh`, boot and log in. In
Terminal or the fallback console (`Shift+Esc`), enter:

```text
run hello
run badptr
run registers
run kernel_read
run spin
run hello
```

Programs are separate ELF64 files built from `programs/` by
`scripts/build-programs.sh` and packed under `/System/Programs`. Only names
containing lowercase letters, digits, `_` or `-` are accepted (32 characters
maximum). Launch requires an authenticated session. Intel builds retain boot,
desktop and storage, but explicitly reject `run` as unsupported.

The runner captures output and reports `exited`, `fault`, `timeout` or `limit`,
the PID and exit code. Serial logs additionally report fault PC/FAR/ESR and
physical free memory before/after cleanup. FAR is meaningful only for exception
classes that provide it; do not interpret stale FAR on a system-instruction trap.

## Memory and traps

- Own 4 KiB-granule EL1 tables, installed after the physical allocator reserves
  the kernel. Supported platform: QEMU `virt`, GICv2, one vCPU, TCG, physical
  addresses below 4 GiB. This does not add native Apple hardware/HVF support.
- Supervisor identity mapping: device memory below `0x40000000`, normal RAM
  from there to 4 GiB. All are EL0-inaccessible. Kernel RAM mappings remain
  coarse RWX at EL1; this is not kernel W^X hardening.
- User image VA range `[0x100010000, 0x100100000)`, private physical backing.
  Readable RX code and R/RW non-executable data; W+X is rejected. User mappings
  are privileged-execute-never. Four stack pages immediately below
  `0x1001ff000`, with unmapped space above and below.
- Entry at EL0t with clean integer registers/NZCV/thread pointers and a private,
  aligned user stack. Exceptions use an EL1-only 16 KiB stack, never SP_EL0.
  Full x0..x30, SP_EL0, ELR/SPSR, ESR/FAR are saved. FP/SIMD is unsupported
  and traps at EL0, so no FP state is exposed or lazily shared.
- User faults end the app; kernel-origin faults remain fatal. IRQ termination
  always acknowledges/re-arms/EOIs the timer before returning. Root switches
  invalidate TLBs; executable loads clean data cache/invalidate instruction cache.
- Return to kernel tables before zeroing/freeing private pages and reaping PID.
  Six static table pages and the exception stack remain reserved kernel BSS;
  they are reused rather than allocated per launch.

## ELF contract

Static ELF64, little-endian, AArch64 (`e_machine=183`), ET_EXEC, ELF flags zero.
Exact ELF/program header sizes, at most eight program headers, 128 KiB file and
64 private pages (including the four stack pages). Bounds are checked using
subtraction before addition; segment page ranges cannot overlap. Entry must be
instruction-aligned and inside file-backed executable bytes. Physical and virtual
segment addresses must match the link address, not the backing physical pages.
Reject dynamic/interpreter/TLS segments, executable GNU_STACK, invalid alignment,
unknown load permission bits, W+X, unreadable load segments and empty load segments.
Section tables are unused, not trusted or dereferenced. BSS and page padding are
zero-filled. Allocation failure unwinds every allocated page.

## ABI v1

`SVC #0`, number in x8, arguments x0..x2, signed result in x0. All other GPRs
are preserved. See `kernel/src/user_abi.h`; this is **not** the old kernel
`syscall_dispatch()` interface. No libc, TLS, dynamic linker or filesystem API.

| Number | Call | Arguments/result |
|---|---|---|
| 1 | getpid | Current runner PID |
| 2 | getuid | Authenticated launch UID |
| 3 | uptime | Guest physical-counter milliseconds |
| 6 | write | x0=1/2 (stdout/stderr), x1=user pointer, x2=length |
| 14 | exit | x0=signed 32-bit exit code, never returns |

Write validates the entire range before copying via supervisor physical aliases;
it never dereferences untrusted virtual addresses in EL1. Zero-length writes
ignore the pointer. Maximum 512 bytes per call, 4095 captured bytes total.
Nonprintable bytes except newline/tab become `?`; stdout/stderr share one buffer.
Errors: `-9` bad descriptor, `-14` invalid/unmapped range, `-22` invalid argument,
`-28` output buffer full, `-38` unsupported syscall. Unsupported calls cannot
reach raw kernel filesystem handles, allocation, sync or kill functions.

256 syscalls are allowed; attempt 257 ends the task with `limit`/`-125`. The
100 ms guest physical-counter budget is checked on IRQ and syscall entry;
an infinite EL0 loop is interrupted and ends with `timeout`/`-110`. Hardware
faults report `fault`/`-11` rather than a Unix signal implementation. This
synchronous runner pauses desktop event handling during the bounded run; it
does not promise a hard host-time latency bound or concurrent app/UI execution.

## Testing

```bash
bash scripts/test.sh
HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 bash scripts/test.sh
python3 -B tests/qemu_smoke.py --arch aarch64 \
  --image build/aarch64/ojjyos3.img --output build/new-protected-check \
  --fresh-data --protected
```

Always choose a new private output directory. `tests/test_user.c` exercises the
real validator, page cleanup and syscall policy through host memory/platform
boundaries, including 4000 deterministic malformed-header mutations. It does
not emulate EL0/MMU/ERET; those require the live probes. The live matrix checks
register preservation, invalid calls, output limits, kernel/device access,
kernel/stack execution, code write, null/guard/SP faults, privileged/FP/SVC/WFI
traps, timer/syscall limits and repeated clean runs. It also launches through
Terminal and reruns input/persistence/cold-boot/checkpoint-recovery flows.

Pending beyond this milestone: concurrent address spaces, scheduling, IPC,
filesystem authorization, password hardening, GUI userspace conversion, Intel
ring-3 and broader hardware support. This educational boundary is not a claim
of security certification or defense against microarchitectural side channels.

### Verification receipt (2026-10-17)

Both complete builds: zero warnings/errors. Host suites: 1055 C assertions
(206 new user tests) and 9 Python tests pass normally and with ASan/UBSan.
Final live ARM64 run passes 19 distinct samples and repeated clean launches;
spin terminates at 101 ms without a syscall, register probe spans timer IRQs,
and Terminal remains usable afterward. Intel's full regression suite passes
with protected execution explicitly unsupported. Both pass saved-login and
corrupted-checkpoint recovery. Evidence and saved-disk hashes: `HANDOFF.md`.
