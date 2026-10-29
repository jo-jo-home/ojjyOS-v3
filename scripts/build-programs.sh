#!/bin/bash
# Build static ARM64 EL0 samples without a host runtime. Created by Jonas Lee.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/programs"
mkdir -p "$OUT" "$ROOT/sysroot/System/Programs"
LD="$(command -v ld.lld || true)"
if [ -z "$LD" ]; then
    for candidate in /opt/homebrew/opt/lld/bin/ld.lld /opt/homebrew/opt/llvm/bin/ld.lld /usr/local/opt/lld/bin/ld.lld; do
        if [ -x "$candidate" ]; then LD="$candidate"; break; fi
    done
fi
if [ -z "$LD" ]; then echo "ELF linker ld.lld required" >&2; exit 1; fi
names=(hello kernel_read kernel_write code_write stack_exec null privileged fp spin badptr syscall_limit output_limit badsvc guard wfi mmio kernel_exec badsp alias_read alias_write alias_exec)
for i in "${!names[@]}"; do
    name="${names[$i]}"
    clang --target=aarch64-unknown-none-elf -ffreestanding -fno-builtin \
      -fno-stack-protector -fno-pic -fno-pie -mgeneral-regs-only -O2 \
      -Wall -Wextra -Wno-unused-function -Wno-unused-variable -DCASE="$i" \
      -I"$ROOT/kernel/src" -c "$ROOT/programs/sample.c" -o "$OUT/$name.o"
    "$LD" -T "$ROOT/programs/linker.ld" -nostdlib -static -z max-page-size=4096 \
      "$OUT/$name.o" -o "$ROOT/sysroot/System/Programs/$name.elf"
done
clang --target=aarch64-unknown-none-elf -I"$ROOT/kernel/src" \
  -c "$ROOT/programs/registers.S" -o "$OUT/registers.o"
"$LD" -T "$ROOT/programs/linker.ld" -nostdlib -static -z max-page-size=4096 \
  "$OUT/registers.o" -o "$ROOT/sysroot/System/Programs/registers.elf"
