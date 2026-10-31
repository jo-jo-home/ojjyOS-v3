# AGENTS.md — ojjyOS v3 (working context for AI sessions)

## LATEST (2026-10-30): HW-14 complete and safely delivered (launch-3 DONE)

Boards `046904` (OS) and `939692` (Control) done; the 3-part launch series
(`HW-12`, `HW-13`, `HW-14`) is complete. `ojjyOS.app`, `scripts/run-qemu.sh`
(`scripts/qemu_launch.py`), and ojjy Control (`GET|PUT /api/ojjyos/launch-settings`
+ React/classic Home editors) share the single version-1
`build/launch-settings.json` with SHA-256 revisions, fail-fast
`build/.launch-settings.lock`, unknown-key preservation, nonpersistent `OJJY_*`
overrides, `--dry-run`, 2x HVF vs 1x TCG/Intel scanout, bounded
`opt/ojjyOS/display` fw_cfg mode selection in `boot/display_mode.h` +
`boot/ojjy-boot.c`, and `window.backingScaleFactor` screenshot guards in
`vmapp/`. Verified: zero-warning ARM/Intel/vmapp dev + installed builds,
normal + ASan/UBSan host suites, 2149 Swift guards, 43/43 Deno + 16/16 Vitest,
9/9 cross-tool parity cases, Control↔native menu light/dark round-trip
(`build/hw-14/final3-control-native/report.json`), QEMU TCG/HVF/Intel smokes,
and 4-case native VZ launch matrix (`build/hw-14/final3-vz-launch/report.json`).
With no running VM, `main` and Jonas's `ojjyOS-v3` checkout fast-forwarded to
implementation `89bf21c` (and `ojjy-control` `main` to `ba37569`); installed
ARM/Intel/app builds preserved saved-data, `launch-settings.json`, EFI store,
and legacy disk SHA-256 byte-for-byte (`build/hw-14/preservation.json`),
`installed-vz-boot` passed, and `READY_TO_TEST.json` is written. Restart the
running ojjy Control server on `:4321` once (`./start.sh` after closing the
old process) to activate live dashboard writes to `ojjyOS-v3/build`.
Next ready P0 cards: `APP-FINDER-01`, `APP-SET-01`, `APP-TERM-01`, `NET-03`,
`SHL-02`, `SHL-03`, `SHL-04`, `QA-20`.

## LATEST (2026-10-30): HW-13 complete and safely delivered (launch-2 DONE)

Board f5c05f done, branch hw-13; the whole HW-13 card is complete.
Parts 1–4 remain intact: runtime map-sized PMM/LOW bootstrap metadata,
explicit zones, ARM TTBR1 and Intel supervisor high aliases, audited
high CPU-only callers and verified 3-GiB simultaneous ownership.
About now formats
actual PMM bytes as rounded GB plus exact MB; Monitor uses a shared tested
AppMemoryUsage projection: reserved=total-managed, used=managed-active free,
including enabled high free pages. Its bar is used/managed, not total-free
counting reservations as allocations. Removed the temporary launch warning
and stale Monitor limitation. Blue glass and Jonas credit preserved.
README/APPS and generated What's New describe real scope (legacy/DMA LOW,
protected ARM64 and audited browser/TLS high; Intel EL0 still unsupported).
New tests/vz_memory_ui.py uses private disks/bundles, screenshot OCR and
PMM/Monitor receipts: VZ2/4/8 light/dark About and Memory agree at
2047/4095/8191 MB RAM, 2027/4075/8171 MB managed. All 12 captures reviewed
in build/hw-13-part5/review-sheet.png. Initial test capture raced repaint;
fixed the test's presentation wait, no kernel workaround or skipped gate.
Normal/fatal-sanitized hosts and both zero-warning dev builds pass; Swift
guards and TCG/HVF/Intel protected/persistence smokes pass. Fresh VZ8 and
Intel4 stress own/verify 3 GiB and restore counts; high VZ8 isolation/TLS
passes. Final native USB/persistence/protected smoke, all 20 app-polish
flows, VZ2/4/8 full clock gates and fresh HVF4 3-GiB stress also pass.
40 app-polish host checks (11 new), 2138 Swift and 7 pixel guards pass.
Main and Jonas's checkout fast-forwarded to implementation 921c482 only
after all gates passed. With no running VM, ARM/Intel/app installed builds
pass with zero warnings. Saved-data SHA-256 unchanged: ARM a1bcea66...,
Intel a8bb9f64...; full legacy disk, launch settings and EFI store also
unchanged (build/hw-13-part5/preservation.json). Both installed kernels
are byte-identical to the tested dev kernels. Private delivered-image VZ8
light/dark About/Monitor passes; About capture reviewed; ready note written.
All HW-13 boxes and matching MEM HW13-A/HW13-X substeps have evidence;
MEM-03/04 themselves remain doing (full pt_*/WC/vmalloc still future).
Jonas approved publication on 2026-10-30. Public origin/main and
origin/hw-13 verified at 8c156a0 after a normal fast-forward push;
all 10 pending main commits passed gitleaks and added-line privacy scans.
Implementation 921c482, delivery 47060cc, UI acceptance b5c2958 and
completion 8c156a0 are published; this receipt changes context only.
Part-5 UI/delivery Done-when acceptance is checked with the same complete
receipts; no placeholder RAM value or skipped native gate is credited.
Execution/evidence: docs/hw-13-part5-plan.md, build/hw-13-part5/.
Next session: **launch-3 / HW-14**, shared QEMU/Control launch settings.
Do not repeat HW-13 or reset disks/settings. Read HW-14 plus HW-12's
version-1 settings contract and ojjy Control's own AGENTS first. Final
records are context-only; installed binaries still match tested kernels.
Keep private-copy tests, NTP-aware clock gates, legacy/DMA LOW policy,
and the explicit unsupported Intel EL0/network limits. Use Homebrew
OpenSSL on PATH for TLS fixtures; ask before any public push.

## LATEST (2026-10-30): part-4 pre-push double-check passed; part 4 pushed

Jonas explicitly approved pushing after a double-check. Reviewed only
76aa1a0..bc39972 against docs/hw-13-part4-plan.md. Six independent static
passes plus parent source/caller/gate audit found no actionable defect;
no production code changed. Fresh full normal/fatal ASan/UBSan hosts pass,
10 extra public paging map/unmap/neighbor/huge-leaf probes pass normally
and sanitized, repeated Intel4/HVF4/VZ8 3-GiB stress and full VZ4/8 clocks
pass. Exact source/dev-image hashes still match the part-4 receipts.
Privacy/checklist/roadmap clean. Evidence build/hw-13-part4-review/;
review scope, reuse and tooling limits: docs/hw-13-part4-review.md.
Pushed ac7acb5 and bc39972 to hw-13 with approval; verified GitHub
contained bc39972e94ea8ccd780d37eba767b544da475b22 before these review-only
records. This follow-up changes context only. Main remains 043894c,
installed images/settings/ready note untouched. Board f5c05f pauses for
**hw13-5**, not started; the previous part-4 Handoff below still applies.

## LATEST (2026-10-30): HW-13 part 4 complete, paused for hw13-5

Part 3 explicitly pushed; origin/hw-13 verified at 76aa1a0. Board f5c05f,
same worktree/branch; main stays 043894c. Intel paging.c/h now has checked
PA/canonical/alignment/flag handling, NX capability + EFER.NXE readback,
explicit PA masks, all missing LOW table levels (including final PT),
OOM rollback and bool map errors. kvm.c stages supervisor NX RAM-only
upper-PML4 aliases, publishes only complete subtrees and then enables HIGH.
52-bit-capable CPUs are accepted when actual RAM fits the 47-bit alias
span; unrepresentable RAM fails closed. Intel EL0/network remain unsupported.
memory_stress.c/h plus authenticated-admin console memtest hold 3 GiB
simultaneously using bounded LOW bookkeeping, reject duplicate extents,
pattern/check eight PA-derived words on every held page, cross-check PMM
peak accounting, free all extents/bookkeeping and require exact cleanup.
Private VZ8/HVF4/Intel4 each own 3221225472 bytes, write/verify 786432 pages,
restore LOW/HIGH counts and recover (ARM hello; Intel whoami/desktop).

BUG-1008 repaired in timer health receipt/vz_boot.py: sampled actual NTP
offset plus RTC-read uptime bracket, exact calendar expectation using C
truncation, unchanged epoch/IRQ/uptime gates. Deterministic 29-subcase
fixtures reproduce 10 old wrong verdicts then pass; wrong-calendar
mutations rejected 13/15 before, 15/15 after. Final full VZ2/4/8 tests pass
with +1230/+1376/+1533 ms NTP offsets at sample; tracker line removed.
Full normal/fatal ASan/UBSan suites pass (51 PMM, 21 ARM, 28 Intel mapper,
9 stress checks), both final dev builds zero warnings; full TCG/HVF/Intel/
native protected+persistence, ARM HVF/TCG browser+HTTPS, strict network
with real SNTP, high VZ4/VZ8/HVF4 browser/TLS/isolation and 2137 Swift +
7 pixel guards pass. Representative screenshots read. Evidence matrix.json,
source-fingerprint.json, arm-final-identical.json in build/hw-13-part4/.
ARM rebuild is kernel-byte-identical; entire disk differs only in two COFF
timestamp bytes. Matrix explicitly reuses those unchanged-code receipts;
fresh final-image VZ2/4/8 and HVF4 high checks pass. Final Intel image's
stress/smoke rerun separately after the CPU-width refinement.
Four part-4/shared MEM HW13-X boxes are now verified; full MEM cards stay
doing (general pt_*/WC/vmalloc remain open). Main/installed images/settings,
launch warning and ready note untouched. Reusable clock-contract findings
saved in apple-vz-custom-os-bringup v1.9.0. Implementation ac7acb5 committed
locally; strict checklist/roadmap and unpushed privacy scans clean. Board
f5c05f paused for part 5. No part-4 push authorization yet.

Handoff: **hw13-5 only**; take paused f5c05f.
Read card/design/part4 plan. About/Monitor currently use raw total/free;
show RAM vs managed capacity vs active free accurately (PmmStats byte units,
reserved=total-usable, managed used=usable-low_free-high_free). Compare
serial/UI at VZ2/4/8, remove temporaryMemoryNote only with current high
proof, light/dark screenshots and combined-source regression. Then safely
fast-forward main and deliver ONLY with no user VM running, saved-data/
settings/legacy hashes and private delivered-image checks. Keep custom
kernel/blue glass/Jonas credit. No full MEM-01..05 completion claim. Use
Homebrew PATH and build/hw-13-part3/venv/bin/python for Pillow live tests.

## LATEST (2026-10-29): HW-13 part 3 complete, paused for hw13-4

Part 2 pushed with explicit approval: remote hw-13 verified at 9b2e93f;
main remains 043894c. Board f5c05f, same worktree/branch. Part 3 implements
ARM RAM-only TTBR1 aliases in kernel/mm/kvm.c/h: checked PA/span/leaf
encodings, LOW table pages, 2 MiB blocks / 4 KiB boundaries and full build
rollback. ARM removes overlapping high-RAM Device identity aliases while
preserving VZ BARs, activates/readbacks TTBR1 before enabling HIGH/ANY.
memory.c/h expose checked translation and mapped-byte APIs; legacy page/
contiguous/byte APIs stay LOW. The active free getter includes HIGH only
after readiness. Protected loader/load/copy/wipe/cache accesses use aliases;
only selected browser arenas/decode scratch and HTTP TLS slots opt in.
DMA/GPU/framebuffer/renderer/other legacy buffers stay LOW. Intel still
uses LOW and explicitly does not support protected execution.

Verified exact sources: full normal + fatal ASan/UBSan hosts (51 PMM,
21 builder checks; 200000 LOW and 20000 mixed-zone random operations;
synthetic physical user-frame load/copy/wipe), both zero-warning dev builds,
native app build, full TCG/HVF/Intel and native protected/persistence smokes,
HVF browser flow. Private high_ram.py VZ4/VZ8/HVF4 each passed actual HIGH
zero/write/read/free at PA 0x100000000, 51 high-backed protected runs with
cleanup equality, EL0 alias read/write/execute permission faults and local
HTTP/HTTPS/PNG with HIGH 40/28/20/48-MiB buffers and 117496-byte TLS slot.
Representative native/QEMU screenshots read. Evidence matrix.json,
source-fingerprint.json and per-guest report.json in build/hw-13-part3/.
Full vz_boot.py at 4/8 GiB still FAILS BUG-1008 after +1493/+1418 ms NTP
correction; no clock gate changed or skipped. This is not a full VZ boot
test pass. Both part-3 and MEM-03/04 HW13-A boxes checked with that caveat;
full MEM cards (doing for this subset) and parts 4/5 remain open. No installed image/settings,
main checkout or ready-to-test note changed. Part 3 not pushed.

Handoff: take paused f5c05f for **hw13-4 only**: Intel upper-PML4 RAM-only
aliases/failure-safe walker/NXE/PA guards; simultaneous >2.5 GiB (target
3 GiB) distinct-page ownership with every-page patterns/free/recovery on
VZ/ARM HVF/Intel; deterministic NTP-aware BUG-1008 test repair and full
regressions. Read HW-13 card + docs/hw-13-memory-design.md. Keep main and
installed images untouched until part 5. Use Homebrew PATH; live Python
with Pillow is build/hw-13-part3/venv/bin/python. Part-3 commit needs a new
push yes; do not infer it from the earlier part-2 push approval.

## LATEST (2026-10-29): HW-13 part 2 complete, paused for hw13-3

Part 1 pushed on explicit approval: remote hw-13 = 3d81716; main untouched.
Board f5c05f resumed in the same worktree/branch. Baseline host suite passes
with Homebrew PATH. memory.c/h now bootstrap runtime per-region eligibility
and allocated bitmaps plus a LOW copy of descriptors, reserve live ranges,
track high capacity and expose LOW/HIGH/ANY APIs. HIGH fails closed; all
legacy callers stay LOW. PMM source has no fixed bitmap/span limit; boot
metadata and descriptor copy are reserved conventional LOW pages. Packed
descriptors use memcpy, validation rejects overlaps/overflow/bad stride;
eligible + allocated bits prevent reserved/hole/double frees.
New tests/test_pmm.c: fake 64 GiB, 260 shuffled regions/200000 operations,
LOW boundary, metadata/map/framebuffer/kernel/runtime reservations, OOM,
malformed maps and exact exhaustion/free accounting. Final expanded
38-check suite and full normal hosts pass. Final log inspection found a
recoverable UBSan error in the host-only low-endpoint helper's pointer
arithmetic despite test exit 0; parenthesized both integer offsets and
full normal/ASan/UBSan reruns pass (38 PMM checks, no reports), with
UBSAN_OPTIONS=halt_on_error=1. Sanitized scripts/test.sh
now adds -fno-sanitize-recover=all so future reports cannot pass silently.
No production code change from this host-helper fix.
Both dev builds zero warnings;
TCG/HVF/Intel private full smokes pass. VZ8 reaches main loop/device/DHCP
with `[PMM] 8191 MB RAM, 8171 MB usable`, LOW 585228 pages, HIGH 1506699
tracked/disabled, metadata 528384 bytes at 0x70000000. Full vz_boot.py
FAILS known BUG-1008 after +1118 ms NTP correction before timer health;
its clock assertion was not changed or waived (repair stays part 4).
Evidence build/hw-13-part2/ (matrix.json, native-memory-proof.json,
host-complete.log, sanitized-complete.log, source-fingerprint.json).
Both part-2 boxes verified/checked; future parts stay open, card stays doing.
Next: take paused f5c05f for hw13-3 ARM mappings and translation helpers,
then enable HIGH only after mapping readiness. No new readiness hook is
present yet, no caller migration is done, and MEM-03/04 shared boxes stay
open. No delivery/main move/installed changes. Part 2 not pushed yet.

## LATEST (2026-10-29): HW-13 part 1 measured/designed, paused for hw13-2

Board `f5c05f`, separate worktree `ojjyOS-v3-hw-13` / branch `hw-13`,
base `043894c`; main checkout and existing worktree edits are untouched.
Part 1 of five is complete: inspect, measure private memory maps, write
Decision/plan/Handoff; no kernel code is changed in the final tree.
HW-12 dependency verified in card and launch settings/`vz_boot.py --memory`.
Baseline `scripts/mkfs.sh` then `scripts/test.sh` passes with
`PATH=/opt/homebrew/bin:/usr/local/bin:$PATH`. The first attempt used
system LibreSSL 3.3.6 and failed `test_tls_local.py` certificate generation;
Homebrew OpenSSL 4.0.3 passes 14/14 cases without source edits. Evidence
`build/hw-13-part1/`: memory-maps.txt (all 242 descriptors), summary.json,
raw serial logs, retained probe.patch/scripts and preservation.json.
VZ 2/4/8 GiB initial free pages: 519159/585325/585325. ARM HVF/Intel TCG
4 GiB: 781935/780223. VZ 8 GiB leaves 5885.543 MiB reclaimable high RAM
unused; BAR0s move to 0x2800x0000. All five reach main loop. Full VZ
2 GiB test passes; VZ 4/8 fail an existing clock assertion after NTP
correction, not a memory boot fault (filed BUG-1008, part 4 regression work).
No gate weakened. Probe sources removed, source image hashes unchanged;
both uninstrumented dev builds zero warnings, full normal/sanitized host
suites pass. No installed disk/settings/app changed or ready-to-test note.

Decision/complete caller audit: docs/hw-13-memory-design.md; D-026 records
map-sized LOW boot metadata, explicit LOW/HIGH/ANY APIs and an opt-in
privileged RAM-only high alias (ARM TTBR1 / Intel upper PML4). DMA and
legacy allocators remain LOW; protected pages and audited CPU-only browser
arenas/TLS pool may opt in after mapping readiness. MEM-03/04 own named
HW13-A/HW13-X preparation boxes; their full cards remain todo. HW-13 card
has all five tagged parts and exact Handoff; TODO marks it partial.
Next **hw13-2 only**: take paused board f5c05f, rerun the baseline with
Homebrew PATH, implement runtime PMM metadata/zone APIs plus fake-map tests;
keep high allocation disabled until part 3. Original firmware map addresses
are themselves high at 4/8 GiB: copy them LOW before switching mappings.
Do not fast-forward main or deliver before hw13-5. Part 1 changes are docs
only; raw ignored evidence is local, the tracked design preserves results.

## LATEST (2026-10-28): NET-02 review: a web page could add a certificate exception (DONE, delivered)

Board f0ce7f, worktree `ojjyOS-v3-check-net02` / branch `check-net02` off
main a374079. Jonas asked for a check of NET-02 (board 27c8ad).
Re-verified at a374079: `scripts/test.sh` + ASan/UBSan pass, both dev
builds 0 warnings, roadmap check 698/0, checklist clean, gitleaks on the
pushed range 5402f4e..a374079 clean (no paths/emails; all 12 commits carry
a `Co-Authored-By` trailer from before global rule 10, already public, left
as is), READY_TO_TEST.json matches the installed image.
**Found + fixed (security):** any web page could open `ojjy:` URLs through
a link, a GET form or `<meta http-equiv=refresh>` (no click), so
`ojjy:proceed?url=https://bank.example` silently added a certificate
exception (a network attacker can inject that into any http page), and
`ojjy:settings?cookies=1` / `history?clear=1` / `bookmarks?remove=N` /
`settings?search=` changed settings (reachable since NET-02's url.c query
fix). Fix: `url_page_may_open()` in `web/url.c` (web pages may open only
plain ojjy: pages; ojjy:proceed only from the certificate warning page;
ojjy: pages unrestricted), used by `page_may_open()` in `ui/browser.c` for
links (incl. Open Link in New Tab), forms and meta refresh; logs
`[WEB] blocked page navigation to ojjy:<page>`; HTTP redirects to non-web
schemes now fail in `web/http.c`. Test: `tests/test_web.c` +9 checks (red
with the old allow-everything rule: 4 FAILs), host + ASan/UBSan pass, both
dev builds 0 warnings. New live step 1b in `tests/qemu_https.py`
(`web_server.py /sneaky?to=` page with a meta refresh to ojjy:proceed).
**Done (2026-10-28, afternoon).** Live: `qemu_https.py` 9/9 under HVF
incl. new step 1b ("page cannot add an exception", "host still warns after
the hostile page"; screenshots read, evidence `build/check/https-hvf3`),
`qemu_browser.py` HVF, smoke ARM HVF + TCG + Intel (`build/check/live.log`),
host suite again. The first 1b run crashed in the test typer (no key for
`%`): `tests/qemu_smoke.py` types `%` as Shift+5 now. Card NET-02 has a
review `**Log.**` line. main fast-forwarded to the review commits; Jonas's
`ojjyOS-v3` checkout (branch ui-polish-3, = main) fast-forwarded; no VM was
running; installed ARM image rebuilt 0 warnings (`build/check-net02-deliver-arm.log`),
saved user data SHA-256 identical before/after (a1bcea66…), old disk backed
up by finalize_image in `build/aarch64/backups/`; delivered image passed
`qemu_https.py` 9/9 on a private copy (`build/check-net02-delivery`);
READY_TO_TEST.json rewritten. Intel installed image not rebuilt (not
needed to try it). Not pushed yet: ask Jonas (repo is PUBLIC).
Leftovers for Jonas to decide (not urgent): the uncommitted lines in
`ojjyOS-v3-net` (netcmd.c [CLOCK]) and `ojjyOS-v3-qa` (release.html
TextEdit) are already on main, and every old worktree branch is fully
merged into main, so those worktrees can be removed once Jonas agrees.
Next card: NET-03 (TLS 1.2 client).

## LATEST (2026-10-27, night): NET-02 — HTTPS now proves who the server is

Board 27c8ad, worktree `ojjyOS-v3-net-02` / branch `net-02` off main
5402f4e (net-web has someone else's uncommitted netcmd.c line: untouched).
Card `roadmap/13-networking.md` NET-02 (Track N's N2+N3). Commits
6419311 (plan), 28d724d, af937a8, 811c041, 14487a9, ea9cc12, 3dff494,
a08c72a + docs.

- `crypto/x509.c/h`: strict DER + X.509 parser and signature checks
  (PKCS#1 SHA-256/384/512, RSA-PSS, ECDSA P-256/P-384). `x509_chain.c`:
  leaf checks (key size, RFC 6125 name incl. wildcard/IP SAN, dates with
  48 h slack only before the first NTP sync, EKU, keyUsage, critical
  extensions) then a path search through the sent certificates in any
  order to a root (CA, pathLen, keyCertSign, EKU, name constraints).
  `roots_data.c`: 51 roots from `tools/mkroots.py` (macOS store or
  `--pem`; Entrust left out). THIRD_PARTY.md records them.
- `net/tls.c`: checks the chain, verifies CertificateVerify (PKCS#1
  refused, scheme must fit the key), alerts on failure, and accepts
  Finished ONLY after CertificateVerify (before, a server could skip
  both). `tls_start_ex(TLS_ALLOW_UNTRUSTED)` = Visit anyway / curl -k;
  never skips the signature. Ed25519 no longer offered.
- Browser: blue "This connection is not private" page (reason, Go Back =
  `ojjy:back`, Visit anyway = `ojjy:proceed?url=` per-host exception until
  restart, browser fetches only via `HttpFetch.use_exceptions`), lock /
  crossed lock + "Not secure", page info with certificate/issuer/root.
  Terminal `curl` refuses with the reason; `-k`/`--insecure` warns.
- Bugs fixed on the way: `ecdsa_verify_raw` used the whole hash mod n (a
  P-256 key signing with SHA-384 never verified; FIPS 186-4 truncation);
  `url.c` dropped every `ojjy:` query, so Settings buttons never worked.

Verified: test_x509.c 374, test_tls_verify.c 46 (replayed + re-encrypted
forged flights; red with the old rules), test_tls_local.py 14 handshakes,
test_web.c +5; host + ASan/UBSan 22/22; both dev builds 0 warnings;
qemu_https.py --internet 17/17 under HVF and TCG; qemu_browser.py; smoke
ARM TCG/HVF + Intel. TLS verify per handshake: TCG <= 252 ms, HVF 3-38 ms.
Screenshots read. Evidence `build/net-02-*`. Limits: no revocation
(OCSP/CRL), no TLS 1.2 (NET-03: badssl.com + craigslist, BUG-1001),
policies not enforced, exceptions per host. Card DONE: with Jonas's OK the
badssl.com Done-when item (TLS 1.2 only) moved to NET-03. Pushed with
Jonas's OK (GitHub main 246a5aa, then this closing records commit).
Rebuild vectors: `python3 -I tests/gen_x509_vectors.py > tests/x509_vectors.h`
(also gen_x509_real.py needs internet, gen_tls_flight.py).
Delivered: main fast-forwarded, Jonas's `ojjyOS-v3` checkout fast-forwarded
(no VM running), installed ARM image rebuilt 0 warnings with saved data
byte-identical (user-data SHA-256 unchanged), delivered image live-tested
on a private copy (build/net-02-delivery), build/READY_TO_TEST.json has
the clicks. Intel installed image not rebuilt (not needed to try it).
Next: NET-03 (TLS 1.2 client: craigslist + the badssl.com cases).

## LATEST (2026-10-27): new GitHub README, 2026 copyright, repo description, privacy check

Board c624c2 (taken over from OpenCode with Jonas's OK), worktree
`ojjyOS-v3-readme` / branch `readme-2026` off `main` 2f4bfd9. Docs only:
no kernel/app code changed. Checklist: TODO.md "README + GitHub
presentation + privacy check".

- `README.md` rewritten for GitHub: centred hero, badges, highlights,
  screenshots, current features (network stack, TLS 1.3, SNTP, ojjyOS
  Web, PBKDF2 accounts, native VZ app were missing or listed as planned),
  an honest "What it can't do yet" list, quick start (native app / QEMU /
  Debian), tests, collapsible shortcuts and commands, layout, docs, credits
  and an Apple trademark disclaimer. "Created by Jonas Lee" kept.
- Screenshots `docs/images/{desktop-light,desktop-dark,web,launchpad}.jpg`
  (788 KB) come from a dev build with NO `wallpapers/` folder (serial
  `[WALLPACK] 0 desktop pictures`), so they show the built-in ojjyOS
  Waves only. Never use captures from checkouts that have
  `wallpapers/local/`: those show Apple's pictures. Capture script:
  QEMU HVF 2880x1800 on a fresh private disk copy, account "Jonas Lee"/jonas.
- Copyright 2024 -> 2026 in README.md and LICENSE.
- GitHub description set with `gh repo edit` (it was "ojjyos v3"); this is
  live already. The README and images are live only after a push.
- Privacy: gitleaks clean (130 commits); no history blob contains the Mac
  account name or personal email. `lighttahoe.png` (Apple's Tahoe Light
  wallpaper, public since 2026-02-01) was removed from the tree on Jonas's
  OK; a byte-identical copy is in `~/Developer/ojjyOS-kernel/apple-reference/`
  and `/lighttahoe.png` is git-ignored. It still exists in old history
  (erasing it = force push; only if Jonas asks). 5 github.com web commits
  carry the personal email in their metadata (GitHub email-privacy setting).
- New bug BUG-1007: Finder (PID 5) stays owned by `guest` after first login.

Verified: host suite exit 0 (build/readme/host-tests.log, 698 cards
0 errors, THIRD_PARTY OK); dev ARM build 0 warnings; README links/anchors
resolved by script and rendered through GitHub's markdown API, then looked
at. Both dev builds 0 warnings after the theme.c comment change. Pushed
with Jonas's OK: GitHub main = 2f29447 (+ this records commit), live page
checked. Next: APP-FINDER-01 (also BUG-1007).

## LATEST (2026-10-27): APP-CAL-01 complete and safely delivered

Board bc2ad1, UI track desktop-polish; code c0f4ee3/6bbbcad/7603802.
Native original-kernel UI; blue Liquid Glass, fonts/icons and Jonas credit
preserved. Calendar has a glass header/full-date today circle/cell-bounded
event chips; Calculator a 72px readout, equation history, 180ms feedback
and shared input/hit model (ui/calculator.c/h). About has a hero/version
card and readable credit. Monitor has Processes/Memory/Frames tabs, zebra
rows, bounded memory bars and the last 48 real renderer durations
(ui/app_metrics.c/h); task ticks are explicitly not CPU utilization.
Monitor refreshes once/second only while authenticated and not minimized.
All four apps have content-aware minimum sizes in both pointer paths.

Evidence build/app-cal-01/: 29 host checks + 5000-key fuzz normal/sanitized,
full host + ASan/UBSan, both dev builds zero warnings, 20 light/dark live
flows (incl. guest-symbol proof that minimized refresh stops/resumes),
TCG/HVF/Intel protected/persistence regression (EL0 ARM only), native
protected three-boot smoke, 2137 Swift + seven pixel gates, apps-check
and native four-app keyboard/tab checks pass. Representative QEMU/native
light/dark and minimum-size captures visually read. APPS.md, DESKTOP.md,
DESKTOP_PLAN.md, What's New and TODO.md updated. Limits: last 48 frames,
not FPS/CPU%, no Monitor row scrolling; high-RAM allocator stays HW-13.

Tested code/docs fast-forwarded to main and Jonas's checkout as a9bb7da;
installed ARM/Intel images and native app rebuilt with zero warnings and
no user VM running. Saved-data extents, legacy disk and launch settings
match the before hashes (installed-preserved.json); installed kernels/app
match tested artifacts byte-for-byte (installed-artifacts.json). Private
saved-data VZ boot passes, installed-image app flow passes 20/20, and the
installed checkout's host suite passes. build/READY_TO_TEST.json in the
main checkout has concrete clicks. Final commit only finishes records;
no tested code changed. Card complete; next ready card APP-FINDER-01.
Original BUG-1006 remains open; Time Machine cancelled. Nothing pushed.

## LATEST (2026-10-27): DNS continuation started; backup cancelled

Jonas confirmed the empty portfolio directories are intentional and
cancelled Time Machine. No backup/configuration or portfolio edits made.
Continuing BUG-1006 in `ojjyOS-v3-review` (net-web's existing netcmd edit
is not ours). Baseline `d155a71`; checklist in TODO.md "Bugs 2026-10-27".
Investigate with DNS packet evidence and failing host deadline tests before
any fix; do not confuse a separate proven retry defect with the original
external-site failures. Next: DNS proof/fix/regression, then select the
first ready daily card (currently APP-CAL-01). Only ask push at the end.

DNS progress: unchanged-image replay loaded all six affected sites HTTP
200; QEMU packet capture showed valid DNS answers (build/bug-1006/).
Original BUG-1006 remains unproven/open. Separate deterministic bug found:
dns_poll abandoned attempt four on its next tick, before a reply could
arrive. Three failing-before host assertions now pass with the original
four-query/ten-second budget preserved. 52/52 network assertions pass
normally and with ASan/UBSan; full host/sanitized suites and both builds
pass. TCG/HVF protected smokes pass; strict internet network check passed
with a real -47 ms NTP correction, DNS and HTTP. Final six-site replay is
6/6 HTTP 200 with valid captured DNS replies; hn.png visually reviewed.
Code `53d6c7c` fast-forwarded to main; original BUG-1006 stays open with a
packet-capture Handoff in TODO.md. Installed delivery will follow the daily
UI task if no user VM is running. Next: APP-CAL-01 in the UI track.

## LATEST (2026-10-27): completed-work audit — fixes and full matrix verified

Board `1674cc`, worktree `ojjyOS-v3-review` / `verify-completed-work`,
baseline `d74efea`. Jonas requested review, fixes as needed and push.
Plan/evidence: `REVIEW_COMPLETED_WORK.md`, `build/review/`. Fixed TIM-04
automatic retry/6 h resync (IDLE never woke; deadline mixed Unix and
uptime), DHCP idempotence, peer/port/expiry checks, fractional-offset
oscillation, missing/out-of-order timestamps and era-boundary unfolding.
Added the date serial receipt (same uncommitted line in net-web was left
untouched), UTC-host-year live assertion and fresh-image/native tests in
the release runbook. Before-fix failures retained; 61 clock assertions,
all host suites normal + sanitized, both dev builds zero warnings,
2137 Swift + 7 pixel gates, protected TCG/HVF/Intel and native persistence
smokes, 5 native RAM boots, 4-case launch/3-screen matrix, TextEdit/Notes
checks and design/apps tours pass. Web tour completed: 7 HTTP 200 loads,
6 DNS failures and the known craigslist handshake failure. PNG profiles plus representative
visual reads performed. Fresh-context adversarial review: no new finding.
Initial external NTP was unreachable; the separate final strict --internet
run PASSED with a real -64 ms correction from pool.ntp.org, plus DNS/HTTP.
No network-success gate was weakened. Known craigslist gap remains. Filed
BUG-1004 (relative build directory), BUG-1005 (Terminal overflow) and
BUG-1006 (six web-tour DNS failures; cause not established).
Installed images/settings and other worktrees' edits are untouched; no
release/version bump or HW-13/14 work. Code commit `569bb22` was pushed
with Jonas's approval and verified on public GitHub main; docs audit is
current, gitleaks/personal-path scans clean. This follow-up only records
completion; no tested code changed. Next: BUG-1006 DNS diagnosis or the
next requested roadmap card. Local Pillow venv is build/review/venv.

## LATEST (2026-10-27, night): THREE more cards done — QA-30 bug tracker, QA-40 release runbook, TIM-04 SNTP clock

Board id `b774d3`, worktree `ojjyOS-v3-net` / `net-web` (brought to
`main` = `a2f4bb4` first; no other session was active). Also committed
the regenerated `sysroot/System/Web/release.html` left over from
APP-TEXTEDIT-01 so builds leave `git status` clean.

**QA-30.** `bugs/OPEN.md` is the in-repo bug tracker (one line per
bug: ID/symptom/steps/area/severity/card) with session rules in
`bugs/README.md`; AGENTS.md's start-here checklist (rule 2b) now
requires filing bugs a session finds but does not fix. Seeded with
BUG-1001 (craigslist/TLS 1.2 -> NET-03), BUG-1002 (no caret click in
the editor), BUG-1003 (64-line read-only cap).

**QA-40.** `docs/release.md` is the copy-pasteable release runbook:
freeze, full matrix (host + ASan/UBSan, both dev builds 0 warnings,
smoke TCG/HVF/Intel, tours, VZ incl. build-vmapp.sh + Swift checks),
SEC-02 security pass, DS-15 design gallery, version sync in the four
places of AGENTS.md §8, What's New via `mkfs.sh`, data-preserving
delivery with hash verification, local tag, ready-to-test note and a
release summary — plus the "3.2 runbook" section. RELEASE.md and
AGENTS.md (rule 2c) point at it.

**TIM-04 (Track N's N4).** New `kernel/src/clock.c/h`: the NTP
correction is applied inside both platform `rtc_read_time()`
implementations (signatures unchanged — the menu clock, Auto
appearance and everything else reading the RTC picks synced time up
for free). After the DHCP lease (hook inside `net.c`, no main.c
change) the SNTP client resolves time.apple.com (fallback
pool.ntp.org) and validates replies hard: mode/stratum/version, the
originate-echo transaction check, non-zero transmit, a plausible-time
window, a 10-year skew cap with an exception for implausible clocks
(the 1970 no-RTC boot), and 2036-era rollover. Resync every 6 h;
offline it backs off exponentially and logs a clean give-up. Terminal
gains `date` (corrected time + one `[CLOCK] date` serial line) and
`ntp` (forces a sync). Verified: `tests/test_clock.c` 41 checks host
+ ASan/UBSan; `tests/qemu_net.py` PASS under HVF **with a real
`[CLOCK] NTP offset +963 ms from time.apple.com` over the internet**;
smoke TCG + HVF; both dev builds 0 warnings; `tests/test_net.c` stubs
the two hooks so its captured frames stay clean. Evidence
`build/tim-04-*`. Limits: resync not persisted across boots (TIM-05
would write the RTC back); QEMU's RTC already carries host time, so
visible offsets are small.

Next ready P0 cards: APP-CAL-01, APP-FINDER-01, APP-SET-01, APP-TERM-01,
SHL-02/03/04.

## LATEST (2026-10-27): HW-12 DONE — native launch menu delivered safely

Board `76a30d`, worktree `ojjyOS-v3-hw-12` / `hw-12`. HW-10 split into
HW-12/13/14 (those exact IDs); HW-12 is complete. Rebased onto parallel QA
`ac5eb6c`, preserving its records/tool and LATEST block, retested both arches
and normal/sanitized/Swift suites, then fast-forwarded main and Jonas's
`ui-polish-3` checkout. Nothing pushed.

Native settings: `vmapp/LaunchSettings.swift` (bounded JSON, RAM validation,
atomic temp+rename), `LaunchWindow.swift` (blue native glass, Jonas credit),
`main.swift` (startup/Option/Cmd+, pending-only Save, configuration/geometry).
`build/launch-settings.json` lives next to the app; builders/tests preserve it.
Defaults 2 GB, 1 CPU, 1440x900 Retina, NAT, windowed, show menu. Offered
screens 1440x900 / 1280x800 / 1600x900, each with inspected live boot and
pixel-aligned input. `--start`/`--control` skip menu; memory/screen overrides
are per-run. Interactive async start errors return to a visible retry window.

Small kernel prerequisites only: retain the 68-byte packed BootInfo header
before owned tables (3 GB firmware metadata alignment panic); expand Device
MMIO coverage 4–16 GiB because 8 GB VZ BARs move above 8 GiB. PMM bitmap and
high-RAM Normal mappings are UNCHANGED. PMM raw totals 2045 vs 3069 MB at
2/3 GB, actual free 2028 vs 2286 MB. The temporary 2.2 GB note stays.

Verified: 2137 Swift checks incl. 2000 JSON mutations, seven pixel gates,
AppKit control/error fixtures + opaque light/dark captures; all five RAM
probes; four-case native launch lifecycle; native three-boot protected/input/
persistence smoke; QEMU TCG/HVF protected and Intel smokes; normal + ASan/
UBSan host suites; both rebased dev builds and delivered app/ARM build zero
warnings; roadmap 698 cards / 0 errors; strict checklist no PROBLEM.
Evidence `build/hw-12-*` in this worktree. Installed app executable and ARM
kernel are byte-identical to tested artifacts. Installed ARM/Intel user data,
legacy image, absent launch settings and pre-existing release.html unchanged:
`build/hw-12-installed-before-verified.json`. Delivered-image private 3 GB VZ
boot passes. `build/READY_TO_TEST.json` in the main checkout has exact clicks.
UI review: ship (existing identity preserved); reusable VZ findings saved in
apple-vz-custom-os-bringup v1.6.0. Next: launch-2 / HW-13 real high-RAM use,
then launch-3 / HW-14 QEMU + ojjy Control shared settings.

## LATEST (2026-10-27, later): QA test day on `main` = `9d90a23` — EVERYTHING GREEN

Board id 188713, worktree `ojjyOS-v3-qa` / `qa-2026-10-27` (only records
and the new `tools/qa_shots.py`; no OS code changed). Full matrix, logs
and screenshots in `build/qa-2026-10-27/`:

| Check | Result |
|---|---|
| Dev builds aarch64 / x86_64 | PASS, 0 warnings |
| `bash scripts/test.sh` (host, 20 suites) | PASS |
| ASan/UBSan host run | PASS |
| `roadmap.py check` | 695 cards, 0 errors |
| Smoke TCG / HVF (fresh private disk) | PASS / PASS |
| Design tour HVF (18 shots) | PASS |
| Apps tour + apps check HVF (22 shots) | PASS |
| Web tour HVF (16 pages) | PASS (craigslist = known NET-03 gap) |
| Native VZ `vz_boot.py` | PASS |
| Native VZ `vz_smoke.py` | PASS |

Every tour screenshot was read with the new `tools/qa_shots.py` (ink
profiles + spot ASCII rendering; no blank captures, expected elements
present, the TextEdit wrap paper verified at its exact geometry). **No
failure and no visible glitch, so nothing needed a fix.** Known gaps
recorded in `TODO.md`'s "QA 2026-10-27" section: craigslist needs TLS
1.2 (NET-03), the editor has no click positioning, documents over 64
lines open read-only. Installed images were rebuilt data-preserving
from the same source as part of the day.

## HW-12 execution history (2026-10-27; superseded by the completed LATEST above)

Board `76a30d`, worktree `ojjyOS-v3-hw-12`, branch `hw-12`. HW-10 now
depends on HW-12 (native launch menu), HW-13 (real high RAM), HW-14 (shared
QEMU/Control settings) and SMP-01. No high-RAM allocator work in part 1. Other
active QA session owns its own worktree; main has an existing release.html
edit, which is not ours. Roadmap validation precedes implementation.
HW-12 is doing with four checkbox steps in roadmap/24-hardware-platforms.md:
settings+guards, native window, live wiring/matrix, final regression/delivery.
Baseline host suite passed after generating system.ojfs; Swift baseline 102
guards passed, Python pixel gates needed Pillow (local build/hw-12-venv).
Step 1 green: LaunchSettings.swift defines safe JSON/default/clamp, VZ/half-host
limits, atomic temp+rename persistence; Options supports start/memory/screen.
2132 Swift checks incl. 2000 JSON mutations and five pixel gates pass;
app builds warnings-as-errors. Step 2 green: LaunchWindow.swift (native
vibrancy, blue title/accent, RAM/screen/network/fullscreen/startup controls),
Option recovery, Cmd+, and save-only pending settings while active. Seven
AppKit interaction guards in tests/test_launch_window.swift pass; inspected
build/hw-12-launch-{light,dark}-final.png. Both dev builds clean (put
/opt/homebrew/bin on PATH for nasm). Step 3 live testing uncovered two
prerequisites, fixed without implementing HW-13: copy the 68-byte packed
BootInfo header into kernel storage before switching page tables (3 GB
boot_epoch access alignment panic, rtc_read_time LDUR at offset 60); widen
only Device/MMIO entries 4–16 GiB because VZ 8 GB BARs move to 0x2800x0000.
2048/3072/8192 private probe boots pass after fixes. New live lifecycle
matrix validates pending-only saves, all three screens, network-off, full-screen
startup and show-menu-off direct boot (build/hw-12-launch-verified/report.json).
PMM totals 2045 vs 3069 MB; free 2028 vs 2286 MB, so HW-13 still needed.
Full-screen startup must yield to AppKit's event loop; inline callbacks leave
busy=1/stage=0 indefinitely. Tests account for letterboxing at unchanged 3px
pointer tolerance. Final UI-test recaptures reject alpha-only show frames.
Native three-boot protected/persistence and ARM TCG/HVF protected smoke pass.
Evidence build/hw-12-*; exact final-source matrix/checklists still running.
Async startup error recovery now reopens interactive settings (never automation).
Reusable findings saved to apple-vz-custom-os-bringup skill v1.6.0.
Step 3 committed as `6316c80` (pre-rebase). Exact final-source four-case VZ
matrix, opaque light/dark/error captures, 2137 Swift + seven pixel gates,
normal/sanitized host suites, both zero-warning builds and VZ/TCG/HVF/Intel
smokes pass. Source/settings unchanged. Final delivery ff refused because
parallel QA advanced main to ac5eb6c (docs/test tooling only); rebase preserves
both LATEST blocks, then rerun required gates before installing. No force.
Next: rebase/retest, finish step 4, preserve installed data/settings and ready note.

## LATEST (2026-10-27, night): THREE cards done — TextEdit soft wrap (APP-TEXTEDIT-01), third-party register (DOC-03), repo hygiene (QA-22)

Board id 42b286, worktree `ojjyOS-v3-ui` / `desktop-polish`, all three
published to `main` by fast-forward.

**APP-TEXTEDIT-01.** The shared editor core (TextEdit + Notes) moved to
`kernel/src/ui/textedit.c/h` and `TEXTEDIT_LINE_MAX` rose 80 -> 256, so
paragraphs are editable and the read-only rule is now "> 64 lines or
lines > 255 cols". Soft word wrap: greedy break after the last space in
the window, hard break for wide words, min width 16, one row per empty
line; `scroll`/Up/Down/PageUp/PageDown work in visual rows with a
remembered goal column; Home/End wired (they were accepted but missing).
Status bar shows word + character counts; explicit saves push a
notification and log `[TEXTEDIT] Saved '<name>' N bytes, W words, C
chars` (Notes autosave stays silent). Four old editor bugs fixed, all
caught by the new host tests: mid-line Backspace truncated everything
right of the caret; Enter at a line end could re-show stale grid bytes;
Shift+Left moved the selection instead of extending it; a join that
would not fit truncated the tail (now refused). Verified:
`tests/test_textedit.c` (66 checks + 500-case fuzz, host + ASan/UBSan),
`tests/qemu_textedit.py` TCG+HVF (ink-row wrap proof, exact count logs),
smoke TCG+HVF+Intel, both dev builds 0 warnings. Evidence
`build/app-textedit-01-*`.

**DOC-03.** `THIRD_PARTY.md` (92 lines) registers every third-party
asset: Inter + JetBrains Mono (SIL OFL 1.1, sources + licence files),
the generated font data derived from them, the Apple wallpapers policy
(`wallpapers/local/` only, never committed or shipped), and
packages/CA roots/tz/dictionary/Unicode placeholders per D-023. It ships
read-only in the image as `/System/Legal/THIRD_PARTY.txt` (doubles as
the editor's read-only fixture — the Notes live test's old >79-column
fixture is editable now) and `tools/check_third_party.py` validates it
from `scripts/test.sh`.

**QA-22.** Build products live under `build/` now: kernel objects in
`build/obj-<arch>/` (per-arch, `make clean` also sweeps the legacy
in-src objects), host tools build into `build/tools/` and `mkfs.sh`
calls them there (`tools/` keeps sources only). New
`scripts/clean-icloud-dupes.sh` lists the "name 2.ext" conflict copies
and deletes them only with `--yes`, only untracked, only for
build-output extensions (removed the 4 stale ones here);
`.gitignore` gained the missing duplicate patterns. Proof: both arches
built twice 0 warnings, then `git status --porcelain` empty and zero
`*.o`/`*.d` under `kernel/src`. Limits: boot/'s `ojjy-boot.obj`/`.efi`
and the protected `.elf` samples still build in place (both gitignored;
the samples must sit inside sysroot for packing).

Next ready P0 cards: QA-30, QA-40, TIM-04, APP-CAL-01, APP-FINDER-01.

## LATEST (2026-10-26, evening): APP-NOTES-01 done — Notes sidebar, New Note, autosave

First card taken with the new workflow (board id 809980, Track D worktree
`ojjyOS-v3-ui` / `desktop-polish`, fast-forwarded to `main`). Notes now
keeps one file per note in `~/Notes`: sidebar (title + first line),
"+ New Note" / Cmd+N, autosave 1 s after typing stops (the old app rewrote
`~/Documents/Notes.txt` and re-indexed Spotlight on EVERY key) and at once
on switch/new/close/lock/Cmd+S. First launch copies the old
`Documents/Notes.txt` in (original kept). Spotlight indexes `~/Notes` (index
16 -> 32 files) and opens notes in Notes. Where: logic in the new
`kernel/src/ui/notes.c/h` (VFS only, host test `tests/test_notes.c`, 91
checks incl. preview fuzz); UI in `compositor.c` (`draw_notes_window`,
`notes_*` block after `textedit_handle_key`, `notes_autosave_tick` at the
top of `compositor_tick`); `hook:` in `main.c` `save_persistent_state()`
calls `compositor_save_documents()` so sync/shutdown/restart (also from the
console, which runs no compositor ticks) never lose the last second.
Two shared-editor bugs fixed on the way (TextEdit too): every load + save
added an empty line; files over 64 lines x 79 columns were silently cut on
save — they now open read-only (shown cut down, edits/Save refused).
Verified: `tests/qemu_notes.py` (new, 2 boots, 9 checks) PASS on TCG and
HVF, smoke TCG + HVF + Intel PASS, host + ASan/UBSan suites pass, both dev builds 0
warnings, screenshots (light, dark, read-only, Spotlight) looked at. Also
marked 28 pre-checklist ticks `(legacy)` so `checklist.py --strict` is clean.
Limits: list sorted by title (RAMFS has no timestamps), 48 notes listed,
editor still 64 x 79 (word wrap is APP-TEXTEDIT-01), no delete/rename in
Notes (use Finder). Next ready P0 cards: APP-TEXTEDIT-01, DOC-03, QA-22.

## LATEST (2026-10-26, later): history cleaned before the first push since Feb

Before pushing to the PUBLIC GitHub repo, a review (gitleaks: no secrets;
manual: personal info, big/unneeded files, Apple wallpapers) found Jonas's Mac
account name inside absolute paths in 9 docs. Unpushed history (87 commits)
was rewritten with git-filter-repo `--refs ... ^origin/main` (paths now
`~/...`; the 4 commits already on GitHub are untouched). **All commit ids
after GitHub's `07e1e69` changed**; references in the current docs were
remapped (old→new map: `.git/filter-repo/commit-map`). Backups of the old
history: `~/ojjyOS-rescue-2026-10-25/ojjyOS-v3-before-history-cleanup-2026-10-26.bundle`.
Also untracked the compiled host tools (`tools/mk*`, built by tools/Makefile)
and `.Rhistory`. Never write absolute `/Users/<name>/...` paths into tracked
files: use `~/Developer/...`.

## LATEST (2026-10-26): project moved to ~/Developer after the iCloud loss

On 2026-10-25 the whole `~/Desktop/Desktop Jonas` folder was removed from
iCloud from another device; Jonas's dad had a copy and put it back on
2026-10-26. It was merged into **`~/Developer/ojjyOS-kernel/`** (not synced
by iCloud): main folder `ojjyOS-v3` [ui-polish-3], worktrees `ojjyOS-v3-ui`
[desktop-polish] and `ojjyOS-v3-net` [net-web] (`git worktree repair` run).
History is complete through `4675f75`; `git fsck` clean; `bash
scripts/test.sh` passes; roadmap check 695 cards, 0 errors. Files from the
restored copy that were older than the committed ones are parked in
`~/Developer/.merge-hold-2026-10-26/`. Old absolute paths in this file that
say `~/Desktop/Desktop Jonas/...` now mean `~/Developer/...`.

New workflow (both Claude Code and OpenCode): `/home`, `/wrapup`, `/backup`,
`/idea`, `/teach`, `/ojjyos` skills; tasks are checkbox lists ticked only with
evidence (`~/.claude/skills/home/scripts/checklist.py --strict`); shared AI
board `~/Developer/AI-BOARD.md`; ask Jonas before every push. Dashboard
**ojjy Control** (`~/Developer/ojjy-control`, http://127.0.0.1:4321) shows a
"ready to test" light: after building a visible change into the installed
image, run `~/.claude/skills/ojjyos/scripts/ready_to_test.py` (ojjyos skill).

## >>> LONG-TERM PLAN (2026-10-22): read `roadmap/README.md` <<<

Jonas asked for a detailed five-year roadmap to make ojjyOS a full modern
OS (macOS Tahoe Liquid Glass look; best of macOS, Windows, Linux and Arch;
Windows and Linux app compatibility; a ChromeOS-style browser "ojjyWeb").
It lives in **`roadmap/`**: ~700 cards (one per driver, app, settings
pane, control, subsystem step), each with context, steps, "Done when",
tests and pitfalls. Start with `roadmap/README.md` and
`roadmap/00-how-to-work.md` (session protocol + Definition of Done).
Tool: `python3 roadmap/tools/roadmap.py next|show ID|deps ID|critical|check|status`
(the check also runs in `scripts/test.sh`). To hand a card to an AI, use
`roadmap/templates/session-prompt.md`. Short-term P0 cards point at
`DESKTOP_PLAN.md` / `NET_WEB_PLAN.md`. New decision D-023: the base system
stays original; open-source programs may be ported only as optional
ojjypkg packages.

## LATEST (2026-10-19, later): Track D — Chrome-like browser ("ojjyOS Web")

Jonas asked for a browser that feels like Chrome. Real Chrome cannot run
on this kernel (why: `BROWSER_CHROME_PLAN.md` §0), so a new engine and
shell were written; the plan file's Log section lists every step with
receipts. Ownership: Track D owns `ui/browser*`, `web/**`, `image/**`,
`http.c`; Track N keeps X.509/cert checks, `clock.c` and TLS 1.2 (craigslist).
Done: DOM/CSS/layout engine (flex, grid, floats, absolute/fixed, @layer,
var(), media queries), tabs/omnibox/bookmarks/history/downloads/find/zoom,
cookies, POST, gzip, PNG/GIF/JPEG/WebP/SVG. Not done: JavaScript (info bar
offers lighter sites). Tools: `tests/web_render.c` renders saved pages on the
host (`WEB_DUMP`, `WEB_REPEAT`, `WEB_BENCH`, `WEB_BASE`), `tests/qemu_web_tour.py`
visits live sites (use `OJJY_ACCEL=hvf` for real timings; TCG is ~8x slower).

## LATEST (2026-10-19): Track N — N1 crypto primitives DONE and on main

`main` = `f35c47a` and holds BOTH tracks' latest (D: D1 glass controls +
mouse-lag fix; N: N1). Track N branch `net-web` == `main`, tree clean.
N1 (`7537656`): SHA-384/512, 4224-bit big integers with CIOS Montgomery,
RSA PKCS#1 v1.5 + RSASSA-PSS, ECDSA P-256/P-384 — written from the
specs, in the kernel build, host-tested (72 checks, OpenSSL-CLI and
Python-hashlib vectors, 4000-corruption fuzz + truncation sweeps, green
under ASan/UBSan; 16/16 suites; both dev builds 0 warnings). Four real
implementation bugs the vectors caught are documented in
`NET_WEB_PLAN.md`'s Log (128-bit SHA-512 length field + UB shift; CIOS
m_i 32-bit mask; PSS leftmost-bits cleared not rejected; EC
Montgomery-domain discipline, add-2007-bl factors, alias-safe double,
P-384 Gy from OpenSSL). **N2 (ASN.1/X.509) is next** — step text, the
exact crypto call signatures it should use, the vector-generation
workflow and gotchas are in `NET_WEB_PLAN.md` §"Current state" +
"N2 starting notes". The debug probes in `ojjyOS-v3-net/build/dbg_*`
can be deleted. Publishing note: if `git fetch -q . net-web:main`
fails, Track D moved main — follow `PARALLEL_PLAN.md` §4.5 (rebase
onto main, rerun tests, fast-forward); NEVER `git branch -f main`.

## LATEST (2026-10-18, night): two AIs in parallel — read `PARALLEL_PLAN.md`

Track D (desktop) in worktree `ojjyOS-v3-ui` / branch `desktop-polish`
(`DESKTOP_PLAN.md`); Track N (HTTPS certificate verification — TLS
currently skips it —, NTP, web images, downloads, Web app) in worktree
`ojjyOS-v3-net` / branch `net-web` (`NET_WEB_PLAN.md`). Strict file
ownership in `PARALLEL_PLAN.md` §3; both fast-forward `main`; the main
folder (Jonas's, branch `ui-polish-3`) is not edited by either.

## LATEST (2026-10-18, evening): security pass — 6 credential/VFS holes fixed

Jonas asked for a detailed improvement list + a routine bug/security
check. `BETTER_OS_PLAN.md` is that list (section 0 = the audit, sections
1-5 = the ordered roadmap). The audit found and FIXED:

- **S1 CRITICAL**: the lock screen "+ New Account" (and Settings
  "+ Add Account" / Terminal `adduser`) minted **admin** accounts with
  no authorization — anyone could walk up and create one. Creation now
  requires an authenticated admin session once any account exists
  (`users_create` returns -4; UIs explain).
- **S2/S3 HIGH**: password salts came from a guessable time/username
  mix and hashes were a single fast SHA-256. Now: 16-hex CSPRNG salts
  and **PBKDF2-HMAC-SHA256 (10k iterations)**; legacy accounts still
  log in and transparently upgrade on next login
  (`[USERS] Upgraded credentials…`). The DB loader previously accepted
  only 64-char hashes — relaxed or PBKDF2 accounts would vanish.
- **S4/S5 MEDIUM**: no per-user file isolation and users.db readable by
  anyone. VFS entry points now deny non-admins other users' homes and
  `/Library/Accounts/` (except the exact write-open password
  self-service uses).
- **S6**: `users_delete` now requires an admin, blocks self-delete and
  last-admin-delete.

Audited clean: ChaCha20 DRBG (key erasure), DHCP/DNS parsing bounds,
HTTP body clamping, HTML engine copies, constant-time compares,
persist validation, EL0 isolation, fuzz suites, host-app control
channel. New/updated host tests cover all six fixes incl. a planted
legacy-hash vector. Receipts: `build/sec-*` (host+ASan/UBSan 15/15,
builds 0 warnings, TCG+HVF+Intel protected smokes and the appearance
cold-login test PASS). Known accepted gap: PBKDF2 at 10k iterations is
below OWASP's modern floor (deliberate for TCG; scale with the
scheduler) and non-admins can still create|truncate-open users.db
(ACLs arrive with the journaled FS) — both documented in the plan.

## LATEST (2026-10-18, later): UI job resumed — C (Appearance) + LG3 (window rims) DONE

Work continues on branch `ui-polish-3` (off integrated `main`), ff'd to
`main` per green step. Settings > Appearance is rebuilt: Light/Dark/Auto
(Auto = dark 19:00–07:00, checked once a minute), 8 accent colours (Blue
default unchanged), Liquid Glass Clear/Tinted, Menu Bar Glass/Solid, Window
Corners Standard/Round, Terminal theme row (stored; S4 wires colours) plus
the moved Reduce/24h switches. All live + persisted (`appearance= accent=
glass= menubar= corners= termtheme=`), every change logs `[UI] Appearance
key=value` and boot logs a summary line. Live test `tests/qemu_appearance.py`
passes incl. cold-boot persistence. LG3: new `gfx_round_rect_rim()` gives
windows the Liquid Glass specular rim over the cached-glass body (hairline
always, rim when transparency is on), title sheen clearer, radius from the
corners setting; `tests/test_gfx.c` covers it (15/15 host suites).
Next per TODO.md: LG4 glass controls, S1 notifications, S2 snapping,
S3 live resize, S4 terminal themes, then per-app polish.

## >>> START HERE (2026-10-18, latest): read `TODO.md` <<<

**Native VM job A is COMPLETE and integrated.** All N1–N11 milestones are
implemented, verified and merged with the UI job's independently committed
work (Liquid Glass, JPEG/wallpaper pack, Inter browser text) on `native-vm`;
local `main` is fast-forwarded to the merge. Launch **`build/ojjyOS.app`**
or `bash scripts/run-vm.sh`; QEMU remains supported for tests/Intel.
`NATIVE_VM_PLAN.md` holds the full receipt log, including the final
full-screen root cause (styleMask flips before the Space animation settles;
AppKit drops toggles issued mid-animation; screenshot churn fired stray
transition callbacks). Job B's unfinished appearance/app features remain
in `TODO.md` / `HANDOFF_UI.md`. Never push; the conversation-export JSON
stays untracked.

## LATEST (2026-10-18): native VM delivered; N1–N11 complete

All native milestones are verified (`e403dd8` is the N9/N10 app commit;
see git log for the final N11 capture/docs commit). Integration is a local
fast-forward to `main`; nothing is pushed. Launch **`build/ojjyOS.app`** or run
`bash scripts/run-vm.sh`. It boots Jonas's custom kernel on Apple's VZ,
not QEMU or Linux. QEMU remains supported (HVF/TCG/Intel fallback).

Installed ARM and Intel images were rebuilt with the standard preserving
builder; both entire data partitions exactly match their pre-build SHA-256
and newest retained backups. Legacy disk also unchanged. Receipt:
`build/vz-resume/installed-data-verified.json`. Installed kernels are
byte-identical to development artifacts used by the regression suites.

`build/vz-resume/delivered-native-alpha/report.json` passes native click
setup, uppercase/Shift+Esc, pixel pointer alignment, Retina/full-screen,
256 editing cycles, all 19 protected-app cases plus cleanup/recovery,
clean shutdown, cold password login and corrupt-checkpoint fallback.
`installed-vz-boot/report.json` verifies boot/device/DHCP/IRQ/clock against
a private copy of the installed image without clearing its saved data.
All test VMs/disks were private; the user's VM was never stopped.

QEMU final TCG/HVF/Intel smoke passes. Network/HTTP, local HTTP+HTTPS
browser, network UI checks pass; apps/file/design/animation tours completed
and representative captures reviewed. Receipts: `build/vz-resume/final-*.log`.
Normal/sanitized host suites and warning-free ARM/Intel builds pass.
Native-host guards: 102 Swift checks and five pixel-gate Python tests.
Final exact-source receipts: `build/vz-resume/n11-{host-final,app-guards,
arm-final,x86-final,native-final}.log`, `n11-native-final/report.json`.
All pass without warnings. The native rerun uses a private copy of the
installed image and includes the hidden-window capture regression and EL0.

Capture hardening: WindowServer returned transparent RGBA surfaces and
show-animation edge strips, not a black guest frame. Capture now brings
only its own window forward and waits (bounded) for opaque edge midpoints;
it never substitutes another image. Pixel gates still reject blank images,
missing/misaligned cursors and geometry changes. Guest motion is unchanged.

Limits: fixed 2880x1800 scanout, not live guest mode resize; directly
attached USB HID only, no hubs/hot-plug enumeration; guest clock is UTC;
control text is US ASCII; manual hardware focus-loss behavior is not
exhaustively tested. No hard frame-time guarantee or bare-metal Mac support.
Shut down inside the guest, then quit the app before rebuilding its disk.
The conversation-export JSON is intentionally untracked. Do not add it.

## Historical checkpoint (N9/N10 verified, before N11; 2026-10-18)

N8 kernel component committed `a73c5a0`. Native app implementation:
`vmapp/main.swift`, `scripts/{build-vmapp,run-vm,test-vmapp}.sh`,
`tests/{test_vmapp.swift,vz_smoke.py}`. `build/ojjyOS.app` builds/signs and
runs VZ at 2880x1800. `native-guards/report.json` proves setup with uppercase
text, 256 editing cycles, sync/shutdown and cold password login, plus actual
live disk/builder lock retention. Own-window CG captures contain Retina GPU
pixels. Full native pointer/click/full-screen/fallback smoke now PASSES
(`build/vz-resume/native-complete/report.json`): setup populated via clicks,
pixel pointer hotspot at (100,100), native full-screen entry/exit, 256 editing
cycles, sync/shutdown, cold login and corrupted-newest-checkpoint fallback.
Full-screen capture reviewed: real blue desktop, sharp text, menu bar and Dock.
102 native-host guard checks pass; both kernel builds warning-free, host and
sanitizer suites pass. Rebuilt kernel hashes match tested artifacts exactly.

Gotchas: synthesized flagsChanged needs physical left/right NX_DEVICE bits
as well as aggregate modifiers; otherwise Shift+Esc and uppercase fail.
Sign the app in non-iCloud temporary staging before ditto installation:
FileProvider restores FinderInfo too quickly for remove-then-sign in build/.
`--control` now ignores physical input in its test view to prevent desktop
mouse tracking from contaminating scripted coordinates. Normal launches
keep physical input. Pixel cursor tests must account for its 2px hotspot.

Next: N9/N10 local commit, renderer/performance regressions,
N11 installed-image data hash preservation,
docs/merge to main only when green. Never push. Installed disks untouched so far.

## LATEST (2026-10-18): N8 USB driver implemented; native app is next

`native-vm`: N5/N6 boot fixes committed as `b54a85a`, N4/N7 timer/clock
health as `eb1fd23`. N8 adds original `xhci.c` / `usb_hid.c`, runtime DMA
rings, polled direct-port enumeration, boot keyboard and descriptor-driven
absolute pointer, shared input translation/repeat policy. VZ enumerates its
Apple keyboard (05ac:8105) and pointer (05ac:8106); QEMU USB full smoke
(setup/editing, 256 fast o/Backspace cycles, protected programs, sync/shutdown,
rebuild/cold login and checkpoint fallback) and USB pointer QA pass.
Receipts: `build/vz-resume/n8-{usb-qemu,usb-pointer,hvf,host,sanitized,arm,x86}.log`
and `n8-vz-checked/report.json`. Both builds warning-free; normal/sanitized
host tests include actual xHCI ring/control construction plus HID decoding.

N8's **native typed/clicked acceptance awaits N9/N10**. There is no native
window app or control socket yet. Implement those from HANDOFF_NATIVE_VM.md
§N9/N10, then visually verify the clock/animations and actual VZ USB input.
No installed disk changed or remote pushed. The controller currently supports
directly attached HID devices only, not hubs or hot-plug enumeration.

Two VZ host-crash traps: set CONFIG.MaxSlotsEn before DCBAAP (VZ caches its
array length on base publication); do not CH-chain the separate Setup/Data/
Status TDs. Probe delegates now report host VM failures and return exit 3.
HID service intervals are capped at 1ms for FS/HS: slow service lost
transitions under QEMU's unchanged 5ms stress and left keys held. FIFO
pressure is the inference; host queue occupancy was not measured.

## LATEST (2026-10-18, resumed): VZ boots with all five PCI devices

Work continues on `native-vm` from `d4f2252`; not paused. Read the newest
log entries in `NATIVE_VM_PLAN.md` before the historical handoff below.
The VZ guest now reaches the main loop with PCI GPU, console, disk, RNG,
and networking (real DHCP lease). USB input and the native app remain next.

Root causes verified this session: VZ reports PARange=1 (36 PA bits), which
must not fall back to 32; PCI reset/ACK/DRIVER must be sequenced; console
DEVICE_READY must carry value 1 (0 signals failure); NET_F_MTU must be
negotiated when VZ offers it; RNG has no device-config capability. The early
GPU must not read QEMU-only MMIO slots on VZ. Console comes up before GPU/GIC
and survives the later device scan. Prior marker-based claims in the handoff,
especially "num_queues is unimplemented", were not reliable observations.
Fresh console register reads report **4 queues**, offered low features
`0x30000000` (no MULTIPORT bit). The working VZ serialPorts device still
requires the control handshake. QEMU advertises MULTIPORT normally; the
transport requests it but never writes unsupported bits. Feature masks are
intersected with device offers after reset/ACK/DRIVER, not before reset.

New checks: `tests/test_virtio_pci.c` exercises the actual PCI transport at
an MMIO boundary; `OJJY_PCI=1 tests/qemu_smoke.py` tests all PCI virtio devices
under QEMU (`highmem=off`, keeping a PL011 log); `tests/vz_boot.py` checks VZ
boot, five attached devices, DHCP and clock on a private disk. Never confuse
this boot check with the still-pending USB/account-persistence VZ smoke.
N4/N7 backend check now measures virtual-timer IRQ delivery and RTC calendar
progression after 5 seconds (`build/vz-resume/timer-final-vz/report.json`:
3889 IRQs, clock delta -0.088s). Visual menu-clock/animation QA awaits N9.

## >>> START HERE (2026-10-18, later): native-vm job PAUSED mid-N5/N6 <<<

Jonas paused the session again. Read **`HANDOFF_NATIVE_VM.md` — the
"⚠️ PROGRESS UPDATE" section at the very top** first: it contains the
exact state (N2/N3 committed; N5/N6 + three bug fixes in a WIP commit),
the measured VZ console facts, a fast QEMU test rig for the console
driver, and the ordered TODO. Branch `native-vm`; do not push.

## Project-wide master-list context

Jonas stopped work and asked for full context so that another AI can "do what
is needed in the project". **`TODO.md` is the master list** of everything still
to do, in order, with a pointer to the detailed brief for each item.
Copy-paste prompts for new sessions: `docs/handoff-prompts/`
(`PROMPT_0_DO_WHAT_IS_NEEDED.md` = generic, `PROMPT_3_UI_CONTINUE.md`,
`PROMPT_1_NATIVE_VM.md`).

Two jobs ran in parallel (check `git worktree list`):

| Job | Folder / branch | Brief | State |
|---|---|---|---|
| A. Native Mac app on Apple Virtualization.framework (replace laggy QEMU) | main folder `ojjyOS-v3`, integration on `native-vm` | `HANDOFF_NATIVE_VM.md`, log `NATIVE_VM_PLAN.md` | Native milestones complete; combined-source regression in progress |
| B. Liquid Glass, Tahoe wallpapers, Settings customisation, every app | worktree `ojjyOS-v3-ui`, branch `ui-polish-2` (fast-forwarded into `main` after each step) | `HANDOFF_UI.md`, plan/log `LIQUID_GLASS_APPS_PLAN.md` | LG1, LG2, W1-W4 done; next is C (Appearance customisation) |

Never touch the other job's folder, branch or uncommitted changes.

### LATEST (2026-10-18): Liquid Glass + official Tahoe wallpapers (job B)

- Liquid Glass material `gfx_liquid_glass()` (`kernel/src/gfx.c`) on the
  Dock, menus, menu bar, Control Center, Spotlight, login card etc.
- Baseline JPEG decoder `kernel/src/image/jpeg.c`; desktop picture pack
  reader `kernel/src/image/wallpack.c`.
- 17 official Tahoe wallpapers via `tools/import_wallpapers.py --fetch` into
  git-ignored `wallpapers/local/` (Apple's images: NEVER commit/push them).
  `scripts/mkimg.sh` packs them into the data partition tail at
  **LBA 102400**; `tools/finalize_image.py` now preserves only LBA
  67584..102399 (user data) on rebuild and takes the pack from the new build.
- Settings > Wallpaper grid, Preview = picture viewer, choice saved as
  `wallpaper=` in system.conf, Light/Dark switch swaps paired pictures.
- Fixed: boot (`cmd_ui(NULL)`) forced Light mode every boot.
- Tests: `tests/test_jpeg.c`, `tests/test_wallpack.c`,
  `tests/qemu_wallpapers.py`; test fresh-data wipes stop at LBA 102400.
- Installed images `build/aarch64|x86_64/ojjyos3.img` NOT rebuilt yet (dev
  images are in `ojjyOS-v3-ui/build/dev-*`).

### Previous START HERE (native VM handoff, still valid for job A)

The native VM job's step-by-step instructions are in
**`HANDOFF_NATIVE_VM.md`**; work continues on branch **`native-vm`**.

**READ THIS ENTIRE FILE BEFORE TOUCHING ANYTHING.** It is the handoff
document: project constraints, history, current state, verification, and roadmap.

## LATEST (2026-10-18, later): renderer, HVF, HiDPI, Inter, icons, animations — DONE

Jonas: lag when windows open, everything pixelated/ugly, wants animations,
icons, better fonts and UI; also "make the vm work natively". Plan + full
log with receipts: `RENDERER_UI_PLAN.md` (Phases 1-5 done, Phase 6 =
native Virtualization.framework app, not started; Phase 4.3 = HTML page text
in Inter, not started). Key facts for the next session:
- `run-qemu.sh` defaults to **HVF** (GICv3, `-cpu host`) at **2880x1800**
  (2x HiDPI of a 1440x900 layout); `OJJY_ACCEL=tcg` -> 1440x900 emulation.
  HVF needs every MMIO access to be a single LDR/STR (`arch/aarch64/mmio.h`)
  and GICv3 is detected from MIDR (Apple core), not ID_AA64PFR0.
- Rendering: `kernel/src/gfx.c` (AA spans, shadows, gradients, layer blit),
  framebuffer stores physical pixels with a logical API (`fb_ui_scale()`),
  Inter UI text `kernel/src/uifont.{h,c}` (generated `uifont_data.c` from
  `tools/mkuifont.py`), icons `tools/mkicons.py` -> `ui/icons_data.c`,
  procedural wallpaper `ui/wallpaper.c`, AA cursor `tools/mkcursor.py`.
- Animations: layer + ghost system in compositor (`draw_window_animated`,
  `ghost_capture`), menu/Dock easing; Reduce Motion disables.
- Device waits must be wall-clock (virtio-blk/gpu `virtio_now_ms`, Intel ATA
  PIT ticks): HVF and loaded hosts broke spin-count timeouts.
- Tests: `tests/qemu_perf.py`, `qemu_design_tour.py`, `qemu_anim_check.py`
  (+ all older suites) honour `OJJY_ACCEL=hvf` and `OJJY_DISPLAY=...`.
- Dev builds: `OJJY_BUILD_DIR=build/dev-aarch64`; the installed images were
  rebuilt at the end with data partitions hash-identical to backups.

## (2026-10-18): full-screen fix — virtio-gpu display (see §4.7)

ARM runner now uses a kernel virtio-gpu driver at 1440x900 (16:10) with
Zoom To Fit on, so macOS full screen has no black bars. All live suites pass
on virtio-gpu and on the ramfb fallback; x86 unchanged. Nothing pushed.

## (2026-10-17): networking, browser with HTTPS, real Wi-Fi tile, apps pass DONE

Commits 2526e68 (network), 45d40c7 (browser), 40b3732 (TLS 1.3/HTTPS),
b641922 (Wi-Fi tile + Settings > Network), 52620fb (apps pass). Next ideas
in ROADMAP.md: certificate verification, images (PNG), Downloads, TextEdit
word wrap, Preview for real files, Intel NIC. Nothing pushed.

### (earlier in this session) networking + working web browser

Jonas: "keep working on improvements, make a list of new stuff, improve all
apps, focus on a working browser" and "add wifi bluetooth ... write down plans
and updates and context". Live plan/log: `BROWSER_APPS_PLAN.md`; ideas list:
`ROADMAP.md`. Done: virtio-net/rng, from-scratch IPv4/DHCP/DNS/TCP, HTTP
client, Terminal ifconfig/ping/nslookup/curl, HTML engine + browser (real
sites over http://). Keep big buffers out of BSS (see plan log: firmware
memory at 0x44000000 blocks the ARM kernel image). No Wi-Fi/Bluetooth
hardware exists in QEMU; the Wi-Fi tile will control the real virtual NIC.

## Earlier: cursor repair + UI cleanup DONE (2026-10-17); bug/improvement pass

Cursor offset/lag fixed (absolute tablet path, scanout cursor overlay, no
scene repaint on pointer motion) and UI cleaned (transparent text, UTF-8,
blue glass, round traffic lights, working Finder titlebar toolbar, menu
layout, Settings switches). Receipts at the end of `CURSOR_UI_PLAN.md`.
Jonas then asked: keep writing context into the project while working, and
after the needed work, hunt for bugs/improvements -> see `IMPROVEMENTS.md`.

Session records / HEAD at this write (2026-10-17 ~21:15):

- **`JONAS_MESSAGES.md`** — Jonas's 42 messages to the AI, verbatim, in order,
  extracted from the 9.7 MB session export he dropped into the repo root
  (`ojjyos-v3-bugs-macos-features-custom-kernel.json`; deliberately NOT
  committed — too large). His msg 280 is the canonical requirements list;
  his latest instructions supersede older ones.
- HEAD is `d1282d2` (file-copy data loss, Finder shortcut/refresh/history,
  account-name bug pass; details in `IMPROVEMENTS.md`); main is 17 commits
  ahead of origin/main, **nothing pushed**.
- More than one AI session has worked in this repo concurrently today.
  Re-check `git log`/`git status` before editing; every handoff file here is
  a snapshot, not a lock.

### (previous) cursor repair then UI cleanup

Jonas reported absolute-pointer offset and lag, then asked for UI improvements
afterward. This supersedes scheduler work; **no scheduler edits exist**. Read
`CURSOR_UI_PLAN.md` for the measured baseline, plan and safe candidate-build path.
QMP (100,100) produced software cursor (512,582); root cause is absolute tablet
positions converted into deltas and scaled again by compositor mouse speed.
Pointer movement also forces whole-scene redraws. User's live Cocoa VM must not
be stopped or its disk overwritten. Keep updating this context during execution.

## Previous milestone: bounded ARM64 protected apps completed (2026-10-17)

This section supersedes older pause/no-userspace claims below. Jonas resumed
with **"continue"**, then asked to **update context as work proceeds**; keep
doing that in future sessions. Read **HANDOFF.md**, **PROTECTED_APPS.md** and
**PROTECTED_APPS_PLAN.md**, then inspect actual git state before changing code.

- Real static AArch64 ELF programs now run at EL0 with private RX/RW+NX pages,
  a guarded four-page stack, clean registers/thread pointers and a private EL1
  exception stack. Owned kernel/app page tables replace firmware mappings.
- Checked SVC #0 ABI permits PID/UID/uptime/captured write/exit only; NEVER
  route user registers into old `proc.c`'s raw-pointer `syscall_dispatch()`.
- Faults return to the kernel. A 100 ms physical-counter budget stops runaway
  EL0 loops (final spin test: 101 ms); syscall/output/page/file limits apply.
  Full integer trap state is saved; FP/SIMD deliberately traps. All user pages
  are wiped/freed and PID reaped after returning to kernel tables.
- Authenticated `run hello`, `run spin`, etc. work in Terminal and console.
  `programs/` builds separate ELF files into the read-only system image. Intel
  builds/boots normally but explicitly reports protected execution unsupported.
- **Not multitasking:** one bounded synchronous runner; built-in GUI apps and
  desktop remain kernel-hosted. No Intel ring-3, hardened account/file ACLs,
  kernel W^X, bare-metal Mac support, HVF or hard real-time UI guarantee.
- Final builds for both architectures passed with zero warnings/errors.
  **1055 C assertions** (849 previous + 206 user tests) and **9 Python tests**
  pass normally and under compatible Homebrew LLVM ASan/UBSan. User tests also
  run 4000 deterministic malformed-header mutations (not counted separately).
- Final ARM64 live suite passes **19 distinct programs**, fault-class checks,
  timer/syscall limits, equal free-page counts, successful runs after faults,
  12 extra clean runs and Spotlight->Terminal hello. Both architectures pass
  sustained input, sync/shutdown, rebuild preservation, cold-boot authentication
  and corrupted-newest-checkpoint fallback. Only private test VMs/disks touched.
- Final source-image data partitions hash-identically to their retained complete
  pre-rebuild backups; legacy image hash still matches Session 8. Receipt:
  `build/protected-data-preservation.log`.
- Final evidence: `build/protected-build-{arm,intel}.log`,
  `build/protected-{host,sanitized}-tests.log`,
  `build/protected-runtime-arm-final/` and `build/protected-runtime-intel-final-v2/`
  with matching `.log` transcripts. ARM directory contains
  `desktop-after-app-faults.png`. Earlier partial runs remain as debugging history.
- See git log for the local milestone commit. **Nothing pushed.** Next work is
  actual scheduling/concurrent tasks, then user-facing app integration and
  authorization. Fullscreen/HVF remain separate tasks; don't silently expand scope.

### Historical pause (superseded by the resume above)

Jonas's latest request was: **"pause. and write all context to the project.
like a new ai can know whats happening"**. Read **[HANDOFF.md](HANDOFF.md)**
alongside this file before resuming. The reliability pass is committed as
`cd2c46b`. The next protected-app milestone was only being investigated;
**no protected-app implementation or runtime changes were made afterward**.
This pause changes documentation only. Do not start the proposed work until
Jonas asks to resume. Never push without explicit authorization.

## Current reliability pass (Session 8, 2026-10-17)

**This section supersedes historical claims below.** The old Session-7
autorepeat explanation was a hypothesis, not a verified diagnosis. Confirmed
defects and the current implementation:

- Virtio queues now retain their negotiated depth. This machine's QEMU
  offers 64-entry input queues; the previous code negotiated 64 but wrapped
  at the 128-entry storage capacity, reading/recycling stale ring entries.
  `tests/test_virtqueue.c` covers 8/64/128 depths and 16-bit index rollover.
- Removed raw evdev `KEY_*` macros that shadowed internal enum names (Home,
  arrows, Delete and other keys mapped incorrectly). Real evdev and PS/2
  decoders share modifier tracking and delayed editing repeats (450/35 ms).
  Adjacent pointer moves coalesce; transitions have reserved queue capacity;
  overflow is counted without printing from the IRQ/producer path.
- Intel standalone UEFI-to-ELF entry explicitly uses SysV ABI (RDI). The
  Intel kernel is now linked at **64 MiB**, not 1 MiB: a live OVMF memory
  map showed ACPI NVS at 8-9 MiB and boot-services allocations overlapping
  the old growing BSS. Allocation failures print conflicting map entries.
- Homebrew's Intel firmware is split code/vars; code alone is not a valid
  `-bios` size. The runner creates a private combined writable pflash image.
  Intel shutdown's literal AML decoder now handles ZeroOp/OneOp and proper
  package lengths; QEMU's actual `_S5_` package uses ZeroOp for both types.
- `update_animations()` now returns `changed`; missing-return warnings fail
  the kernel build. UI re-entry restores buffering; panic paths draw to
  scanout directly, not a backbuffer that a halted kernel cannot present.
- Builds output `build/<arch>/ojjyos3.img`. `tools/finalize_image.py` locks
  and validates existing GPT images, backs them up, copies the entire data
  partition, and atomically installs a separately assembled candidate.
  First per-arch builds import legacy `build/ojjyos3.img` without altering
  it. Refuse malformed/locked disks. Never reset a user's disk to get a test
  passing. VirtualBox VDIs are retained and running VMs are not force-stopped.
- Persistence v2 alternates two 8 MiB slots at LBA 67584 and 83968. Header
  and payload have CRC-32; flush payload before publishing/flushing header.
  Restore validates into scratch first and falls back to the older valid
  slot. v1 had NO checksums: validate structure and migrate via slot 1.
  Autosaves capture a snapshot then write at most eight payload sectors per
  main-loop turn. Later edits stay dirty. Failed sync is reported; failed
  save cancels shutdown/restart. Snapshot capture/CRC and individual block
  requests remain synchronous, so there is no hard real-time latency bound.
- Current runner is **TCG, one vCPU**, not HVF. ARM64 QEMU virt support is
  not bare-metal Apple Silicon support. Process entries/CPU tick estimates
  are bookkeeping, not scheduling, isolated executable processes, or memory
  protection. Account/file authorization and password hardening remain open.
- Existing filesystem/account/heap/process tests plus input, virtqueue,
  persistence fault injection, ACPI AML, and image-update tests run via
  `bash scripts/test.sh`. ASan/UBSan use `HOST_CC=/opt/homebrew/opt/llvm/bin/clang
  OJJY_SANITIZE=1 bash scripts/test.sh`; Apple clang 14's ASan runtime fails
  even for an empty program on this macOS. Sanitized tests namespace the
  freestanding libc to avoid interposing on sanitizer startup.

### Verification receipts (completed Session 8)

- Both full macOS builds succeeded with zero warnings/errors. Delivered
  images: `build/aarch64/ojjyos3.img`, `build/x86_64/ojjyos3.img`.
- 849 host C assertions (37 FS + 81 RAMFS/accounts/heap/proc + 67 input +
  633 persistence + 10 virtqueue + 21 AML) and 9 Python image tests passed.
  The suite also passed with Homebrew LLVM ASan/UBSan. The Python tests
  verified refusal to replace a disk actually locked by a QEMU process.
- Both delivered images passed three-boot live QEMU flows: UI setup,
  Backspace, 256 o/Backspace cycles, console commands, sync, real shutdown,
  private-disk rebuild preserving accounts, cold-boot password login, and
  recovery/login after deliberately damaging the newest checkpoint.
  Screenshots were captured after `[UI] Desktop presented`, not merely
  after the credential-check log.
- The legacy image's SHA-256 before/after delivery was identical:
  `1594a5d572485d4a4df474a2a5fcfbfa0af7f33f3076289e01d23e04c6e72e24`.
  Full backups were retained under each architecture's `backups/` directory.
- Evidence is in this machine's temporary workspace:
  `/private/var/folders/d1/1ykss2fs7_v0nx_r_8qd0_9c0000gn/T/opencode/ojjyos-reliability-build-3ujat0b1/`
  (`deliver-build-*`, `delivered-tests.log`, `delivered-smoke-*`,
  `vm-delivered-*`, `tests-final-sanitized.log`, `ring-before/after.log`).
- Limitations: no bare-metal/HVF claims; manual Cocoa focus-loss behavior,
  VirtualBox runtime and Linux GNU-EFI builds were not revalidated. Panic
  rendering was corrected by inspection/build, not a forced live fault.
  No hard real-time latency or arbitrary app-crash isolation is promised.
  Fullscreen scaling, stronger account security and protected apps remain
  future work. See the local git log for this pass's commit; nothing pushed.

### Historical session summaries (not current verification receipts)

- **Session 1:** Rescued the broken codebase (fixed 28+ build/boot/IRQ/VFS/UI
  bugs, added RAMFS disk persistence, ACPI/PSCI power off, Desktop icons,
  offline `Web.app`, and `About.app`).
- **Session 2 & 3:** Completed the native Apple Silicon (`aarch64`) port
  (`entry.S`, `gicv2`, `timer_a64`, `pl011`, `pl031`, `virtio-mmio` v2
  `virtio_blk` + `virtio_input`, `psci`, dual-arch Makefiles & scripts,
  `run-qemu.sh`), window resize + green zoom, Mission
  Control cards, Reduced Transparency/Motion toggles, Control Center
  Restart/Shut Down buttons, and full documentation sweep.
- **Session 4:** Implemented the multi-user Account & Salted FIPS 180-4
  SHA-256 Credential Engine (`users.c`/`users.h`), First-Boot Setup
  Assistant, macOS Tahoe Lock/Login Screen (`Ctrl+Cmd+Q`), `Settings →
  Users & Accounts`, interactive Menu Bar dropdowns (`ojjy`, `File`,
  `Edit`, `View`, `Window`), Desktop right-click context menu,
  `Calculator.app`, and `Activity Monitor.app` (11 built-in apps total).
- **Session 5:** Implemented the 16-byte-aligned coalescing Kernel Heap
  Allocator (`kmalloc`/`kcalloc`/`krealloc`/`kfree`/`kheap_validate` in
  `memory.c`) and the Kernel Process Table & Syscall Dispatcher
  (`proc.c`/`proc.h` with PID/PPID/UID tracking, per-user app ownership,
  live CPU tick accounting, protected system daemons PID 1–4,
  `syscall_dispatch` for 13 syscalls, and `ps`/`top`/`kill`/`heap`
  commands in `Terminal.app`, console, and `Activity Monitor.app`).
- **Session 6:** Fixed the constant screen **flashing** (compositor
  rendered straight into the live scanout — now double-buffered +
  dirty-tracked, verified 0.000% idle framebuffer diff), shipped the
  anti-aliased **JetBrains Mono** font, 2x cursor, `show-cursor=on`,
  `-smp 1` + MPIDR core parking, Tahoe ASCII art everywhere
  (commits `e331aa4`, `278370a`, `216b2df`).
- **Session 7 (historical handoff):** documented repeated `o`, incorrect
  deletion, apparent freezes, and fullscreen bars. Its high-confidence
  autorepeat claim was unproven. Session 8 found the negotiated-depth and
  key-name collisions above; do not restore the proposed ignore-all-repeats
  workaround or assume VNC stretches correctly without testing it.

---

## 0. START-HERE CHECKLIST (do these in order)

1. **Read this file fully.** Never push to a remote; commit locally as
   "Jonas Lee" (`git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit`).
2. Check git state and preserve user work. Use the current build scripts,
   never an old destructive image builder. Shut down a VM before updating
   its image. Do not delete backups or saved disks automatically.
2b. **Found a bug you do not fix in the same session?** Add ONE line to
   `bugs/OPEN.md` (format: `bugs/README.md`, roadmap card QA-30) and
   mention it in your LATEST block — never leave it only in chat.
2c. **Cutting a release?** Follow `docs/release.md` (card QA-40) —
   freeze, full matrix, reviews, version sync, delivery, tag.
3. Run `bash scripts/test.sh` and both full builds. For live testing, use
   `tests/qemu_smoke.py` with a new output directory; it always copies the
   source disk. `--fresh-data` resets only that new private test copy.
4. Keep scope kernel-first. Next milestones: protected tasks/address
   spaces, actual scheduling/syscall ABI, account/file authorization,
   measured rendering/input latency. Fullscreen/HVF work remains separate.
5. Record fresh verification, limitations and commit locally; never push
   without Jonas explicitly asking.

**Completed work this codebase already has (all verified):**
- `aarch64`: 28/28 objects compile clean; `x86_64`: 30/30 clean;
  host unit tests 118/118 (`test_fs` 37 + `test_ramfs` 81).
- Full aarch64 port: `main.c` ifdefs, dual-arch Makefiles/bootloader,
  `linker-aarch64.ld` @ `0x41000000`, virtio transport (fixed register
  offsets + `VIRTIO_F_VERSION_1`), GICv2/GICv3 auto-detect, full IRQ
  register save in the vector table, polled input via 1 kHz timer.
- Desktop polish: window resize + green zoom, Mission Control cards,
  Reduced Transparency/Motion, Control Center Restart/Shut Down,
  menu-bar dropdowns, right-click context menu, desktop icons, Web app.
- Accounts (salted SHA-256, first-boot setup, lock screen), Calculator,
  Activity Monitor (11 apps), kernel heap + process table + syscalls.
- Session 6: double-buffered compositor + dirty-tracking (idle
  framebuffer diff **0.000%** — this is the number to re-verify after
  any rendering change), 2x cursor, `-smp 1` + MPIDR core parking,
  anti-aliased JetBrains Mono font (`tools/mkfont.py` regenerates
  `kernel/src/font.c` from `fonts/JetBrainsMono-Regular.ttf`), Tahoe
  ASCII art across splash/serial/console/login/About/Terminal/power-off.
- Persistence: RAMFS snapshots autosaved to the data partition every
  30 s when dirty + on shutdown/reboot/`sync`; restored on boot.
- Commits: `0dbaea6` → `17ba39b` → `26cec5c` → `e331aa4` → `278370a`
  → `216b2df` (all local, nothing pushed — Jonas pushes himself).

---

## 1. What this project is

**ojjyOS v3** — a hobby operating system built entirely from scratch by
**Jonas Lee** (the repo owner; GitHub `jo-jo-home`, online handle "ojjy").
The AI assistant's job is to improve it. Non-negotiable rules from Jonas:

- **Keep the custom kernel.** No Linux/BSD/Windows/macOS base. Those systems
  may be used as *build tools* only. All kernel work must be original.
- **Credit Jonas Lee everywhere it matters**: README, About screen, boot
  splash, serial banner, system info, power-off screen. Preserve that.
- **Keep the blue Tahoe theme** (macOS-Tahoe-inspired, blue glass). Never
  introduce other accent colors as the theme.
- **macOS-like behaviors implemented natively** — not by depending on macOS
  or pretending to run Mac apps.
- **Autonomous work**: plan thoroughly, execute end-to-end without
  check-ins, then summarize what changed. Do not ask questions mid-task.
- **Never push to a remote until Jonas explicitly says so.** Local commits
  are fine (and expected).
- **Focus on the kernel first** — "it sets up everything else we build
  upon."
- Have fun: Jonas asked for "lots of cool ascii art and welcome texts" in
  the boot — that exists now; keep it fun but not garish.
- Jonas's priority order (his words): *"reliable boot → input → persistent
  storage → protected apps → dependable desktop → visual polish."*

### Hardware reality (critical for understanding the port)

Jonas runs the OS on an **M2 Pro Mac** in a VM:

- **VirtualBox on Apple Silicon can only run ARM64 guests.** It cannot run
  x86_64 guests at all. The x86_64 build targets *other* machines
  (Debian + VirtualBox / x86 QEMU) — Jonas cannot run it locally.
- Therefore the **aarch64 port is the path that actually runs on his Mac**,
  via `qemu-system-aarch64` with **TCG emulation** in the current runner.
  Jonas said: *"ik its built for intel so i dont like it i want it to be
  native."* That is why the port exists and why it is the primary target.
- macOS builds both complete architectures using clang, Homebrew lld,
  standalone UEFI definitions and the Python image builder. No Linux VM or
  gnu-efi is required. `brew install qemu nasm lld`; Homebrew LLVM is also
  installed and supplies a compatible sanitizer runtime.

---

## 2. Repository layout

```
ojjyOS-v3/
├── AGENTS.md               THIS FILE (handoff; update at session end)
├── PLAN.md                 improvement plan (bug-fix plan + feature plan)
├── README.md, RELEASE.md, ARCHITECTURE.md, DESKTOP.md, APPS.md,
│   FILESYSTEM.md, VISUALS.md, BUGFIXES.md, LICENSE   (up-to-date docs)
├── boot/
│   ├── ojjy-boot.c         UEFI bootloader (ELF64 loader; both arches)
│   └── Makefile            builds BOOTX64.EFI / BOOTAA64.EFI
├── kernel/
│   ├── Makefile            ARCH=x86_64 (default) | aarch64 (+ `objs` target)
│   ├── linker.ld           x86_64 script (base 0x4000000)
│   ├── linker-aarch64.ld   aarch64 script (base 0x41000000)
│   └── src/
│       ├── main.c          entry, console shell, splash art, power seq
│       ├── users.c/h       multi-user accounts, salted SHA-256 credentials, home dirs
│       ├── proc.c/h        kernel process table (PID/UID/ticks) & syscall dispatcher
│       ├── memory.c/h      PMM bitmap page allocator + 512 KB coalescing kheap (kmalloc/kfree)
│       ├── system.c/h      power requests (shutdown/restart/sync flags)
│       ├── arch_power.h    arch_power_off()/arch_reboot() interface
│       ├── types.h         types + per-arch asm helpers (both arches)
│       ├── arch/aarch64/   Apple Silicon / ARM64 port (all device/platform code, §4.1)
│       ├── gdt/idt/paging/timer(PIT)/serial(16550)/entry.asm  x86-only
│       ├── serial_core.c   shared serial_puts/puthex/printf
│       ├── framebuffer.c font.c console.c string.c panic.c  (neutral)
│       ├── drivers/        driver.c model, input.c, ps2_*, ata, block_cache,
│       │                   acpi (x86 S5), blockdev.h + blockdev_x86.c,
│       │                   diagnostics.c
│       ├── fs/             vfs, ojfs (read-only), ramfs (writable),
│       │                   bundle, persist (disk checkpoints)
│       └── ui/             compositor.c (whole desktop + 11 apps + login/lock),
│                           services.c, theme.c
├── scripts/                build.sh mkfs.sh mkimg.sh run.sh run-qemu.sh
├── tools/                  mkojfs mkicon mkwallpaper mkarray (host tools)
├── sysroot/                files packed into the OJFS image (11 .app bundles + Web pages)
│   └── System/Web/*.page   the offline Web app's pages (home, about, accounts, release, shortcuts)
├── tests/                  host-side unit tests (test_fs.c, test_ramfs.c — 118 assertions total)
└── build/                  outputs (gitignored)
```

---

## 3. History — what happened in Sessions 1–5

### Session 1 (the rescue, commit `0dbaea6`)

A five-way audit found the original project **could not build or boot at all**.
Fixed 28+ defects across build, boot, input, filesystem, and desktop:

- **Build/boot:** Fixed space-indented `kernel/Makefile`, replaced broken `xxd -i`
  with deterministic `tools/mkarray.c`, preserved `BootInfo *` in `rbx` across BSS
  clear in `entry.asm`, rewrote `LoadKernel()` in `boot/ojjy-boot.c` to parse
  **ELF64 `PT_LOAD` program headers**, reserved up to `_kernel_end` in PMM, and
  implemented on-demand `paging_map()` for framebuffers above 4 GB.
- **Input:** Wired `driver_dispatch_irq()` into `isr_handler()`, unmasked PIC IRQ 2
  cascade for slave IRQs (`>= 8`), added `0xE0` extended scancodes (giving the OS
  a Super/Cmd key), fixed 9-bit signed mouse delta decoding, and wired scroll wheel
  events to `compositor_handle_scroll()`.
- **Filesystem:** Fixed OJFS root index lookup (`parent == root_idx`), made VFS mounts
  carry per-instance `fs_data` so `/Users` and `/Library` are independent RAMFS trees,
  replaced the leaking RAMFS bump allocator with a first-fit free list + node reuse,
  fixed `strncpy` `n+1` write overflow, and implemented real `.`/`..` path normalization.
- **Persistence & Power:** Added `fs/persist.c` (snapshots both RAMFS instances to
  the GPT `ojjyOS Data` partition at sector `67584` with CRC-32 verification,
  restored at boot, autosaved every 30 s when dirty and on `sync`/shutdown/reboot)
  and `drivers/acpi.c` (ACPI S5 power-off and 8042 reset).
- **Desktop:** Decoupled window opening from `bundle_launch()`, implemented window
  close/minimize traffic lights, single-source-of-truth cursor hit-testing, fixed
  Finder/Terminal/TextEdit/Calendar bugs, and added Desktop icons, `Web.app`, and
  `About.app`.

### Sessions 2 & 3 (Native Apple Silicon `aarch64` Port & Tahoe Polish, commit `935f27f`)

- Completed `kernel/src/arch/aarch64/` (`entry.S`, `arch64.c`, `gicv2.c`, `timer_a64.c`,
  `pl011.c`, `pl031.c`, `virtio.c`, `virtio_input.c`, `virtio_blk.c`, `psci.c`),
  `kernel/linker-aarch64.ld` (`0x41000000`), dual-arch `kernel/Makefile` and
  `boot/Makefile`, `scripts/build.sh`, `scripts/mkimg.sh`, and `scripts/run-qemu.sh`
  (Apple Silicon `-accel hvf` + `ramfb` + `virtio-*`).
- Fixed two `virtio` descriptor chain bugs: `d->next` index advancement in
  `virtio_desc_add()` and setting `VRING_DESC_F_NEXT` on write descriptors in
  `virtio_blk.c`.
- Added window bottom-right resize grip + green traffic-light maximize/zoom,
  live Mission Control window cards (`Cmd+M`), `Reduced Transparency` (`rt=`) and
  `Reduced Motion` (`rm=`) toggles in Settings, Control Center Restart/Shut Down
  buttons, and PS/2 modifier key release events for `Cmd+Tab`.

### Session 4 (User Accounts, Salted SHA-256 Credentials & Tahoe Desktop Expansion, commit `cfbd5de`)

- **Multi-User Account & Credential Engine (`kernel/src/users.h`, `kernel/src/users.c`):**
  - Full FIPS 180-4 SHA-256 implementation with per-account 8-hex-char salt
    (`SHA-256(salt_hex + ":" + password)`) and constant-time digest verification.
  - Persistent account database in `/Library/Accounts/users.db` (`username|full_name|salt_hex|hash_hex|home_dir|admin`)
    automatically synced to disk via `system_request_sync()`.
  - Automatic per-user home directory provisioning under `/Users/<username>`
    (`Desktop/Welcome.txt`, `Documents/Notes.txt`, `Calendar.txt`).
- **First-Boot Setup Assistant & macOS Tahoe Lock/Login Screen (`compositor.c`):**
  - When `users_count() == 0` (or clicking `+ New Account`), displays the Setup
    Assistant card (**Full Name**, **Username**, **Password**, **Confirm Password**).
  - When locked (`!users_is_authenticated()`, or pressing `Ctrl+Cmd+Q`), displays
    the macOS Tahoe Lock/Login Screen with large clock, user selector pills,
    masked password input, `+ New Account`, `Restart`, and `Shut Down`.
- **Desktop & App Additions:**
  - `Settings → Users & Accounts` (`SETTINGS_PAGE_USERS`) with active user card,
    interactive password change form, account list, `+ Add Account`, `Lock Screen`,
    and `Switch User`.
  - Interactive Menu Bar dropdowns (`ojjy`, `File`, `Edit`, `View`, `Window`) +
    clickable active username pill on the top-right menu bar.
  - Desktop right-click context menu (`New Folder on Desktop`, `New Text Note`,
    `Open Terminal`, `Open Finder`, `Change Wallpaper...`, `Account Settings...`,
    `About ojjyOS`).
  - Added `Calculator.app` (`APP_CALCULATOR`) and `Activity Monitor.app`
    (`APP_MONITOR`), bringing total built-in apps to 11.

### Session 5 (Kernel Heap Allocator & Process/Syscall Subsystem)

- **Kernel Heap Allocator (`kernel/src/memory.h`, `kernel/src/memory.c`):**
  - Added a 512 KB 16-byte-aligned coalescing free-list kernel heap (`kheap_init`,
    `kmalloc`, `kcalloc`, `krealloc`, `kfree`, `kheap_get_total`, `kheap_get_used`,
    `kheap_get_free`, `kheap_get_alloc_count`, `kheap_validate`).
  - Every block header carries magic `0x4F4A4850` (`"OJHP"`), detects double-frees
    and out-of-bounds frees, splits oversized blocks on allocation, and coalesces
    adjacent free blocks both forward and backward on `kfree`.
- **Kernel Process Table & Syscall Subsystem (`kernel/src/proc.h`, `kernel/src/proc.c`):**
  - Tracks up to 24 processes (`PID`, `PPID`, `UID`, `owner`, `app_type`,
    `ProcState`, `cpu_ticks`, `memory_bytes`, `syscalls_invoked`).
  - Spawns 4 protected system daemons at boot (`PID 1 kernel_task`,
    `PID 2 vfs_persistd`, `PID 3 inputd`, `PID 4 WindowServer`) and automatically
    spawns/terminates per-user application processes as windows open and close.
  - Provides `syscall_dispatch(num, a0, a1, a2, a3)` implementing 13 kernel
    syscalls (`SYS_GETPID`, `SYS_GETUID`, `SYS_UPTIME`, `SYS_OPEN`, `SYS_READ`,
    `SYS_WRITE`, `SYS_CLOSE`, `SYS_MKDIR`, `SYS_UNLINK`, `SYS_MALLOC`,
    `SYS_FREE`, `SYS_KILL`, `SYS_SYNC`).
  - Integrated into `Activity Monitor.app` (live process table + KHeap & syscall
    telemetry), `Terminal.app` (`ps`, `top`, `kill <pid>`, `heap`/`mem`), and
    the fallback kernel console.

---

## 4. Architecture & Subsystem Reference

### 4.1 `aarch64` / QEMU `virt` Platform Layer (`kernel/src/arch/aarch64/`)

| File | Responsibility |
|---|---|
| `arch64.h` / `arch64.c` | Platform init (`a64_init`), IRQ/exception dispatch (`a64_irq_handler`, `a64_exception_handler`), input polling (`a64_poll_input`) |
| `entry.S` | EL3/EL2 → EL1 drop, FP/NEON enable (`CPACR_EL1`), 128 KB stack setup, BSS clear (`BootInfo *` preserved in `x19`), `VBAR_EL1` vector table |
| `gicv2.c/h` | GICv2 at `GICD 0x08000000` / `GICC 0x08010000`; masks SPIs, sets priorities, ack/eoi/enable/disable |
| `timer_a64.c` | ARM Generic Physical Timer → 1 kHz tick (`PPI 30`); `timer_init`/`timer_get_ticks`/`timer_sleep` |
| `pl011.c` | `serial_init`/`serial_putc` on PL011 UART `0x09000000` |
| `pl031.c` | PL031 RTC at `0x09010000`; Unix epoch → calendar `RtcTime` conversion |
| `virtio.c/h` | `virtio-mmio` v2 transport scanning 32 slots at `0x0A000000 + 0x200*i` |
| `virtio_input.c` | Virtio keyboard + tablet (`EV_KEY`, `EV_ABS` → screen bounds, `REL_WHEEL`) |
| `virtio_blk.c` | Virtio block device implementing `block_available`/`block_read_sectors`/`block_write_sectors` |
| `psci.c` | `arch_power_off`/`arch_reboot` via PSCI `hvc #0` (fallback `smc #0`) |

### 4.2 Future Milestones (Roadmap for Next Sessions)

1. **M4.1 — Concurrent Scheduling & Intel Ring 3 (bounded ARM64 EL0 exists):**
   - Extend `ProcEntry` in `kernel/src/proc.h` with saved CPU context (`CpuContext`:
     `x19..x30, sp, elr_el1, spsr_el1` on `aarch64`; `r12..r15, rbx, rbp, rsp, rip, rflags`
     on `x86_64`) and per-process kernel stacks allocated from `kmalloc`/`pmm_alloc_page`.
   - Extend the separate checked `user.c` ABI and ARM64 trap path to concurrent
     tasks; implement a checked Intel entry path later. **Do not expose the raw
     kernel `syscall_dispatch()` to userspace.**
   - Extend `user_image.c`'s bounded ELF loader to a deliberate executable/app
     policy and lifetime-managed per-task tables, rather than bypassing validation.
2. **M4.2 — Networking (`virtio-net` + ARP/IPv4/ICMP/UDP/TCP/HTTP):**
   - Add `virtio_net.c` on `aarch64` (`virtio-mmio` device ID 1) and a minimal
     socket/HTTP client so `Web.app` can fetch live HTTP pages when QEMU user-mode
     networking (`-nic user`) is attached.
3. **M4.3 — Audio (`virtio-sound` / HDA) & Anti-Aliased Vector Fonts.**

---

## 4.6. Input investigation: corrected Session-7 handoff

**Symptom (from Jonas, live on his M2 Pro):** "if I press a letter
sometimes it just types a bunch of that letter"; after pressing `o`,
"a bunch of os showed", pressing Delete "just spawns more os", and
then the desktop froze completely. It also froze once before, right
around typing in the first-boot account-setup screen.

The old handoff incorrectly presented host autorepeat as a proven cause and
recommended ignoring all repeats. That proposal is withdrawn. Main-loop input
is batched before drawing; `mark_dirty()` does not force a frame per event.

The confirmed transport defect was a **64/128 negotiated ring-size mismatch**.
Reading `events[desc_id]` was correct, but `desc_id` itself could come from the
wrong used-ring slot after wrapping. The shared transport and input driver now
use `vq->size` for all masks/limits. Device indices are reloaded with volatile
access and barriers; each input harvest has a bounded budget.

A second confirmed defect was the raw evdev `KEY_*` macros overwriting internal
`KeyCode` enumerators: Home mapped to 102 instead of 73, Up to 103 instead of
74, Delete to 111 instead of 85 (`KEY_MAX` was 91). Those macros are removed.

Repeats are classified and rate-limited centrally, not dropped wholesale.
Editing repeats work after 450 ms, at most once per 35 ms; Enter and held
shortcuts do not repeat. Modifier state derives from separate physical left/
right keys. PS/2 Pause bytes are swallowed rather than fabricating modifiers.

Verification: `tests/test_input.c`, `tests/test_virtqueue.c`, and the QMP smoke
flow. A temporary fault variant restoring the old 128-entry ring masks fails
the ring-wrap regression; the corrected source passes. Live tests create an
account via the UI, erase `oooo` with Backspace, cross the ring repeatedly with
256 `o`/Backspace cycles, then execute commands successfully. QMP duplicate-down
commands alone are not proof of the exact hardware event stream: the emulator
may filter them. Host decoder tests exercise repeats directly.

Original manual Cocoa focus-loss behavior is not exhaustively reproduced;
do not claim every possible cause of apparent freezing has been eliminated.

---

## 4.7. Display scaling — FIXED 2026-10-18 (virtio-gpu)

Root cause measured: Homebrew edk2's ramfb GOP lists only 640x480, 800x600
and 1024x768 (all 4:3), so no firmware path could fill a 16:10 screen.
`kernel/src/arch/aarch64/virtio_gpu.c` now drives `virtio-gpu-device` (2D:
one B8G8R8X8 resource in PMM memory, TRANSFER_TO_HOST_2D + RESOURCE_FLUSH via
`fb_set_flush_hook`; fb_present flushes the frame, the cursor flushes its
rectangle, direct console drawing is flushed every 50 ms from the main loop,
panic paths call `fb_flush_all()`). It is probed right after `kheap_init()`
and takes the host's preferred size (QEMU xres/yres; Retina sizes are halved
to stay <= 1920x1200). The ARM bootloader now boots without a linear GOP
(virtio-gpu's firmware GOP is BltOnly). `run-qemu.sh` defaults to 1440x900 +
cocoa `zoom-to-fit=on`; `OJJY_RES`, `OJJY_FULLSCREEN=1`, `OJJY_DISPLAY=ramfb`.
Live tests use the same device (`OJJY_DISPLAY` env in `tests/qemu_smoke.py`).
Not done: live resize when the host window changes (virtio-gpu display
events are ignored; Zoom To Fit scales instead).

### Historical notes (before the fix)

Jonas: "Zoom to Fit + fullscreen — screen is bigger but not filling
the whole screen."

The tested ARM firmware currently boots ramfb at 1024x768 (4:3); aspect-
preserving scaling on a widescreen host leaves bars. Intel's tested GOP mode
is 1920x1080. Previous firmware/CLI experiments are version-specific, not proof
that no other resolution path exists. QEMU on this machine is now 11.1.2.

The Session-7 claim that macOS Screen Sharing would stretch away all bars was
not verified. No `--vnc` feature was added in this reliability pass. Investigate
and test firmware modes, renderer buffer limits, host scaling and HVF separately
before promising a fullscreen fix. Keep the current working runner as fallback.

---

## 5. `aarch64` / QEMU `virt` Cheat Sheet

| Thing | Address / value |
|---|---|
| RAM base | `0x40000000` |
| Kernel load/link | `0x41000000` |
| GICv2 distributor | `0x08000000` |
| GICv2 CPU interface | `0x08010000` |
| Timer PPI | `intid 30` (non-secure EL1 physical timer) |
| PL011 UART | `0x09000000` |
| PL031 RTC | `0x09010000` |
| virtio-mmio slots | `0x0A000000 + i*0x200` (32 slots, version 2 = modern) |
| virtio device IDs | `blk=2`, `input=18` |
| PSCI conduit | `hvc #0` (`SYSTEM_OFF 0x84000008`, `SYSTEM_RESET 0x84000009`) |
| Exception levels | UEFI may leave us at EL2 → `entry.S` drops to EL1 |
| ELF machine | `x86_64=62`, `aarch64=183` |

---

## 6. Build & Verify Commands (this Mac)

```bash
cd "~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3"

# Complete builds, including UEFI and data-preserving images (VMs shut down).
ARCH=aarch64 bash scripts/build.sh
ARCH=x86_64 bash scripts/build.sh
bash scripts/test.sh

# Optional instrumented host suite (compatible Homebrew LLVM runtime).
HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 bash scripts/test.sh

# Live checks use a private disk copy; --fresh-data resets ONLY that new copy.
# Choose a NEW output directory each run; repeat with --arch x86_64 as needed.
python3 -B tests/qemu_smoke.py --arch aarch64 \
  --image build/aarch64/ojjyos3.img --output build/smoke-arm --fresh-data

# Stage only reviewed changes; commit locally as Jonas, never push unasked.
git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit -m "..."
```

### 6.1 QEMU forensics toolkit (how the flashing bug was cracked)

Use the `VM` helper in `tests/qemu_smoke.py` rather than fixed shared sockets,
logs, framebuffer addresses, or the user's writable disk. It handles QMP
greetings, request IDs and asynchronous events, uses a short private AF_UNIX
socket, and stops only its own child process. Preserve evidence in its output
directory. It never force-stops another VM.

`vm.screenshot(path)` uses QMP `screendump` to PNG. `vm.qmp("pmemsave", ...)`
can dump physical memory, but rediscover addresses from that boot's serial log
and honor framebuffer pitch. A stable idle framebuffer does not prove working
input. For static checks, the old flashing regression showed 35-100% pixel
differences at idle; after double buffering it was 0.000%. Do not hardcode
`0xbc7a0000` or assume the dimensions remain 1024x768.

Qcodes: `o`, `shift`, `esc`, `backspace`, `delete`, `ret`, `tab`, `spc`.
The key macOS labels Delete is Backspace, not forward Delete. Absolute tablet
motion uses QMP `abs` events (axis x/y), not relative mouse coordinates.

Health receipts: expected devices attach, `Entering main loop`, real typing
and editing work, saved login survives cold boot. `[UI] Desktop presented`
is emitted after the login-to-desktop frame is actually copied to scanout;
waiting merely for a users.c login log can capture the old login screen.
Queue loss is counted (`diag`), not logged once per event. A successful boot
alone is not evidence that input, persistence, or rendering is correct.

---

## 7. Gotchas Learned the Hard Way (do not regress)

1. **Makefile recipes need TABS.** Double-check any Makefile you edit.
2. **Never push to a remote without Jonas's explicit go-ahead.**
3. **BSS-clear vs stack**: on both arches, anything saved on the stack
   before the BSS wipe is destroyed — keep the `BootInfo` pointer in a
   callee-saved register (x86 `rbx`, ARM `x19`).
4. **`VfsOps` carry `fs_data` as the first parameter of every op.** Mounts
   are per-instance; do not reintroduce globals (`current_instance`).
5. **`strncpy` must write exactly `n` bytes** (tested in `tests/test_fs.c`).
6. **OJFS root entry**: top-level entries have `parent == root entry index`
   (`0` in the writer), NOT `0xFFFFFFFF`. `find_entry()` resolves the root
   index first.
7. **x86 IRQ wiring**: `isr_handler()` → `driver_dispatch_irq(irq)` for
   vectors `32..47` first, legacy IDT handler fallback; `pic_enable_irq`
   for IRQ ≥ 8 must also unmask the IRQ2 cascade.
8. **Big structs never go on the kernel stack** (`AppWindowState` ~110 KB;
   window reordering uses static `state_scratch`; `Bundle` arrays in
   `app_registry_init`/`cmd_apps` are static).
9. **Compositor cursor is the single source of truth** — `main.c` passes
   `compositor_get_cursor()` coords into `compositor_handle_mouse` and
   `compositor_handle_right_click`.
10. **printf set**: `console_printf`/`serial_printf` support
    `%s %d %u %x %p %c %%` with `-`/`0`/width/`l`/`ll`. Cast explicitly
    when passing 64-bit values as `%d`/`%x`.
11. **Persistence checkpoint = raw RAMFS instance bytes.** Changing
    `RAMFS_MAX_NODES`/`RAMFS_DATA_SIZE` invalidates old checkpoints via
    the size check (intentional).
12. **Data partition start sector `67584` is a contract** between
    `scripts/mkimg.sh` and `kernel/src/fs/persist.h`. Change both together.
13. **`aarch64` has no PIT/PS2/ATA/CMOS** — generic timer / `virtio-input` /
    `virtio-blk` / `PL031` instead. Common code must use the neutral APIs
    (`timer_get_ticks`, `input_post_*`, `block_*`, `rtc_read_time`).
14. **The compositor's `ui` command** is the desktop entry; `Shift+Esc`
    returns from desktop UI to the fallback console so plain `Esc` only
    closes menus/overlays/Setup Assistant inside the desktop.
15. **`virtio-blk` single-chain-in-flight model**: `d->next`
    must link to `idx + 1` (not `free_head`), and write sector requests
    must set `VRING_DESC_F_NEXT` alongside `VRING_DESC_F_WRITE`.
    Input instead keeps multiple independent event descriptors in flight;
    its ring masks must use the negotiated queue size, not `VQ_SIZE`.

---

## 8. Version & Attribution

- Version: **3.1.0** ("Make It Work" release) — `OJJYOS_VERSION` in
  `kernel/src/main.c`. Keep in sync: boot art (`main.c` splash), About
  window (`compositor.c` `draw_about_window`), `sysroot/System/version.txt`
  (`mkfs.sh`), serial banner.
- Author: **Jonas Lee** in every user-visible surface (README, About,
  splash, serial banner, power screen, `sysroot/System/version.txt`).
  AI contributions are improvements to HIS project — never add AI
  co-author lines to user-facing strings, and commit as
  `user.name="Jonas Lee"`.
