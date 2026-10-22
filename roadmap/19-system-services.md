# 19 — System services

User-space daemons that make the system work (D-013): the service manager
`ojjyd` (PID 1), sessions, settings, notifications, pasteboard, logging,
crash reports, search, updates, recovery, installation, printing, power,
time, location, background tasks.

Rules: every service has a definition file, runs with the least privilege
(own user where possible, sandbox profile), speaks typed `ojidl`
protocols (IPC-07), restarts on crash, logs through `logd`, and has a
live test.

---

### SVC-01 — `ojjyd`: init and service manager
- Phase: P2 · Size: L · Status: todo
- Depends: PROC-02, IPC-04, FS-02, BOOT-03
- Files: services/ojjyd/ (new)

**Goal.** PID 1: mounts filesystems (fstab-like config), starts services
in dependency order with parallelism, supervises them (restart with
back-off, crash loop detection → safe mode), on-demand activation when a
registered port name is first used (IPC-04), reaps orphans, handles
shutdown/reboot ordering (stop in reverse, sync, power off), exposes
status to `ojjyctl` (`list`, `status`, `start`, `stop`, `restart`, `logs`).
**Steps.**
1. Minimal PID 1 that spawns a shell (proves PROC/ELF/FS path).
2. Service definitions + dependency graph + supervision.
3. Port-based activation and the bootstrap registry.
**Done when.** Boot to desktop is driven by ojjyd; killing any service
restarts it; `ojjyctl list` shows states.
**Tests.** Live tests killing services; boot time budget (BOOT-08).
**Pitfalls.** PID 1 must never crash: minimal code, no dynamic memory
surprises, tested heavily.
**Refs.** launchd and systemd concepts.

### SVC-02 — Service definition format
- Phase: P2 · Size: S · Status: todo
- Depends: USR-41
- Files: docs/services/format.md, /System/Library/Services/*.toml

**Goal.** TOML files: name, executable, arguments, user/group, sandbox
profile, dependencies (after/requires), ports to register, restart
policy, resource limits, environment, run-at (boot/login/on-demand/
schedule), logging subsystem.
**Done when.** All system services described this way; validator tool.
**Tests.** Validator host tests.
**Pitfalls.** —
**Refs.** —

### SVC-03 — Boot sequence to the login window
- Phase: P2 · Size: M · Status: todo
- Depends: SVC-01, WS-01, SHL-15
- Files: services/ojjyd/boot.toml

**Goal.** Kernel starts `/System/Boot/ojjyd`; ojjyd mounts root/data,
starts logd → devd → diskd → netconfigd → WindowServer → loginwindow;
the kernel no longer launches the UI itself.
**Done when.** Cold boot under VZ reaches the login window via ojjyd in
≤ 3 s (BOOT-08).
**Tests.** `vz_boot.py`, smoke matrix.
**Pitfalls.** Recovery path if WindowServer fails (text console + ojsh).
**Refs.** —

### SVC-04 — Login session manager
- Phase: P2 · Size: M · Status: todo
- Depends: SVC-01, PROC-04, SEC-41
- Files: services/sessiond/, kernel/src/users.c → services/authd

**Goal.** Authenticates through `authd` (account database moves to user
space, SEC-41), creates a session (audit ID, user credentials, session
ports), starts per-user agents (Dock, menu bar, Finder, notification UI,
pasteboard), ends sessions cleanly on log out, supports multiple sessions
(fast user switching, SHL-15).
**Done when.** Two users logged in at once with isolated processes.
**Tests.** Live tests (cold login, switching, log out).
**Pitfalls.** Keep account database compatibility with today's users.db.
**Refs.** —

### SVC-05 — Per-user agents
- Phase: P2 · Size: S · Status: todo
- Depends: SVC-04
- Files: /System/Library/Agents/*.toml

**Goal.** Services that run per user session (e.g. searchd user indexer,
notification UI, clipboard history) with the same supervision as
system services.
**Done when.** User agents start/stop with the session.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### SVC-10 — Settings store (`prefsd`) and `defaults`
- Phase: P2 · Size: M · Status: todo
- Depends: SVC-01, USR-41
- Files: services/prefsd/, lib/libojjy/prefs.c, apps-cli/defaults

**Context.** Settings live in `system.conf` keys (`appearance=`,
`accent=`, `wallpaper=`…) system-wide (TODO.md: per-user settings
pending).
**Goal.** Domains per app and global, per user and system-wide (`/Library`
vs `~/Library/Preferences`), typed values (bool, int, float, string, data,
array, dict), change notifications to observers, atomic writes, managed
(admin-locked) values, `defaults read/write` CLI; migration from
`system.conf`.
**Done when.** Appearance is per user; changing it in one session does
not affect another.
**Tests.** Host tests; live test with two users.
**Pitfalls.** Keep values readable as text files (user is in control).
**Refs.** —

### SVC-11 — Notification service (`notifyd`)
- Phase: P3 · Size: M · Status: todo
- Depends: SVC-01, SHL-02, IPC-07
- Files: services/notifyd/, lib/libojjy/notify.c

**Goal.** App API (title, subtitle, body, image, sound, actions, reply
field, thread/grouping, scheduled delivery, badges), per-app permission
prompt, delivery to the banner UI and Notification Center, Focus filtering,
history persistence.
**Done when.** A sandboxed app posts a notification with an action that
calls back into it.
**Tests.** Live tests.
**Pitfalls.** Rate limiting abusive apps.
**Refs.** —

### SVC-12 — Pasteboard service (`pasteboardd`)
- Phase: P3 · Size: S · Status: todo
- Depends: SVC-01, IPC-05
- Files: services/pasteboardd/

**Goal.** System pasteboards (general, find, drag), multiple
representations per item, lazy data provision, size limits, privacy
(paste notification when an app reads the pasteboard without user
action — optional), host clipboard bridge (DRV-VIRTIO-11).
**Done when.** Copy/paste of text, images and files works across apps.
**Tests.** Live tests.
**Pitfalls.** Secure fields never placed on the pasteboard.
**Refs.** —

### SVC-13 — Input method server
- Phase: P4 · Size: M · Status: todo
- Depends: WS-04, I18N-02
- Files: services/inputmethodd/

**Goal.** Hosts keyboard layouts and input methods (I18N-03) out of
process, candidate windows positioned at the caret, input source menu in
the menu bar.
**Done when.** Switching input sources works in all text fields.
**Tests.** Live tests.
**Pitfalls.** Latency per keystroke < 2 ms.
**Refs.** —

### SVC-14 — Media session service (Now Playing)
- Phase: P6 · Size: S · Status: todo
- Depends: SVC-01, AUD-01
- Files: services/mediasessiond/

**Goal.** Apps publish now-playing info and accept remote commands
(play/pause/next) from media keys, Control Center, Bluetooth AVRCP and
the lock screen.
**Done when.** Media keys control Music and ojjyWeb videos.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SVC-15 — Background tasks and scheduling
- Phase: P5 · Size: S · Status: todo
- Depends: SVC-01, SVC-60
- Files: services/ojjyd/schedule.c

**Goal.** cron-like schedules and "run when idle/on power" background
tasks (indexing, backups, update checks) with power and thermal awareness.
**Done when.** Backups and indexing run only when idle on AC power
(laptops) or always in VMs.
**Tests.** Live test with simulated idle.
**Pitfalls.** —
**Refs.** —

### SVC-20 — Unified logging (`logd`)
- Phase: P2 · Size: M · Status: todo
- Depends: SVC-01, KDB-01
- Files: services/logd/, lib/libojjy/log.c

**Goal.** Structured log entries (timestamp, process, thread, subsystem,
category, level, message with typed arguments and privacy redaction of
dynamic strings by default), in-memory ring + persistent compressed
store with size limits, kernel log ingestion, streaming and query API,
`log stream/show` CLI and Console app.
**Done when.** All services log through it; `log show --last 5m` works.
**Tests.** Host tests (formatting, redaction); live tests.
**Pitfalls.** Logging must never block callers (drop with counters).
**Refs.** —

### SVC-21 — Crash reporter (`crashreporterd`)
- Phase: P2 · Size: S · Status: todo
- Depends: SVC-20, PROC-08
- Files: services/crashreporterd/

**Goal.** Receives crash exceptions, writes symbolicated `.crash` reports
(OS version, process, exception, thread backtraces, loaded images,
recent logs), shows the "quit unexpectedly" dialog with Reopen and
"Copy report", optional sending is never automatic.
**Done when.** Crash dialog and report for a test crasher.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SVC-22 — Diagnostics bundle (`sysdiagnose`)
- Phase: P3 · Size: S · Status: todo
- Depends: SVC-20, SVC-21
- Files: apps-cli/sysdiagnose

**Goal.** One command (and a Help menu item) collecting logs, crash
reports, system info, process list, disk/network status into an archive
for bug reports to Jonas/AI sessions.
**Done when.** Archive produced with redactions.
**Tests.** Live test.
**Pitfalls.** Privacy: list contents before sharing.
**Refs.** —

### SVC-30 — Search indexer (`searchd`)
- Phase: P3 · Size: L · Status: todo
- Depends: FS-08, USR-40, SVC-05
- Files: services/searchd/

**Goal.** Metadata index (name, kind, dates, size, tags, app-specific
attributes) and content index (text extraction plugins: plain text, rich
text, PDF, HTML, Markdown, office formats, code) with a full-text
inverted index (tokenization per language, prefix search, ranking),
incremental updates from FS events, per-user indexes respecting
permissions, query API used by Spotlight and Finder search.
**Done when.** 50,000 files indexed in < 10 minutes under VZ; query
latency < 50 ms.
**Tests.** Live tests with generated corpora.
**Pitfalls.** Throttle indexing (background class, BLK-05).
**Refs.** —

### SVC-40 — Software updates (`updated`) with A/B rollback
- Phase: P5 · Size: L · Status: todo
- Depends: FS-18, PKG-02, BOOT-04
- Files: services/updated/, tools/mkupdate

**Goal.** Signed update packages (full image or delta against the
installed sealed system), downloaded in the background, staged into the
inactive slot (new sealed snapshot), boot slot switch with health check
after boot (services up, login reached) → mark good; automatic rollback
otherwise (BOOT-04). Packages (ojjypkg) updated in the same flow.
**Done when.** Update from version N to N+1 over the network; a
deliberately broken update rolls back automatically.
**Tests.** Live tests with a local update server.
**Pitfalls.** Never leave the machine unbootable; keep the old slot until
the new one is confirmed.
**Refs.** —

### SVC-41 — Recovery mode
- Phase: P5 · Size: M · Status: todo
- Depends: BOOT-03, STO-02, SVC-40
- Files: recovery/ (initrd image), services/recoveryui/

**Goal.** Recovery environment (minimal WindowServer + recovery app from
the initrd/recovery partition): reinstall ojjyOS, restore from Rewind
backup, Disk Utility, Terminal, reset a password (admin data volume
unlock required), startup security settings, share disk logs.
**Done when.** A VM with a corrupted system volume can be repaired from
Recovery.
**Tests.** Live tests with damaged images.
**Pitfalls.** Recovery must work offline.
**Refs.** —

### SVC-42 — Installer
- Phase: P9 · Size: M · Status: todo
- Depends: SVC-41, STO-02, HW-01
- Files: services/installer/, tools/mkinstallmedia

**Goal.** Bootable installer (USB/ISO) that partitions a target disk
(GPT: ESP, recovery, system A/B, data), installs bootloader to the ESP
(with UEFI boot entry), copies the sealed system, runs Setup Assistant on
first boot; dual-boot aware (doesn't touch other OSes' partitions unless
told).
**Done when.** Installation on a blank QEMU disk and a real PC (record).
**Tests.** QEMU installer test.
**Pitfalls.** Destroying data on the wrong disk — strong confirmations.
**Refs.** UEFI boot manager spec.

### SVC-50 — Printing (`printd`)
- Phase: P6 · Size: L · Status: todo
- Depends: NET-13, GFX-08, UIK-28
- Files: services/printd/, lib/ojjykit/print/

**Goal.** Driverless printing (IPP Everywhere / AirPrint-compatible):
discovery via mDNS, capabilities query, job submission as PDF or PWG
raster/URF (rasterized via libpdf), queue management, print dialog with
preview (copies, pages, paper, two-sided, colour), Print to PDF.
**Done when.** Printing to an IPP Everywhere test printer (or a real
modern printer, record) works from TextEdit and ojjyWeb.
**Tests.** Live test with a simulated IPP printer program.
**Pitfalls.** Spooling large jobs; cancel behaviour.
**Refs.** RFC 8011 (IPP), PWG 5100.14 (IPP Everywhere).

### SVC-60 — Power management (`powerd`)
- Phase: P5 · Size: M · Status: todo
- Depends: SVC-01, SCH-06
- Files: services/powerd/

**Goal.** Idle display sleep and system sleep timers, power assertions
(apps prevent sleep during video/downloads), CPU performance states
(ACPI CPPC/P-states on hardware), battery status (DRV-ACPI-03), low power
mode, thermal pressure handling, wake reasons log; VM-friendly behaviour
(display sleep only).
**Done when.** Display sleeps after idle; video playback prevents it.
**Tests.** Live tests with shortened timers.
**Pitfalls.** Wake from sleep on VMs is host-dependent.
**Refs.** ACPI CPPC.

### SVC-70 — Full NTP time service
- Phase: P5 · Size: S · Status: todo
- Depends: TIM-04, NET-10
- Files: services/timed/

**Goal.** Move SNTP from the kernel to `timed`: multiple servers, clock
slewing instead of jumps, drift file, NTS (authenticated NTP) later.
**Done when.** Clock stays within 50 ms of host over 24 h.
**Tests.** Soak test.
**Pitfalls.** —
**Refs.** RFC 5905, RFC 8915.

### SVC-71 — Location service (`locationd`)
- Phase: P6 · Size: S · Status: todo
- Depends: NET-17, SEC-11
- Files: services/locationd/

**Goal.** Coarse location by IP (opt-in, public service), manual location
setting, Wi-Fi based later; permission-gated API for Weather, Maps,
time zone auto-set; menu bar indicator when in use.
**Done when.** Weather shows the local city after permission.
**Tests.** Live test with recorded responses.
**Pitfalls.** Privacy first: off by default.
**Refs.** —

### SVC-80 — Sync with a user-owned server (optional)
- Phase: P10 · Size: L · Status: todo
- Depends: NET-76, STO-01, SEC-40
- Files: services/syncd/

**Goal.** Sync Notes, Reminders, Calendars, Contacts, ojjyWeb bookmarks
and settings between ojjyOS machines through a server the user controls
(WebDAV/Nextcloud/CalDAV), end-to-end encrypted.
**Done when.** Two VMs stay in sync through a local WebDAV server.
**Tests.** Two-VM test.
**Pitfalls.** Conflict resolution must never lose data.
**Refs.** —
