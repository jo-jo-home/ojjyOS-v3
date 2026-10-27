# Completed-work audit (2026-10-27)

Owner: board `1674cc`, branch `verify-completed-work`. Scope: combined
HW-12, TextEdit/Notes, QA-22/30/40 and TIM-04 changes, without starting
HW-13/14. Tests use private images; installed guest data is not changed.

- [x] Review changed contracts and reproduce clock scheduling/reply defects. — done 2026-10-27 (test: build/review/clock-before.log, clock-validation-before.log)
- [x] Fix confirmed defects with deterministic host regressions. — done 2026-10-27 (test: build/review/clock-final.log, 61 checks; before-fix logs reproduce failures)
- [x] Run normal/sanitized host suites, both zero-warning builds and native guards. — done 2026-10-27 (test: build/review/host-final.log, sanitized-final.log, build-arm-final.log, build-intel-final.log, native-guards.log: 2137 Swift checks and 7 Python pixel gates)
- [x] Run private ARM TCG/HVF/Intel and VZ smoke, launch and app/network tours. — done 2026-10-27 (test: build/review/smoke-{tcg,hvf,intel}.log, native-smoke.log, native-launch/report.json, vz-boot-{2048,3072,4096,6144,8192}/report.json, textedit.log, notes.log, design.log, apps.log, apps-check.log, web.log; network local check and external limitations below)
- [x] Record results/limits, update context, privacy-scan and push approved main. — done 2026-10-27 (test: strict checklist and roadmap check pass; gitleaks 1 commit / no leaks plus zero personal-path/account/email matches; git ls-remote origin refs/heads/main verified code commit 569bb221d40d3855d046d9d78b967ceea96ad1ca after fast-forward push)

## Investigation

Static trace: `clock_ntp_poll` returns immediately in IDLE, so neither
the retry nor six-hour timer can start another request. Successful replies
schedule `ntp_next_ms` from Unix wall time but polls use uptime. The UDP
handler ignores the peer address/port. Tests currently exercise a single
success and check only that start did not send synchronously, not that a
later poll respects its deadline. Add controlled-uptime repeated-exchange
tests before changing implementation.

## Results

Published code commit: `569bb22` on public GitHub `main`, verified remotely.
Final read-only documentation repair audit returned current after requiring
the web-tour DNS failures to be disclosed. No release/deploy was performed.
This completion-record follow-up changes documentation only; code/tests
remain byte-identical to the tested and reviewed product tree.

Confirmed fixes:

* IDLE now wakes at retry/resync deadlines; all deadlines use uptime.
* DHCP renewals preserve in-flight work and backoff.
* Only the selected UDP peer/ports and a matching unexpired request can
  adjust time; failed UDP binding is retried rather than marked successful.
* The next exchange includes the fractional correction already stored,
  preventing repeated subsecond corrections from accumulating/oscillating.
* Both server timestamps must be present, plausible and ordered. Each
  unfolds into the supported era independently, including 1970 boots after
  2036 and exchanges straddling the rollover.
* `date` emits the serial receipt required by its live test. Live date
  year assertions use the host UTC year, not a hardcoded 2026.

Both full dev builds are warning-free. Host suites pass normally and under
ASan/UBSan. Native launch matrix passed 53 lifecycle assertions, all three
screens and the unchanged 3-pixel pointer gate. Five VZ memory probes and
native protected-app/input/persistence smoke pass. QEMU TCG/HVF and Intel
protected smokes pass, including saved login and corrupt-checkpoint recovery.
TextEdit/Notes tests pass including wrap geometry, autosave and cold boot.
Design/apps/apps-check tours passed. Web tour completed with 7 HTTP 200
loads, 6 DNS failures (wikipedia/search/hn/npr/python/mdn), and the known
craigslist handshake failure. The tour's DONE exit is not proof every site
loaded. DNS failure cause is not established; filed BUG-1006 for NET-04,
not silently counted as green. All tour PNGs were profiled in
`build/review/screenshot-profiles.log`; representative light/dark/editor/
date/native desktop screenshots were also visually read. Long Terminal
output visibly spills past the content area: filed BUG-1005 for APP-TERM-01.

The external-NTP attempt produced a clean unreachable/backoff receipt,
not a successful live synchronization. That exploratory run initially
accepted either outcome, but the final test retains the original strict
`--internet` success requirement: no network-success gate is weakened.
The final default local-network gate passed (`network-final.log`), and
the separate strict external run PASSED (`network-internet-strict.log`):
real NTP correction -64 ms from pool.ntp.org, followed by corrected date,
example.com DNS and HTTP 200. The initial unreachable receipt is retained,
not hidden or counted as a successful sync. Web tour still reports known
BUG-1001 (craigslist/TLS 1.2). SNTP remains unauthenticated and RTC precision
is seconds; no on-path protection or subsecond display precision is claimed.

Fresh-context adversarial review found no remaining concrete defect in
the product diff/callers (tests reviewed in summary mode by that reviewer;
parent read and executed them). Outside Codex CLI unavailable on PATH.
No version bump, release tag, PR, installed-image delivery or HW-13/14 work
is part of this audit. Installed disks/settings and other worktrees' edits
remain untouched.

First matrix attempts exposed two setup issues: the builder requires an
absolute OJJY_BUILD_DIR (filed BUG-1004, outside this clock fix), and system
Python lacks Pillow. Corrected the invocation and installed Pillow only
inside `build/review/venv`. Initial logs retained. No test gate weakened.
The committed date command omitted its expected serial receipt; the
network worktree has that same line uncommitted. Implemented independently
here without changing that worktree. Release runbook now tests fresh dev
images, protected apps, native guards and launch settings before delivery.
