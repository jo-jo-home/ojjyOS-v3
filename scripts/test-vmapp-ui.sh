#!/bin/bash
# AppKit-only launch-window checks/captures, no VM. Created by Jonas Lee.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OJJY_VMAPP_UI_TEST_DIR:-$ROOT/build/hw-12-ui}"
mkdir -p "$OUT"
swiftc -O -warnings-as-errors -D OJJY_VMAPP_UI_TEST \
    "$ROOT/vmapp/LaunchSettings.swift" "$ROOT/vmapp/LaunchWindow.swift" "$ROOT/vmapp/main.swift" \
    "$ROOT/tests/test_launch_window.swift" -framework Virtualization -framework AppKit -o "$OUT/test-launch-ui"
for appearance in light dark; do
    "$OUT/test-launch-ui" --capture-launch "$OUT/$appearance.png" --appearance "$appearance"
done
