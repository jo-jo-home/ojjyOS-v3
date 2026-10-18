#!/bin/bash
# Native host guards, without starting a VM. Created by Jonas Lee.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OJJY_VMAPP_TEST_DIR:-$ROOT/build/tests-vmapp}"
mkdir -p "$OUT"
swiftc -O -warnings-as-errors -D OJJY_VMAPP_TEST "$ROOT/vmapp/main.swift" "$ROOT/tests/test_vmapp.swift" \
    -framework Virtualization -framework AppKit -o "$OUT/test-vmapp"
"$OUT/test-vmapp" "$OUT"
