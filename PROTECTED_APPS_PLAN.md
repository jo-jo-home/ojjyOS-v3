# Protected execution milestone (ARM64 first)

Author: Jonas Lee. Resumed after the paused handoff at `c85b2ff`.

## Contract and scope

Run one bounded ELF64 program at ARM64 EL0, returning safely on exit, fault,
time limit or syscall limit. The existing desktop remains kernel-hosted. This
is not preemptive multitasking, hardened multi-user authorization, bare-metal
Mac support, or an Intel ring-3 implementation. Preserve saved disk layouts.

## Execution plan

1. **Memory boundary.** Own ARM64 4 KiB-granule translation tables. Preserve
   supervisor-only identity mappings of the supported QEMU platform and map
   app pages separately above 4 GiB. Code is user RX, data/stack user RW+NX;
   privileged execution of user mappings is prohibited. Guard stack boundaries.
2. **Loader and copying.** Validate static little-endian AArch64 ELF64 before
   allocation. Bound file size, segment count and page count; reject arithmetic
   overflow, page overlaps, invalid entry, W+X, interpreters, dynamic loading and
   TLS. Copy through supervisor physical aliases, never unchecked user pointers.
3. **Trap ABI.** Use SVC #0, syscall number x8, arguments x0..x2, return x0.
   Permit only PID/UID/uptime, captured stdout/stderr and exit. Never expose the
   old raw-pointer kernel dispatcher. Save x0..x30, SP_EL0, ELR/SPSR and fault
   registers on a private EL1 stack. Restore kernel stack and translation state.
4. **Resource limits.** 64 private pages including four stack pages, 128 KiB ELF,
   eight program headers, 512 bytes per write, 4095 bytes captured output,
   256 syscalls, 100 ms execution budget. Hardware timer interrupts terminate
   infinite loops. No arbitrary files, heap pointers, devices or kill API.
5. **Launch and samples.** Authenticated `run <name>` from console and Terminal;
   installed samples under `/System/Programs`. Capture output and report status,
   PID, fault details and page cleanup. Intel returns an honest unsupported error.
6. **Evidence.** Host tests for loader/range/cleanup/syscall policy; live tests
   for normal exit, BSS, bad pointers, kernel read/write, code write, NX stack,
   null/privileged/FP faults, timeout and repeated clean runs. Follow each fault
   with a successful program. Re-run boot/input/persistence on both architectures.

## Safety and validation

All hardware changes are validated in isolated QEMU disk copies first. Do not
reset saved data or kill unrelated VMs. Run normal and ASan/UBSan host tests.
Record limitations and actual receipts in AGENTS.md/HANDOFF.md before committing
locally. No remote push without Jonas explicitly asking.

## Status

**Completed and verified.** Both builds are warning-free; 1055 C assertions and
9 Python tests pass normally and under ASan/UBSan. Final ARM64 `--protected`
flow passes all 19 samples, repeated clean runs, Terminal launch and the full
three-boot storage/input suite. Intel's final full regression passes with an
explicit unsupported result. Evidence: `build/protected-runtime-arm-final.log`,
`build/protected-runtime-intel-final-v2.log` and `HANDOFF.md`. No remote push.

The checkpoints below are historical progress, not pending final work.

### Checkpoint 1

Owned mappings, ELF loader, EL0 trap/entry, safe bounded calls and launch commands
are wired into both builds (Intel stub explicitly unsupported). Normal/sanitized
host tests pass. ARM64 baseline three-boot smoke and Intel full capability/smoke
flow pass. Real hello and invalid-pointer app run/reap correctly. Register test
needed a longer delay to stay below the syscall cap; the full ARM64 matrix and
Terminal recovery test remain pending. See `build/protected-*.log`.

### Checkpoint 3

Full ARM64 matrix and Terminal recovery passed, including 19 distinct samples,
repeated clean runs and the complete input/persistence/cold-boot/fallback flow.
`build/protected-runtime-arm-v3.log` and its screenshot directory are the receipt.
Final Intel rebuild/recheck, documentation cleanup and local commit remain.
