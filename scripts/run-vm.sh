#!/bin/bash
# Native ARM64 host. QEMU remains available via run-qemu.sh. Created by Jonas Lee.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="${OJJY_VMAPP_DIR:-$ROOT/build/ojjyOS.app}"
if [ "$(uname -m)" != arm64 ]; then
    echo "The native host requires Apple Silicon. Use scripts/run-qemu.sh on Intel." >&2
    exit 1
fi
if [ ! -x "$APP/Contents/MacOS/ojjyOS" ] || [ "$ROOT/vmapp/main.swift" -nt "$APP/Contents/MacOS/ojjyOS" ]; then
    bash "$ROOT/scripts/build-vmapp.sh"
fi
open -n "$APP" --args "$@"
