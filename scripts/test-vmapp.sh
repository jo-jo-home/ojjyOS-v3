#!/bin/bash
# Native host guards, without starting a VM. Created by Jonas Lee.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OJJY_VMAPP_TEST_DIR:-$ROOT/build/tests-vmapp}"
mkdir -p "$OUT"
if grep -En 'temporaryMemoryNote|2\.2 GB|until its memory update' "$ROOT/vmapp/LaunchWindow.swift"; then
    echo "FAIL: obsolete low-memory warning remains in the launch menu" >&2
    exit 1
fi
swiftc -O -warnings-as-errors -D OJJY_VMAPP_TEST "$ROOT/vmapp/LaunchSettings.swift" "$ROOT/vmapp/LaunchWindow.swift" "$ROOT/vmapp/main.swift" "$ROOT/tests/test_vmapp.swift" \
    -framework Virtualization -framework AppKit -o "$OUT/test-vmapp"
"$OUT/test-vmapp" "$OUT"
python3 -B -m unittest discover -s "$ROOT/tests" -p test_vz_capture.py
