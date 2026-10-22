# 22 — Linux compatibility (the "Linux/Arch" side)

Goal (D-016): run **unmodified Linux ARM64 programs** (and x86_64 ones on
the x86_64 build) on ojjyOS, like WSL1 or FreeBSD's Linuxulator: the kernel
recognises Linux ELF binaries and gives them a **Linux system-call
personality** on top of ojjyOS processes, files, sockets and threads. On
top of that, an optional **Linux environment** installs an Arch Linux ARM
userland so `pacman` and thousands of packages work — the Arch spirit
Jonas asked for. GUI apps appear as normal ojjyOS windows through a
**Wayland bridge**.

Clean-room rule: implement Linux behaviour from the public man pages and
ABI documentation (syscall numbers and structure layouts are interface
facts); never copy Linux kernel source.

Primary test corpus: small static test programs we write (with musl from
a package, or our own tiny libc stubs), then BusyBox/coreutils binaries
from Arch Linux ARM, then bigger apps. Third-party binaries are test
inputs only (not shipped in the base image).

---

### LNX-01 — Linux personality: detection and dispatch
- Phase: P7 · Size: M · Status: todo
- Depends: ELF-04, SYS-01, PROC-03
- Files: kernel/compat/linux/ (new)

**Goal.** Detect Linux binaries (ELF OSABI SYSV/GNU with a Linux
interpreter path or `.note.ABI-tag`, or static binaries flagged via a
per-volume setting), mark the process `personality = linux`, route its
`svc #0`/`syscall` through the Linux syscall table (AArch64 generic
numbers; x86_64 numbers on x86), translate errors to Linux errno values
(SYS-04 tables), unimplemented calls return `-ENOSYS` and are logged once
per name.
**Done when.** A static Linux "hello world" (aarch64) prints and exits.
**Tests.** Live test with a fixture binary; host test of the dispatch
table generator.
**Pitfalls.** ARM64 Linux uses `x8` for the number and `x0` for return —
same registers as native, different numbers.
**Refs.** Linux man-pages (section 2); arm64 syscall table (asm-generic).

### LNX-02 — Core syscalls (files, memory, process)
- Phase: P7 · Size: L · Status: todo
- Depends: LNX-01, SYS-07, SYS-06
- Files: kernel/compat/linux/sys_*.c

**Goal.** Enough for static coreutils: `openat, close, read, write,
readv, writev, pread64, pwrite64, lseek, fstat, newfstatat, statx,
getdents64, mkdirat, unlinkat, renameat2, linkat, symlinkat, readlinkat,
fchmod(at), fchown(at), utimensat, fcntl, ioctl (TCGETS/TIOCGWINSZ …),
dup3, pipe2, getcwd, chdir, fchdir, mmap, munmap, mprotect, brk, mremap,
madvise, exit, exit_group, getpid, getppid, gettid, getuid…, uname,
clock_gettime, nanosleep, getrandom, prlimit64, sysinfo, umask`.
Structure layouts converted exactly (struct stat, statx, dirent64).
**Done when.** BusyBox (static) runs `ls`, `cat`, `cp`, `sh` correctly.
**Tests.** Our own Linux syscall test programs + BusyBox smoke script.
**Pitfalls.** `stat` time fields and device numbers encoding; `brk`
semantics.
**Refs.** man-pages.

### LNX-03 — Threads and futexes (clone)
- Phase: P7 · Size: M · Status: todo
- Depends: LNX-02, THR-05
- Files: kernel/compat/linux/clone.c

**Goal.** `clone`/`clone3` with the flag combinations libc uses for
threads and fork, `set_tid_address`, `futex` (wait/wake/requeue/
bitset, robust lists), `set_robust_list`, TLS register setting, `rseq`
returns `-ENOSYS` cleanly, `sched_*` basics.
**Done when.** A pthreads test program from glibc/musl runs with 64
threads.
**Tests.** Live tests.
**Pitfalls.** Thread exit clearing `clear_child_tid` + futex wake.
**Refs.** man 2 clone, futex.

### LNX-04 — Linux signals
- Phase: P7 · Size: M · Status: todo
- Depends: LNX-03, PROC-06
- Files: kernel/compat/linux/signal.c

**Goal.** `rt_sigaction`, `rt_sigprocmask`, `rt_sigreturn` with the exact
Linux signal frame layouts (arm64 `struct rt_sigframe`, x86_64 with
FP state), `kill`, `tgkill`, `sigaltstack`, `rt_sigsuspend`,
`rt_sigtimedwait`, SA_RESTART semantics.
**Done when.** Programs handling SIGINT/SIGCHLD/SIGSEGV behave correctly.
**Tests.** Signal test programs.
**Pitfalls.** Frame layout must match what libc's restorer expects.
**Refs.** Kernel ABI docs for signal frames.

### LNX-05 — Linux filesystem view (/proc, /sys, /dev, /etc)
- Phase: P7 · Size: M · Status: todo
- Depends: LNX-02, FS-12
- Files: kernel/compat/linux/procfs.c, fsview.c

**Goal.** Linux processes see a Linux-shaped tree: `/proc` in Linux
formats (self/maps, self/exe, cpuinfo, meminfo, stat, mounts…), minimal
`/sys` (devices/system/cpu), `/dev` (null, zero, urandom, tty, pts,
shm), `/etc` (passwd/group generated from ojjyOS accounts, resolv.conf,
hosts, os-release identifying ojjyOS), with the Linux root being either
the ojjyOS root or the Linux environment rootfs (LNX-40) plus bind views
of `/Users`.
**Done when.** `ps`, `top`, `df` from BusyBox work.
**Tests.** Live tests comparing outputs to expected formats.
**Pitfalls.** Many tools parse `/proc` strictly — match formats exactly.
**Refs.** man 5 proc.

### LNX-06 — Networking for Linux programs
- Phase: P7 · Size: M · Status: todo
- Depends: LNX-02, NET-10
- Files: kernel/compat/linux/net.c

**Goal.** `socket, bind, connect, listen, accept4, sendto, recvfrom,
sendmsg, recvmsg (SCM_RIGHTS), getsockopt/setsockopt (common options),
shutdown, socketpair, getsockname, getpeername`, AF_INET/INET6/UNIX
with Linux sockaddr layouts; minimal AF_NETLINK (route dump for `ip`).
**Done when.** Linux `curl` (dynamic, LNX-08) downloads a page.
**Tests.** Live tests with host echo servers.
**Pitfalls.** Option numbers differ from native.
**Refs.** man 7 socket, ip, unix, netlink.

### LNX-07 — epoll, eventfd, timerfd, signalfd, inotify, memfd
- Phase: P7 · Size: M · Status: todo
- Depends: LNX-02, SYS-08, FS-08
- Files: kernel/compat/linux/epoll.c, ...

**Goal.** Event APIs used by servers and GUI toolkits, mapped onto SYS-08
event queues and FS-08 notifications; `memfd_create` on vm_objects.
**Done when.** A GLib/Node-style event loop program runs.
**Tests.** Live tests.
**Pitfalls.** Edge-triggered semantics.
**Refs.** man 7 epoll, inotify.

### LNX-08 — Dynamically linked glibc programs
- Phase: P7 · Size: M · Status: todo
- Depends: LNX-07, LNX-05, LNX-04
- Files: kernel/compat/linux/

**Goal.** Run Arch Linux ARM's glibc binaries: `ld-linux-aarch64.so.1`
loaded as interpreter, auxv entries glibc expects (AT_HWCAP/HWCAP2
matching the CPU, AT_SYSINFO_EHDR with a Linux-compatible vDSO
providing `__kernel_clock_gettime`), `prctl` basics, `getrandom`,
`/etc/ld.so.cache` from the environment.
**Done when.** `bash`, `python3`, `git` from Arch Linux ARM run inside
the Linux environment.
**Tests.** Live tests running package binaries.
**Pitfalls.** glibc probes many syscalls at startup — log ENOSYS hits.
**Refs.** —

### LNX-09 — ptrace for debuggers
- Phase: P8 · Size: M · Status: todo
- Depends: LNX-04, SDK-05
- Files: kernel/compat/linux/ptrace.c

**Goal.** `ptrace` subset (attach, peek/poke, getregset, cont, singlestep,
syscall tracing) so Linux `gdb` and `strace` work.
**Done when.** Linux gdb debugs a Linux program.
**Tests.** Live tests.
**Pitfalls.** Security: same-user + entitlement.
**Refs.** man 2 ptrace.

### LNX-10 — Syscall coverage tracker and test harness
- Phase: P7 · Size: S · Status: todo
- Depends: LNX-01
- Files: tests/linux/, docs/compat/linux-syscalls.md

**Goal.** Generated table of all Linux syscalls with status
(implemented/partial/ENOSYS), counts of ENOSYS hits from test runs, and a
harness running our Linux test programs plus selected upstream test
binaries (as external test inputs, not shipped).
**Done when.** Coverage page generated in CI.
**Tests.** The harness.
**Pitfalls.** —
**Refs.** —

### LNX-20 — Seamless shell integration
- Phase: P7 · Size: S · Status: todo
- Depends: LNX-08, USR-20
- Files: apps-cli/ojsh/, compat/linux/

**Goal.** Linux binaries run from ojsh like native ones (PATH includes
the environment's `/usr/bin` when enabled), `linux` command opens a shell
inside the environment, pipes mix native and Linux programs.
**Done when.** `ls | linux-grep x` style pipelines work.
**Tests.** Live tests.
**Pitfalls.** Name clashes between native and Linux tools — native first.
**Refs.** —

### LNX-30 — Wayland bridge for Linux GUI apps
- Phase: P8 · Size: L · Status: todo
- Depends: LNX-07, WS-02, UIK-01
- Files: compat/linux/wayland-bridge/

**Goal.** A bridge process implementing the Wayland server protocol
(wl_compositor, wl_subcompositor, wl_shm, xdg_shell incl. popups,
wl_seat with keyboard (xkb keymap), pointer, touch, wl_data_device for
clipboard/DnD, wl_output with scale, xdg-decoration requesting
server-side decorations, linux-dmabuf later) translating to WindowServer
surfaces; Linux windows get ojjyOS glass title bars and appear in the
Dock and Mission Control.
**Done when.** GTK4 and Qt6 demo apps from Arch Linux ARM run and look
native-ish; copy/paste works between Linux and ojjyOS apps.
**Tests.** Live tests with weston demo clients.
**Pitfalls.** Keymaps (xkb) generation from our layouts.
**Refs.** Wayland protocol XML specs (public).

### LNX-31 — X11 applications (via a ported XWayland package)
- Phase: P8 · Size: M · Status: todo
- Depends: LNX-30, PKG-04
- Files: packages/xwayland/

**Goal.** Package XWayland (MIT licence, D-023) so X11 apps run through
the Wayland bridge.
**Done when.** `xterm` and `xeyes` run.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### LNX-32 — Graphics and audio for Linux apps
- Phase: P8 · Size: M · Status: todo
- Depends: LNX-30, GPU-41, AUD-02
- Files: compat/linux/gpu/, compat/linux/audio/

**Goal.** Vulkan/GL for Linux apps via our ICD (GPU-41) exposed as Linux
libraries in the environment; audio via a PulseAudio/PipeWire-protocol
bridge to `audiod`.
**Done when.** A Linux game plays with sound.
**Tests.** Live tests.
**Pitfalls.** Library ABI versions in the environment.
**Refs.** —

### LNX-40 — Linux environment (Arch Linux ARM userland)
- Phase: P7 · Size: M · Status: todo
- Depends: LNX-08, FS-22, PKG-01
- Files: compat/linux/env/, Settings > Developer

**Goal.** "Enable Linux environment" downloads a signed Arch Linux ARM
root filesystem (x86_64 Arch on the x86 build) into `/opt/linux` (an
ojjyFS volume or ext4 image), configures it (users mapped, DNS, home
folder shared at `/home/<user>` → `/Users/<user>`), and `pacman -Syu`
works inside it; a "Linux" Terminal profile; uninstall cleanly.
**Done when.** `pacman -S neovim` installs and runs.
**Tests.** Live test (long-running; nightly).
**Pitfalls.** Trademark: call it "Linux environment (Arch Linux ARM)",
following Arch's naming guidelines.
**Refs.** Arch Linux ARM installation docs.

### LNX-41 — Desktop integration for Linux apps
- Phase: P8 · Size: S · Status: todo
- Depends: LNX-40, LNX-30, SHL-27
- Files: compat/linux/integration/

**Goal.** `.desktop` files and icons from the environment appear in
Launchpad/Spotlight (in a "Linux apps" folder), file associations, open
Linux apps from Finder.
**Done when.** Installing a GUI app with pacman adds it to Launchpad.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** freedesktop.org Desktop Entry spec.

### LNX-42 — D-Bus and portals bridge
- Phase: P8 · Size: M · Status: todo
- Depends: LNX-41, SVC-11
- Files: compat/linux/dbus-bridge/

**Goal.** Session bus (ported dbus-daemon in the environment or our own
broker) with bridges: notifications → notifyd, file chooser portal →
UIK-29 panels, secrets → keychain, screenshots → WS-12 with permissions.
**Done when.** A Linux app's notification shows as an ojjyOS banner.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** freedesktop.org specs.

### LNX-50 — Container isolation for Linux processes
- Phase: P10 · Size: L · Status: todo
- Depends: LNX-40, SEC-21
- Files: kernel/compat/linux/ns.c

**Goal.** Mount/PID/network namespace subset so container tools
(podman-style rootless containers) and Flatpak-like sandboxes can run;
the Linux environment itself runs in a namespace.
**Done when.** A rootless container runs a simple image.
**Tests.** Live tests.
**Pitfalls.** Large; split before starting.
**Refs.** man 7 namespaces.

### LNX-51 — Milestone: Firefox from the Linux environment
- Phase: P10 · Size: M · Status: todo
- Depends: LNX-32, LNX-42
- Files: docs/compat/linux-apps.md

**Goal.** Firefox (ARM64, from Arch Linux ARM) runs, browses and plays
video — proving the Linux layer at scale (and, with it, access to apps
like LibreOffice and GIMP).
**Done when.** Firefox loads YouTube and plays a video (record).
**Tests.** Recorded manual test + automated launch test.
**Pitfalls.** Firefox's sandbox uses seccomp/namespaces — may need
`MOZ_DISABLE_CONTENT_SANDBOX` initially (document it).
**Refs.** —
