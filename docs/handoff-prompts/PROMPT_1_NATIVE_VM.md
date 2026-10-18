You are taking over an unfinished job on ojjyOS v3, a hobby operating system written from scratch by Jonas Lee. You have no prior context; everything you need is in the repository. Work fully autonomously (do not ask questions), and finish the job end to end.

PROJECT LOCATION
~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3   (Apple M2 Pro Mac, macOS 26.6.2, Swift 5.8 with CommandLineTools SDK 13.3, QEMU 11.1.2 from Homebrew, Homebrew LLVM at /opt/homebrew/opt/llvm)

THE GOAL
Jonas finds QEMU laggy and wants ojjyOS to "feel smooth like a native VirtualBox Windows aarch64 VM". Replace QEMU (for everyday use on Apple Silicon) with our own Mac app, build/ojjyOS.app, built on Apple's Virtualization.framework (VZ): a normal macOS window showing the OS through VZVirtualMachineView, native full screen, Retina sharpness, smooth USB keyboard and pointer. QEMU must keep working because all automated tests use it.

STEP 0 - READ FIRST (in this order, completely)
1. HANDOFF_NATIVE_VM.md  - the step-by-step instructions you will follow (steps N2..N11), measured facts about the VZ machine, register layouts, acceptance checks. THIS IS YOUR MAIN SPEC.
2. AGENTS.md             - project rules and history (the "START HERE" block at the top points at the handoff).
3. NATIVE_VM_PLAN.md     - probe results table and the log you must keep appending to.
4. ~/.claude/skills/apple-vz-custom-os-bringup/SKILL.md and ~/.claude/skills/qemu-hvf-hobby-kernel-bringup/SKILL.md - lessons already learned.

STEP 1 - CHECK STATE
- Run: git status; git branch; git log --oneline -5
- Expected: branch main is green; branch native-vm has one extra commit "WIP N2: ACPI platform description, virtual timer, serial ring, boot clock" that does NOT build yet (arch64.c calls pci_init() from a pci.h that does not exist).
- Run: pgrep -fl qemu-system   (if a QEMU or ojjyOS process uses build/aarch64/ojjyos3.img, that is Jonas's VM: never stop it and never overwrite that file).
- git checkout native-vm and work there.

HARD RULES (breaking any of these is a failure)
- Never git push. Commit locally only, with: git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit -m "..." and end every commit message with the line: Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
- Never git add -A blindly: iCloud creates duplicate files like "panic 2.o"; never commit ojjyos-v3-bugs-macos-features-custom-kernel.json.
- Before every commit: bash scripts/test.sh must show ALL TESTS PASSED for every suite; both kernels must build with 0 warnings:
  OJJY_BUILD_DIR=$PWD/build/dev-aarch64 ARCH=aarch64 bash scripts/build.sh
  OJJY_BUILD_DIR=$PWD/build/dev-x86_64 ARCH=x86_64 bash scripts/build.sh
  (check the logs for "warning:" and "error:").
- Use the dev build directories above for development. Only at the very end rebuild the installed images with plain `ARCH=aarch64 bash scripts/build.sh` and `ARCH=x86_64 bash scripts/build.sh`, and only when no VM has them open; then verify the data partition (byte offset 67584*512, length (262144-34-67584)*512) has the same SHA-256 as the newest image in build/<arch>/backups/.
- All device register (MMIO) access must go through kernel/src/arch/aarch64/mmio.h (single LDR/STR instructions) or the hypervisor aborts. All device waits must use wall-clock deadlines (virtio_now_ms(), timer_get_ticks()), never loop counts.
- Never put big static arrays in the kernel (BSS); allocate at runtime with pmm_alloc_bytes().
- Keep "Created by Jonas Lee" credit in user-facing text; never put AI names in the product.
- After each step append a dated line to the "## Log" section of NATIVE_VM_PLAN.md with what you did and the evidence (test output folders, screenshots).

WHAT TO DO - follow HANDOFF_NATIVE_VM.md section 4 exactly, in this order:
N2 finish platform plumbing (create kernel/src/arch/aarch64/pci.h/.c, add platform.c + pci.c to kernel/Makefile, call platform_init(boot_info) first in kernel_main on aarch64, remove unused RTC_BASE). Acceptance: QEMU smoke (TCG and OJJY_ACCEL=hvf, both with --protected) and tests/qemu_pointer.py pass. Commit.
N3 page tables in user_arch.c: 2 MiB granularity below 4 GiB from the UEFI memory map (RAM = normal, rest = device), device blocks for 4-8 GiB, 40-bit IPS. Acceptance: QEMU suites pass. Commit.
N5/N6 PCI enumeration, virtio-pci modern transport (capability walk, common config offsets given in the handoff), refactor virtio drivers to a VirtioDev handle, new virtio_console.c registered with serial_set_sink(). Make the early virtio-gpu probe also find the PCI GPU. Acceptance: running vmapp/probe.swift (build + run commands are in the handoff) produces the kernel boot log in the serial file, ending with "[INIT] Entering main loop"; QEMU suites still pass. Commit after every building sub-step.
N7/N4 verify the clock (boot_epoch) and virtual timer under VZ.
N8 xHCI + USB HID keyboard and absolute pointer (polled; full bring-up sequence in the handoff). Acceptance: inside the app you can type in the setup screen and the pointer lands exactly under the macOS cursor.
N9 the app: vmapp/main.swift, scripts/build-vmapp.sh (swiftc + Info.plist + ad-hoc codesign with vmapp/ojjyOS.entitlements), scripts/run-vm.sh. 1 CPU, 2 GiB, disk build/aarch64/ojjyos3.img, EFI vars build/aarch64/efi-vars.fd, serial log build/aarch64/serial-vz.log, virtio-gpu scanout 2880x1800, NAT network, entropy, USB keyboard + USB screen-coordinate pointer, capturesSystemKeys = true, menus (About/Quit, Start/Stop, Enter Full Screen), refuse to start if the disk is locked by another process.
N10 host control channel (--control socket: key/text/move/click/screenshot/quit) and tests/vz_smoke.py that repeats the first-boot flow of tests/qemu_smoke.py (create account, log in, run Terminal commands, sync, restart, log in with the saved password).
N11 docs (AGENTS.md LATEST section and remove the START HERE block when done, README "Run the native app" section, NATIVE_VM_PLAN.md log), full regression (QEMU TCG + HVF + x86 smoke, pointer, apps, browser, files, network UI tests in tests/), merge native-vm into main (fast-forward or normal merge), rebuild installed images with the data check above.

IF YOU ARE STUCK
- Kernel dies under VZ before the console works: use PSCI SYSTEM_OFF (arch_power_off()) as a 1-bit marker; the probe prints the VM state.
- Bootloader-level facts: python3 tools/esp_read.py <disk.img> OJJYBOOT.LOG
- Never "fix" a test by weakening it; find the real cause.

WHEN DONE, reply to Jonas with a short plain-English summary: what works, how to start the app (exact command), what was tested and the results, anything not finished and why. Nothing pushed.
