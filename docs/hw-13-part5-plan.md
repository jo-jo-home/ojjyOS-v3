# HW-13 part 5 execution and delivery

Created by Jonas Lee, 2026-10-30. Branch hw-13, base 65a5b4a.
Use only private test disks/bundles until all combined-source gates pass.

- [x] Baseline, clean HW-13 checkout, every worktree inspected, Decision/parts 1–4 and MEM shared subsets checked; take board f5c05f. — done 2026-10-30 (test: build/hw-13-part5/baseline.log; main 043894c already ancestor; unrelated net-web/qa edits left untouched).
- [x] Shared byte-based memory model and capacity formatter, host boundary cases, About exact PMM capacity, Monitor managed/used/free/reserved and bounded bar, remove obsolete native warning and add Swift/source guards. — done 2026-10-30 (test: host-v1.log/sanitized.log, both zero-warning build logs, vmapp.log; evidence in build/hw-13-part5/).
- [x] Private VZ 2/4/8 GiB PMM/UI agreement and reviewed light/dark About/Monitor captures, with source disk unchanged. — done 2026-10-30 (test: build/hw-13-part5/{vz2,vz4-v2,vz8}/report.json, screenshot OCR, all 12 captures visually reviewed via review-sheet.png; 2047/4095/8191 MB RAM, 2027/4075/8171 MB managed).
- [x] Finish combined-source host/Swift/native/QEMU/app-polish/full-clock/high-memory/stress gates; strict checklist and roadmap, local implementation commit. — done 2026-10-30 (test: build/hw-13-part5/{host-final,sanitized,vmapp-final,build-arm,build-intel}.log; TCG/HVF/Intel protected/persistence smokes, native full smoke, 20 app-polish flows, VZ2/4/8 clocks, VZ8 high isolation/TLS, VZ8/HVF4/Intel4 3-GiB stress all pass; checklist-checkpoint.log has no PROBLEM).
- [x] Fast-forward main and Jonas's checkout only after green tests; no running VM; hash saved-data regions, legacy disk, launch settings and EFI stores before/after data-preserving ARM/Intel/app builds. Boot a private delivered-image VZ8 copy and verify About/Monitor again; write ready_to_test. — done 2026-10-30 (test: build/hw-13-part5/preservation.json, delivery-{arm,intel,app}.log zero warnings, delivered-vz8/report.json and reviewed About capture; both installed kernels byte-identical to dev; main/checkout 921c482 and ready note written).
- [x] Mark HW-13 done only with full delivery evidence; cross-check four MEM shared boxes without completing their full cards; update AGENTS/NATIVE_VM_PLAN/TODO/card/STATUS and commit final records, board done, ask before pushing. — done 2026-10-30 (test: build/hw-13-part5/matrix.json/preservation.json, strict checklist and roadmap checks clean; implementation 921c482, delivery 47060cc, UI acceptance b5c2958; final records contain no code change. All HW-13 boxes checked; full MEM cards remain doing; next HW-14/launch-3; push requires a new yes).

GB labels use binary units rounded to the nearest tenth. MB counters are
binary MiB rounded down individually, so row sums may differ by one MB.
The denominator is initial managed capacity, not requested VM RAM or the
highest physical address. Enabled high free contributes to active free;
reservations are not allocations. Physical/device addresses are unchanged.

The initial vz4-v1 diagnostic failed because capture preceded the tab's
redraw. The native test now waits for presentation; vz4-v2 passes without
changing the guest. Keep failed evidence, do not count it as a pass.
