# 20 — Security and privacy

Today (`02-current-state.md` §8): PBKDF2 passwords with CSPRNG salts,
admin-gated account creation, VFS-level home isolation, a bounded EL0
sandbox for test programs, encrypted (not yet verified) HTTPS. The
2026-10-18 routine audit (`BETTER_OS_PLAN.md` §0) found and fixed six
holes; that routine continues every release (SEC-02).

Target (D-014): least privilege everywhere, sandboxed apps with declared
entitlements and user consent, signed code, sealed system volume,
encrypted data, hardened kernel and user space, honest privacy UI.

---

### SEC-01 — Threat model
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: docs/security/threat-model.md (new)

**Goal.** Who we defend against (malicious web pages, downloaded apps,
hostile files/USB devices/networks, other local users, physical theft of
the device) and what we don't (nation-state firmware attacks, cold-boot
attacks on VMs), assets (user files, credentials, keys), trust boundaries
(kernel/user, app/app, network/host, VM/host app control channel).
**Done when.** Doc exists; every SEC card cites the threat it addresses.
**Tests.** —
**Pitfalls.** Keep it updated with each phase.
**Refs.** —

### SEC-02 — Routine security review per release
- Phase: P1 · Size: S · Status: todo
- Depends: SEC-01
- Files: docs/security/review-checklist.md

**Context.** Jonas asked for "a routine check on all things for bugs and
security compromises" (BETTER_OS_PLAN §0) — make it permanent.
**Goal.** A checklist run before each release: new parsers fuzzed, new
syscalls reviewed for pointer validation and rights, new services'
sandbox profiles, dependency licences, secrets in logs, crypto review,
plus a findings log with fixes.
**Done when.** Checklist used for release 3.2 (QA-40).
**Tests.** —
**Pitfalls.** —
**Refs.** BETTER_OS_PLAN.md §0.

### SEC-03 — Exploit mitigations (user space)
- Phase: P2 · Size: M · Status: todo
- Depends: ELF-05, USR-01
- Files: lib/ld/, lib/libc/, sdk/toolchain/

**Goal.** PIE everywhere with full ASLR (stack, heap, mmap, libraries,
vDSO), stack protectors (canary from AT_RANDOM), `_FORTIFY_SOURCE`-style
checked libc functions, full RELRO + immediate binding for system
binaries, non-executable stacks/heaps, ARM PAC (pointer authentication)
and BTI enabled where the CPU supports them, x86 CET shadow stacks later,
hardened malloc option (guard pages, randomized allocation).
**Done when.** `checksec`-like tool reports all mitigations on all system
binaries.
**Tests.** Tool + exploit-style test programs that must crash safely.
**Pitfalls.** PAC keys per process; compatibility for ported software.
**Refs.** Arm PAC/BTI architecture docs.

### SEC-04 — Kernel hardening
- Phase: P2 · Size: M · Status: todo
- Depends: MEM-07, MEM-13
- Files: kernel/

**Goal.** KASLR, PAN/PXN (ARM) and SMEP/SMAP (x86) enabled, kernel stack
canaries, `-fzero-call-used-regs`/auto-init of stack variables, freelist
hardening in slab, restricted kernel pointer exposure in logs, PAC/BTI
for the kernel on ARM, CFI where the compiler supports it.
**Done when.** Mitigations listed by `ojjyctl security` and verified by
tests.
**Tests.** Debug-build negative tests (e.g. kernel executing user page
faults).
**Pitfalls.** Performance check of each mitigation.
**Refs.** —

### SEC-10 — Code identity for processes
- Phase: P5 · Size: S · Status: todo
- Depends: SEC-20
- Files: kernel/security/identity.c

**Goal.** Each process carries the identity of its signed code (bundle
ID + team/developer ID + signature hash), unforgeable, queryable by
services (firewall rules, permissions, keychain ACLs).
**Done when.** Permissions survive app updates (same identity) and fail
for a tampered copy.
**Tests.** Live tests.
**Pitfalls.** Unsigned code gets an ad-hoc identity (hash only).
**Refs.** —

### SEC-11 — Privacy permissions (consent database and prompts)
- Phase: P5 · Size: M · Status: todo
- Depends: SEC-10, SEC-12, SVC-01
- Files: services/privacyd/

**Goal.** Protected resources (camera, microphone, screen recording,
location, contacts, calendars, reminders, photos, Desktop/Documents/
Downloads folders, removable volumes, network volumes, full disk access,
accessibility control, input monitoring, local network) require consent;
prompts explain why (text from Info.toml); grants stored per code
identity; revocable in Settings (SET-13); menu bar indicator dot when
camera/microphone/screen capture is active.
**Done when.** A test app denied camera access gets an error; indicator
shows during recording.
**Tests.** Live tests.
**Pitfalls.** Enforce in the kernel/service, not in the app.
**Refs.** —

### SEC-12 — Authorization service (`authd`)
- Phase: P3 · Size: M · Status: todo
- Depends: SVC-04, IPC-07
- Files: services/authd/

**Goal.** Rights database ("system.preferences.network", "system.install",
"system.users.admin"…), admin password prompts as secure glass sheets,
rate limiting/back-off, `sudo` integration, timeout-based caching
(5 min), audit log of privileged actions.
**Done when.** Settings lock icons, package installs and `sudo` all go
through authd.
**Tests.** Live tests incl. brute-force delay.
**Pitfalls.** Prompts must be unspoofable (secure input + system-only
window level).
**Refs.** —

### SEC-13 — Secure input and anti-keylogging
- Phase: P3 · Size: S · Status: todo
- Depends: WS-04
- Files: services/windowserver/input/secure.c

**Goal.** While a secure text field is focused, no other process can
observe keystrokes (event taps disabled), and input-monitoring
permission is required for any global key observation at all.
**Done when.** A test key-observer gets nothing while a password is typed.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SEC-20 — Code signing
- Phase: P5 · Size: L · Status: todo
- Depends: USR-44, ELF-07, APP-00
- Files: tools/ojsign, kernel/security/codesign.c, docs/security/codesign.md

**Goal.** Signatures over a "code directory" (page hashes of executables
+ hashes of bundle resources + entitlements) with Ed25519 certificates
chaining to the ojjyOS root (Jonas's key) or a developer key; kernel
verifies pages on demand for signed code (tamper → kill); system binaries
require platform signatures; `ojsign` tool for developers; ad-hoc
signing for local builds.
**Done when.** Modifying one byte of a signed app makes it refuse to run
with a clear message.
**Tests.** Live tamper tests; host tests of signature formats.
**Pitfalls.** Key management and revocation documented (PKG-02 shares
keys policy).
**Refs.** —

### SEC-21 — App sandbox and entitlements
- Phase: P5 · Size: L · Status: todo
- Depends: SEC-20, FS-09, IPC-04
- Files: kernel/security/sandbox.c, /System/Library/Sandbox/*.sb

**Goal.** Kernel-enforced profiles: allowed file paths (container
`~/Library/Containers/<id>` + user-chosen files via powerbox UIK-29),
network client/server, IPC services by name, devices (camera/microphone
via privacyd), process spawning, JIT (W+X pages via a special mapping
flag, entitlement `jit`); built-in apps and all services get profiles;
third-party apps sandboxed by default (unsandboxed requires explicit user
approval).
**Done when.** Sandbox violation attempts are blocked and logged
("sandbox: deny file-read ~/Documents/secret.txt by com.x.app").
**Tests.** Live test suite of allowed/denied operations per profile.
**Pitfalls.** Start permissive-with-logging, then enforce per app.
**Refs.** —

### SEC-22 — Quarantine and first-launch checks
- Phase: P5 · Size: S · Status: todo
- Depends: SEC-20, FS-07
- Files: services/appregd/quarantine.c

**Goal.** Files downloaded by ojjyWeb/Mail get a quarantine xattr (source
URL, date); first open of quarantined apps/scripts shows "X is an app
downloaded from the internet. Open?", blocks unsigned code unless the
user overrides in Settings; known-malware blocklist updated with system
updates.
**Done when.** Downloaded unsigned app requires explicit approval.
**Tests.** Live test.
**Pitfalls.** Archives propagate quarantine to extracted files.
**Refs.** —

### SEC-23 — System integrity protection
- Phase: P5 · Size: S · Status: todo
- Depends: FS-18
- Files: kernel/security/sip.c

**Goal.** Even admin/root processes cannot modify `/System`, sealed
volume, kernel memory, or debug system processes; only `updated` with a
platform signature can stage system updates; can be disabled only from
Recovery (developer use).
**Done when.** `sudo rm /System/...` fails with "operation not permitted".
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SEC-30 — Data volume encryption (FileVault-like)
- Phase: P5 · Size: L · Status: todo
- Depends: FS-13, SEC-40, USR-44
- Files: kernel/fs/ojjyfs/crypto.c, services/authd/unlock.c

**Goal.** ojjyFS encryption: per-file keys (AES-256-XTS or AES-GCM for
metadata) wrapped by a volume key, which is wrapped by each user's
password-derived key (Argon2id) and a recovery key; unlock at the login
window (pre-boot unlock from the initrd UI when the data volume holds
the users); optional TPM sealing (DRV-TPM-01); enable/disable with
background conversion; secure erase = discard the volume key.
**Done when.** With encryption on, the data partition bytes reveal no
plaintext (test greps a known string); login unlocks; recovery key works.
**Tests.** Live tests incl. wrong passwords and recovery key.
**Pitfalls.** Key material zeroed after use; hibernation/swap encrypted.
**Refs.** IEEE 1619 (XTS), NIST SP 800-38D.

### SEC-31 — Secure boot chain
- Phase: P5 · Size: M · Status: todo
- Depends: SEC-20, BOOT-04, DRV-TPM-01
- Files: boot/ojjy-boot.c, kernel/

**Goal.** ojjy-boot verifies the kernel and initrd signatures (embedded
public key) before jumping; kernel verifies the sealed system volume;
optional UEFI Secure Boot with user-enrolled keys on PCs; measured boot
into TPM PCRs; boot policy (Full/Reduced/Permissive) set from Recovery.
**Done when.** A modified kernel is refused under Full security.
**Tests.** QEMU tests with tampered kernels.
**Pitfalls.** Don't brick dev builds: Permissive mode for development.
**Refs.** UEFI spec ch. 32 (Secure Boot).

### SEC-40 — Keychain (`keychaind`)
- Phase: P5 · Size: L · Status: todo
- Depends: SEC-10, USR-44, SVC-04
- Files: services/keychaind/

**Goal.** Encrypted item store per user (passwords, Wi-Fi keys, account
tokens, certificates, private keys) unlocked by the login password, item
ACLs bound to code identities, API for apps (add/query/update/delete),
system keychain for machine items, backup-safe export (encrypted).
**Done when.** ojjyWeb stores and autofills a password; Wi-Fi keys and
Mail credentials live in the keychain.
**Tests.** Live tests; host tests of the encrypted format.
**Pitfalls.** Re-encryption on password change; never log secrets.
**Refs.** —

### SEC-41 — Account database and password hashing v2
- Phase: P2 · Size: M · Status: todo
- Depends: USR-44, SVC-01
- Files: kernel/src/users.c → services/authd/accounts.c

**Context.** PBKDF2-HMAC-SHA256 at 10k iterations (deliberately low for
TCG; BETTER_OS_PLAN S3 notes the OWASP floor).
**Goal.** Accounts move into authd (user space); Argon2id (memory-hard,
parameters tuned to ~250 ms on VZ) with transparent upgrade on next
login (as done for legacy SHA-256 → PBKDF2); password policy options;
lockout back-off; account database file protected (only authd reads it).
**Done when.** Existing users log in and are upgraded; users.db no longer
readable/writable by non-authd processes (closes the documented
create|truncate compromise).
**Tests.** Host tests with fixed vectors (legacy, PBKDF2, Argon2id); live
cold-login tests.
**Pitfalls.** Migration must never lock Jonas out: keep fallback until
verified.
**Refs.** RFC 9106 (Argon2).

### SEC-42 — Passkeys (WebAuthn platform authenticator)
- Phase: P6 · Size: M · Status: todo
- Depends: SEC-40, WEB-20
- Files: services/keychaind/passkeys.c

**Goal.** Create/use passkeys in ojjyWeb (ES256 credentials stored in the
keychain, user verification by login password), passkey management in
Passwords app.
**Done when.** Sign in to a WebAuthn test site with a passkey.
**Tests.** Live test with a local WebAuthn demo server.
**Pitfalls.** —
**Refs.** W3C WebAuthn Level 3.

### SEC-60 — IOMMU DMA protection
- Phase: P9 · Size: M · Status: todo
- Depends: MEM-18, DRV-PCI-02
- Files: kernel/drivers/iommu/

**Goal.** SMMUv3 (ARM) and VT-d (x86) setup: devices can only DMA into
buffers mapped for them; Thunderbolt/external devices restricted.
**Done when.** QEMU with an emulated IOMMU runs all drivers through it.
**Tests.** QEMU tests (`-device intel-iommu`, `smmuv3`).
**Pitfalls.** Performance: map caching.
**Refs.** Arm SMMUv3 spec; Intel VT-d spec.

### SEC-61 — CPU vulnerability mitigations
- Phase: P5 · Size: S · Status: todo
- Depends: MEM-05
- Files: kernel/arch/*

**Goal.** Apply relevant mitigations (Spectre v2/BHB barriers, SSBD
controls, kernel page-table isolation on vulnerable x86 CPUs) based on
CPU identification; report status.
**Done when.** `ojjyctl security` lists mitigation states.
**Tests.** —
**Pitfalls.** Performance impact documented.
**Refs.** Arm and Intel security advisories.

### SEC-70 — Privacy report and no-telemetry policy
- Phase: P6 · Size: S · Status: todo
- Depends: SEC-11
- Files: docs/privacy.md, Settings > Privacy & Security

**Goal.** Written policy (no telemetry, no ads, opt-in only), and a
privacy report showing which apps accessed camera, mic, location, files
and network in the last 7 days.
**Done when.** Report lists accesses from the privacyd log.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SEC-71 — Family controls (optional)
- Phase: P10 · Size: M · Status: todo
- Depends: SEC-21, SET-19
- Files: services/familyd/

**Goal.** Optional managed child accounts: app limits, downtime, web
content filter in ojjyWeb, purchase/install approval via a parent admin.
**Done when.** Limits enforced on a managed account.
**Tests.** Live tests.
**Pitfalls.** Clear, honest UI; no spying features.
**Refs.** —
