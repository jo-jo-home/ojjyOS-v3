# HW-13/HW-14 independent regression verification

2026-10-30. Tested delivered HW-14 implementation `89bf21c` / closure
`8855354`, plus Control `ba37569`. No production implementation changed;
the runner live test now accepts `--memory-mb` from the supported choices
and checks the requested amount rather than hard-coding 3072 MiB.
Evidence is local, ignored output in `build/hw-14-verify/`.

## Completed checks

- [x] Fresh ARM/Intel/app builds without warnings. — done 2026-10-30 (test: build-{aarch64,x86_64,vmapp}.log)
- [x] Normal and fatal ASan/UBSan host suites; native/Python guards. — done 2026-10-30 (test: host.log, sanitize.log, vmapp.log; 2149 Swift guards, 7 Python contract cases, 7 capture guards)
- [x] Control backend/frontend checks, tests and offline/CSP build. — done 2026-10-30 (test: control-{check,test}.log, web-{check,test,build}.log; 43 Deno and 16 Vitest tests)
- [x] Both private Home editors and actual API/native-menu cross-tool round trip. — done 2026-10-30 (test: control-ui.log, cross-tool.log 9 cases, control-native/report.json; light/dark captures reviewed in review.png)
- [x] Private TCG/HVF protected/persistence and Intel persistence regression. — done 2026-10-30 (test: smoke-{tcg,hvf,intel}.log; saved accounts/passwords after rebuild/cold boot, damaged newest checkpoint fallback)
- [x] Actual runner RAM/screen/network and native lifecycle matrix. — done 2026-10-30 (test: runner-{hvf4,intel4,tcg8}/report.json, vz-launch/report.json four cases with 16 checks each; review.png inspected)
- [x] Installed-image private VZ boot, memory UI and peak ownership/recovery. — done 2026-10-30 (test: vz-installed6/report.json, vz-ui8/report.json and memory-review.png inspected, vz-stress8/report.json, intel-stress4/report.json)
- [x] Installed-file preservation and active dashboard acceptance. — done 2026-10-30 (test: preservation.json, installed-artifacts.json, installed-dashboard.json; Home/classic 200, token/foreign-Origin refusals)

## Measured live results

| Backend | Requested RAM | PMM usable | Captured scanout |
| --- | --- | --- | --- |
| ARM QEMU HVF, network enabled | 4096 MiB | 4072 MB | 2880x1800 (1440x900 logical) |
| Intel QEMU TCG, network unsupported | 4096 MiB | 4071 MB | 1280x800 |
| ARM QEMU TCG, network disabled | 8192 MiB | 8168 MB | 1600x900 |

VZ6 boot verifies five PCI virtio devices, two USB HID devices, DHCP and
firmware/timer clock progression. VZ8 About/Monitor show **8 GB**, exactly
8191 MB RAM and 8171 MB managed in light and dark appearances. Used/free/
reserved receipts agree with PMM. VZ8 and Intel4 each simultaneously own
3 GiB, write and verify all 786432 pages, include physical pages above
4 GiB, restore both zone free counts and recover to the desktop. ARM also
runs protected hello afterward; Intel explicitly reports unsupported EL0.

The four native lifecycle cases cover all three screens, network toggles,
fullscreen, show-at-start, stale external-writer rejection/reload, pending-
only Save during a running VM and aligned pointer input. These are feature
checks, not merely boot-to-desktop observations.

## Repeat important live checks

Run from the OS worktree with Homebrew tools on PATH. Use new output
directories; tests refuse to overwrite evidence. `$ARM_IMAGE`, `$INTEL_IMAGE`
are source images: tests make private writable copies automatically.

```sh
python3 -B tests/qemu_launch_smoke.py --image "$ARM_IMAGE" \
  --accel hvf --network --memory-mb 4096 --screen 1440x900 \
  --output build/new-verify-hvf4
python3 -B tests/qemu_launch_smoke.py --arch x86_64 --image "$INTEL_IMAGE" \
  --memory-mb 4096 --screen 1280x800 --output build/new-verify-intel4
python3 -B tests/vz_boot.py --image "$ARM_IMAGE" \
  --memory 6144 --screen 1440x900 --output build/new-verify-vz6
```

Control's cross-tool CLI needs **two positional arguments**: the OS source
directory and its compiled `build/tests-vmapp/launch-settings-probe`.
Use physical/canonical TMPDIR for Control's pre-existing portfolio test.
`vz_boot.py` uses `--memory` and `--screen`, not app/EFI/screenshot options;
it builds and signs its own private probe. The interrupted verification's
argument errors were invocation mistakes, not credited as test passes.

## Preservation and remaining limits

Both full installed architecture images, their saved-data regions, the
legacy image, launch-settings bytes and EFI store match the pre-verification
hashes. Fresh kernels are byte-identical to installed kernels. Fresh UEFI
loaders match after excluding only the PE COFF build timestamp; no executable
bytes differ. No installed rebuild or user-data reset was needed.

The installed dashboard was still running its old server and returned 404
for the new endpoint. Restarted only that process with its existing service
configuration; guarded settings GET now returns 200 and writable. Home and
classic return 200; missing tokens and foreign Origin are denied. Actual
Save flows use private fixtures, not the user's installed settings.

This is broad regression coverage, not proof of every OS feature. CPU remains
one; Intel networking and protected execution remain unsupported. Linux GTK
and bare-metal launches were not tested on this Apple Silicon Mac. Control's
documented pre-existing TMPDIR alias and classic readiness-panel overflow
limits are unchanged. No pushes were made.
