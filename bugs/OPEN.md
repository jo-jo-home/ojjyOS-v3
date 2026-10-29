# Open bugs

Format and rules: `bugs/README.md`. Add one line per bug you find but do
not fix; remove the line in the commit that fixes it.

| ID | Symptom | Steps | Area | Severity | Card |
|---|---|---|---|---|---|
| BUG-1001 | craigslist.org and all badssl.com test hosts fail the secure handshake (handshake_failure: they need TLS 1.2; the browser only speaks TLS 1.3) | Web: open https://craigslist.org/ or https://sha256.badssl.com/ | NET | med | NET-03 |
| BUG-1002 | Clicking inside TextEdit/Notes does not move the caret (the editor has no click positioning at all) | TextEdit: click anywhere in the text | APP-TEXTEDIT | low | -- |
| BUG-1003 | Documents longer than 64 lines open read-only and are shown cut down | TextEdit: open a 70-line .txt file | APP-TEXTEDIT | low | -- |
| BUG-1004 | Relative OJJY_BUILD_DIR fails after the build script changes directory; absolute paths work | Run OJJY_BUILD_DIR=build/dev-aarch64 ARCH=aarch64 bash scripts/build.sh | QA | low | QA-22 |
| BUG-1005 | Long Terminal output draws past the right edge of its content area | Terminal: curl a page with a long unbroken line (tests/qemu_net.py /big fixture) | APP-TERM | low | APP-TERM-01 |
| BUG-1006 | First six web-tour destinations reported DNS failure; 2026-10-27 replay was 6/6 HTTP 200 both before and after a separate retry-window fix, with valid DNS packets; original cause still unestablished | Run tests/qemu_web_tour.py under HVF; initial build/review/web.log, replay/capture build/bug-1006/ in review worktree | NET | med | NET-04 |
| BUG-1007 | Activity Monitor lists Finder (PID 5) under user `guest` after the first account logs in, and closing/reopening Finder keeps that owner (the Finder window opens at boot before any account exists) | Fresh disk: create an account, open Activity Monitor > Processes | APP-FINDER | low | APP-FINDER-01 |
| BUG-1008 | tests/vz_boot.py can report an RTC progression failure after a legitimate NTP correction before its 5-second sample; it compares corrected RTC to uncorrected firmware epoch plus uptime | Private VZ 4096/8192 MiB probes, 2026-10-29: complete boot/device/DHCP/maps, NTP offset before health sample, then "RTC calendar did not advance by guest uptime"; build/hw-13-part1/vz-{4096,8192}/serial.log | QA/TIM | low | HW-13 part 4 |
