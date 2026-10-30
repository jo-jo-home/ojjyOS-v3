# HW-14: one launch settings file

Created by Jonas Lee, 2026-10-30.

The native menu, QEMU runner and ojjy Control use the OS checkout's
`build/launch-settings.json`. There is no Control database preference or
QEMU-specific persistent copy. Version 1 has `memoryMB`, `cpus`, `screen`,
`network`, `fullScreen`, `showAtStart`; the defaults remain
2048, 1, `1440x900`, true, false, true. Unknown JSON keys survive saves.
Unsupported versions are readable as safe defaults but cannot be saved over.
Files larger than 64 KiB cannot be saved over. Malformed files can be
explicitly replaced with validated settings; reads never repair user files.

Memory choices are 2048/3072/4096/6144/8192 MiB, bounded by half host RAM
and, for native launches, VZ's memory bounds. Nearest choice wins, ties
choose the lower value. No available choice means launch/save is refused.
CPU remains 1; Intel networking remains unsupported.

## Precedence and screen units

Defaults < validated file < explicit launch overrides. Native `--memory`
and `--screen` keep their nonpersistent meaning. QEMU accepts
`OJJY_MEMORY_MB`, `OJJY_SCREEN`, `OJJY_NETWORK`, `OJJY_FULLSCREEN`;
legacy `OJJY_RES` is a physical scanout override and wins over the logical
screen. Overrides are validated, never written back. `OJJY_ACCEL` selects
auto/HVF/TCG independently of the file. Invalid explicit overrides fail
instead of silently launching a different configuration.

`screen` is a logical size: 1440x900, 1280x800 or 1600x900.
Native VZ and ARM HVF use 2x scanout; ARM TCG uses 1x for performance.
Legacy `OJJY_DISPLAY=ramfb` keeps its fixed 1024x768 fallback, explicitly
reported rather than pretending to apply a saved screen. Intel uses std
VGA at the logical size, one TCG CPU, and explicitly disables networking.
The runner passes a bounded `opt/ojjyOS/display` fw_cfg item to the Intel
bootloader; it selects the matching BGRx GOP mode before its legacy
1920x1080 fallback. A host-tested parser checks the QEMU signature,
bounded big-endian file directory, exact key and size grammar. The Intel
bootloader touches fw_cfg ports only when CPUID reports a hypervisor.
`fullScreen` controls the host display, not guest pixels. Linux uses GTK;
macOS uses Cocoa, with zoom-to-fit and a single guest pointer.

## Concurrency and safety

Writers acquire `build/.launch-settings.lock` using exclusive directory
creation and fail fast if busy. Never steal a lock: after an interrupted
writer, verify no writer is active before manually removing it. Under the
lock, compare the current file's SHA-256 (or `missing`) against the revision
read by the editor. A stale editor must reload before saving. Merge known
fields into the current JSON object, retain unknown fields, write an
exclusive 0600 temporary file in the same directory, flush, then rename.
Release the lock in all normal error paths. Refuse symlink/nonregular
targets. Native reloads each time its settings window opens; an already
open editor cannot silently overwrite Control's save.

QEMU only reads settings. `OJJY_LAUNCH_SETTINGS`, `OJJY_BUILD_DIR`,
`OJJY_IMAGE`, `OJJY_SERIAL_LOG`, `OJJY_FIRMWARE` select private test paths.
`--dry-run` reports JSON argv/effective settings without starting QEMU or
creating firmware/logs. No shell evaluation of settings or overrides.
Live tests must use private image/firmware/log copies and preserve the
installed settings/disks. A running user VM prevents installed delivery.
