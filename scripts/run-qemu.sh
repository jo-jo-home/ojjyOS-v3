#!/bin/bash
# ojjyOS QEMU launcher. Created by Jonas Lee.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$ROOT_DIR/scripts/qemu_launch.py" "$@"
