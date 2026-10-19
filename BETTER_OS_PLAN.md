# BETTER_OS_PLAN.md — how to make ojjyOS better (2026-10-18)

Jonas: "make a detailed list on how to make the os better then implement.
also do a routine check on all things for bugs and security compromises."

This is that list, ordered by Jonas's own priority ("reliable boot → input
→ persistent storage → protected apps → dependable desktop → visual
polish"), with a **security pass first** because the routine check found
real problems. Each item says what/why/how and its acceptance test. The
UI-job items (LG4, S1-S4, per-app polish) stay in `HANDOFF_UI.md` §5 and
are not repeated here; this list covers everything else. Mark items here
as they land.

## 0. Security & bug pass (2026-10-18 routine check) — FOUND, fixing now

Audited: credential engine (`users.c`), CSPRNG (`random.c`), DHCP/DNS
parsers (`net.c`), HTTP body handling (`http.c`), HTML engine copies
(`html.c`), VFS (`vfs.c`), persistence (CRC + validation), EL0 runner,
fuzz suites (HID/JPEG/wallpack), Mac host app control channel.

**Clean, no action needed:** ChaCha20 DRBG with key erasure and
SHA-256 entropy folding (`random.c`) is sound; DHCP option parsing is
fully bounds-checked with xid/MAC/magic validation; HTTP never trusts a
Content-Length larger than the buffer; the HTML engine uses bounded
copies with explicit NULs; hash comparison is constant-time; persistence
validates CRCs and structure into scratch before swapping; the EL0
boundary suite (19 programs) holds; the host app's control socket is
owner-only with view-local input.

- [x] **S1 CRITICAL — unauthenticated admin creation.** FIXED: the lock
  screen's "+ New Account" (and Settings' "+ Add Account" and Terminal
  `adduser`, which both route to the same paths) called
  `users_create(..., is_admin=true)` with no authorization — anyone at
  the login screen could mint an admin account. `users_create` now
  refuses (-4) once any account exists unless the session is an
  authenticated administrator; the first account stays open as the
  bootstrap. Both UIs and `adduser` explain the denial. Verified: host
  test (second create denied unauthenticated/non-admin, allowed as
  admin) + first-boot setup still works in every live smoke.
- [x] **S2 HIGH — guessable password salt.** FIXED: 8 bytes from
  `random_bytes()` (the ChaCha20 DRBG) stored as 16 hex chars; the old
  time/username/counter mix is gone. Verified: host test asserts the
  16-hex CSPRNG salt; legacy 8-hex salts still verify.
- [x] **S3 HIGH — no key stretching.** FIXED: PBKDF2-HMAC-SHA256, 10,000
  iterations (a deliberate compromise for a TCG-emulated guest — OWASP's
  modern floor is far higher; scale up when preemptive scheduling
  lands). New hashes are `pbkdf2$10000$<hex>`; the DB loader accepts
  both formats (it previously required exactly 64 chars, which would
  have silently dropped every PBKDF2 account — caught during the fix);
  iteration counts are validated and capped at 1e6 so a hostile DB
  record cannot overflow the comparison buffer. Legacy accounts still
  verify and are transparently re-hashed with PBKDF2 + a fresh CSPRNG
  salt on the next successful login (`[USERS] Upgraded credentials…`).
  Verified: host tests incl. a fixed legacy vector
  (sha256("ojjyOS-v3-auth:deadbeef:legacy-pass")) planted in users.db,
  loaded, logged in and upgraded.
- [x] **S4 MEDIUM — no per-user file isolation.** FIXED: VFS-level
  enforcement in `vfs_open/opendir/stat/exists/isdir/isfile/mkdir/
  unlink/rename` — authenticated non-admins are denied everything under
  `/Users/<other>/` (case-insensitive name compare); admins and the
  unauthenticated boot path (provisioning, first-boot setup) are
  unrestricted; `/Users` itself stays listable. Finder, Terminal,
  Preview and the apps inherit it because they all go through VFS.
  Verified: host tests (non-admin denied read/stat/probe of another
  home, own home and admin unaffected).
- [x] **S5 MEDIUM — credential database readable.** FIXED: non-admins are
  denied all access under `/Library/Accounts/` except the exact
  write-open `users_save()` performs for password self-service (a
  non-admin create|truncate open of users.db is the remaining,
  documented compromise until the journaled filesystem brings real
  per-file ACLs). Verified: host tests (non-admin read denied, admin
  allowed).
- [x] **S6 — `users_delete` guards.** FIXED: admin session required;
  cannot delete yourself; cannot delete the last administrator.
  Verified by host tests.

Receipts: `build/sec-{host-4,san,arm-final,x86-final,tcg,hvf,x86,appearance}.log`
(15/15 host suites normal + ASan/UBSan, both builds 0 warnings, TCG
protected smoke PASS; the rest of the live matrix runs with this exact
tree).

## 1. Kernel & platform (Jonas: "it sets up everything we build upon")

- [ ] **Real preemptive scheduler.** Per-task kernel stacks + saved
  context in `ProcEntry`; round-robin at timer IRQ across kernel tasks
  so network, autosave and rendering never stall each other. Biggest
  single win for "feels smooth". Acceptance: drag never blocks on disk
  flush; measured frame p95 under load; all existing suites still pass.
- [ ] **Built-in apps as protected EL0 programs** over a real syscall
  ABI (file handles, windows, events) — never the raw
  `syscall_dispatch()`. Starts with one app (Calculator) as a pilot.
- [ ] **Kernel W^X + heap guards.** Text pages RO in the owned tables;
  kheap red zones between allocations (magic already detects
  overruns; make the guard pages fault instead).
- [ ] **pmm_free_bytes() + heap backed by the page allocator**, then
  UI buffers can be released (today everything is kept for the boot).
- [ ] **Intel parity:** PCI enumeration, NIC, USB absolute tablet.
- [ ] **virtio-gpu display-change events** (live guest resize) on the
  native app too (fixed 2880x1800 scanout today).

## 2. Storage

- [ ] **Journaled on-disk filesystem** replacing RAMFS snapshots:
  real directories, larger files, per-inode owner/mode (replaces the
  path-based S4 enforcement with real ACLs), crash-safe append.
- [ ] **Per-user settings** (wallpaper, appearance) once settings move
  to the filesystem; today they are system-wide honestly.

## 3. Networking & web

- [ ] **HTTPS certificate verification** (X.509 profile, RSA-2048 +
  ECDSA-P256 verify against a small root store; `crypto/` already has
  the primitives). Until then the About/limits text must keep saying
  "encrypted, identity not verified".
- [ ] **NTP clock sync** at boot + after DHCP; disciplines the firmware
  clock for Auto appearance too.
- [ ] **Downloads to ~/Downloads**, PNG decode (inflate) and
  progressive JPEG for `<img>`; browser cache to disk.

## 4. Desktop & apps (beyond the UI job's list)

- [ ] **Multiple windows per app** (two Finder/Terminal windows) —
  needs `AppWindowState` arrays instead of singletons.
- [ ] **Screenshot shortcut** (⌘⇧3 → ~/Desktop/shot-N.bmp).
- [ ] **Sound**: virtio-sound driver + startup chime + UI clicks.
- [ ] **Games in Tahoe style** (Snake/2048/Minesweeper), Paint app.
- [ ] **Accessibility**: text-size scale, high-contrast theme.

## 5. Performance discipline (measurement rules, learned today)

- Never report wall-clock numbers measured while the host load is
  inflated (iCloud fileproviderd churn after big builds, parallel QEMU
  suites). Run heavy suites solo; A/B against a stashed base under the
  same load when in doubt; re-run the LG3 drag-frame gate (≤ ~36 ms at
  2x) on a quiet host before quoting it.
