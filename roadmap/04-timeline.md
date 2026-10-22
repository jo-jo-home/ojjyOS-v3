# 04 — Five-year timeline (2026 Q4 → 2031 Q4)

A plan, not a promise. Jonas works "bit by bit"; AI sessions do most of
the implementation. The schedule is built from the card counts and the
dependency graph (`python3 roadmap/tools/roadmap.py stats|critical`) and
is reviewed every quarter (§6).

## 1. Phases by quarter

| Quarter | Phase | Focus | Cards (≈) | Release at end |
|---|---|---|---|---|
| 2026 Q4 | P0 | Finish desktop + net tracks, split `compositor.c`, hygiene, release process | 18 | **3.2** |
| 2027 Q1–Q2 | P1 | Kernel foundations: memory, threads, preemption, syscalls, processes, IPC, Intel ring 3 | 67 | 4 "Foundation" beta |
| 2027 Q3 – 2028 Q1 | P2 | Userland: libc, shell, tools, ojjyd, WindowServer in user space, ojjyKit, every app ported | 187 | **4 "Foundation"** |
| 2028 Q2–Q3 | P3 | Block layer, ojjyFS + migration, USB stack, storage controllers, audio, Spotlight index | 89 | 5 "Harbor" beta |
| 2028 Q4 – 2029 Q1 | P4 | GPU paths, real Liquid Glass, fonts/text engine, multi-display, Mission Control, accessibility | 70 | **5 "Harbor"** |
| 2029 Q2–Q3 | P5 | Sockets/TCP/IPv6, TLS lib, packages (ojjypkg), sandbox, signing, encryption, updates, recovery | 54 | 6 "Shield" beta |
| 2029 Q4 – 2030 Q1 | P6 | App suite (Mail, Photos, Music, Videos, Maps, Code, Store…), media codecs, ojjyWeb JS + APIs | 81 | **6 "Shield"** |
| 2030 Q2 | P7 | Linux compatibility + Linux environment, ports (clang, git, Python), OUR | 31 | 7 "Bridge" beta |
| 2030 Q3–Q4 | P8 | ojjyWin (Windows apps), Wayland bridge, x86 translator design | 31 | **7 "Bridge"** |
| 2031 Q1–Q2 | P9 | Real PCs/laptops, Wi-Fi, Bluetooth, installer, x86-on-ARM JIT, Raspberry Pi | 50 | 8 "Horizon" beta |
| 2031 Q3–Q4 | P10 | Polish, office apps, self-hosting, accessibility audit, stretch goals | 17 | **8 "Horizon"** |

Sizes in session-equivalents (S=1, M=2.5, L=6, XL=12) total roughly
**2,000 AI sessions** — about 8 per week over five years, or fewer with
bigger sessions. That is ambitious; §5 explains how to keep momentum and
what to cut if needed.

## 2. Critical paths (run the tool for the current values)

**Kernel → apps as processes** (`critical WS-18`, ≈ 64 sessions):
MEM-01 → MEM-03 → MEM-04 → MEM-06 → THR-01 → PROC-01 → PROC-04 → FS-01 →
SYS-07 → USR-01 → GFX-01 → UIK-01 → UIK-24 → UIK-21 → UIK-22 → UIK-46 →
APP-PORT-10 → APP-PORT-12 → WS-18. This chain decides when release 4
ships; keep at least one track on it at all times during P1–P2.

**Longest overall** (`critical`, ≈ 100 sessions): the kernel chain above
continued into ojjyWin (WIN-00 → WIN-01 → WIN-02 → WIN-03 → WIN-08 →
WIN-60 → WIN-62). Windows compatibility is long but starts late because
it needs the whole platform underneath.

**Other milestone chains:** `critical LNX-51` (Firefox via Linux, ≈ 60),
`critical HW-03` (real laptop, ≈ 62), `critical WIN-91` (everyday
Windows apps, ≈ 98).

## 3. Quarter-by-quarter detail

### 2026 Q4 — P0: ship 3.2
- Track D: SHL-02, SHL-03, SHL-04, APP-TERM-01, APP-FINDER-01,
  APP-TEXTEDIT-01, APP-NOTES-01, APP-CAL-01, APP-SET-01.
- Track N: NET-02, NET-03, TIM-04.
- Shared: QA-20 (split compositor — when no track is in the file),
  QA-22, QA-30, QA-40, DOC-03, QA-41 (release).
- Monthly wins: notifications (Oct/Nov), snapping + Terminal themes
  (Nov), verified HTTPS lock (Dec), 3.2 release (Dec).

### 2027 Q1–Q2 — P1: the kernel grows up
- Order: KDB-01, KDB-04, BOOT-01, TIM-01, IRQ-01 → MEM-01…MEM-07 →
  THR-01/02/03 → SCH-01…SCH-05 → SYS-01…SYS-06 → PROC-01/02/04/10 →
  ELF-01/02/03 → IPC-01/02/05 → MEM-08/09/11/13.
- In parallel: DS-01/02/03/04/05/12 (design specs), QA-01/02/04/06/08/21/23,
  SEC-01/02, DRV-CORE-01/05, DRV-PCI-01, BOOT-06/07, SDK-01.
- Monthly wins: Activity Monitor shows real memory freeing (MEM-01);
  CPU burner can't freeze the desktop (SCH-02); downloads + autosave no
  longer stutter (SCH-05); `run spin` runs alongside the desktop, Intel
  runs protected programs (PROC-10, ELF-03).

### 2027 Q3 – 2028 Q1 — P2: userland and the big move
- Q3: USR-01/02/04, USR-41/42/44/46, FS-01/02/03/04/08/12, SYS-07/08/09,
  DRV-TTY-01, USR-20/21/10/11/12/13/14, SVC-01/02/20/21, SEC-41.
- Q4: WS-01…WS-06/08/11, GFX-01/02/03, TXT-01/02/03/04/07/11/13,
  UIK-01…UIK-06 and the control cards (UIK-10…UIK-50), DS-06/07/10/11,
  ANI-01/03.
- Q1: APP-00, APP-PORT-01 (Calculator pilot) → APP-PORT-02…12, SHL-01/05/
  07/10/14/15/25/27/28, SVC-03/04/05/10, WS-18, release 4.
- Monthly wins: `ls | grep` in a real shell; first app process
  (Calculator); WindowServer in user space with SIMD glass (much faster
  frames); every app crash-isolated; kernel UI deleted.

### 2028 Q2–Q3 — P3: storage, devices, sound
- BLK-01…05, FS-10/11/13/14/15/17 (migration of Jonas's data!), FS-05/06/
  07/09/20, STO-02/03/04, DRV-VIRTIO-02/05/06/09, DRV-USB-01…05, DRV-STOR-
  01/02/03, DRV-AUD-01, AUD-01/02/03/04, DRV-NET-01, HW-40, SVC-11/12/22/
  30, SHL-08/11/21/24/30, TXT-08/09/10, DS-08, APP-FINDER-02/05/06/07,
  APP-TERM-02, APP-DISK-01, APP-CONSOLE-01, APP-SYSINFO-01, APP-GAMES-02,
  SET-01/02/09/14/16/18/19/26, USR-16/40/43, SDK-03/04/05, SEC-12/13.
- Monthly wins: startup chime + sound; Mac shared folder in Finder;
  USB sticks; big files and snapshots on ojjyFS; content search.

### 2028 Q4 – 2029 Q1 — P4: graphics and polish
- GPU-00…05, WS-07/09/13, GFX-04, ANI-02, UIK-04/07/27/30/31/40/45/49/53,
  DRV-DISP-01/03, DRV-IN-04, SHL-06/09/12/13/18/19/20/29, A11Y-01…09/12,
  I18N-01/03/04/06/07/08, APP-FINDER-03/04/08, APP-TERM-03, APP-TEXTEDIT-02,
  APP-NOTES-02, APP-CALC-02, APP-MON-02, APP-CLOCK-01, APP-FONTS-01,
  APP-HELP-01, APP-GAMES-01, APP-PAINT-01, APP-ARCHIVE-01, SET-05/10/11/
  12/15/25/27/28, SVC-13, DOC-02, WEB-02/10.
- Monthly wins: real refractive Liquid Glass at 60 fps; any font size
  crisp; Mission Control with Spaces; screen reader speaks.

### 2029 Q2–Q3 — P5: secure and connected
- NET-01…NET-08/11/14/15/16/17/80, SEC-03/04/10/11/20/21/22/23/30/31/40/61,
  PKG-01/02/03/04/06/21, SVC-40/41/60/70, BOOT-04, FS-18/21/24, BLK-06,
  DRV-FW-01, DRV-TPM-01, DRV-VIRTIO-03/10/11, APP-PASSWORDS-01,
  SET-03/08/13/20/24, SHL-16, SVC-15, WEB-03/20/28.
- Monthly wins: `ojjypkg install`; FileVault-like encryption; atomic
  updates with rollback; ojjyWeb runs its first JavaScript.

### 2029 Q4 – 2030 Q1 — P6: the app suite
- APP-MAIL-01, APP-PHOTOS-01/02, APP-MUSIC-01, APP-VIDEO-01, APP-MAPS-01,
  APP-WEATHER-01, APP-CONTACTS-01, APP-REMIND-01, APP-CAMERA-01,
  APP-VOICE-01, APP-CODE-01, APP-STORE-01, APP-PREVIEW-02, APP-CAL-02,
  APP-BOOKS-01, APP-NEWS-01, APP-DICT-01, APP-MIGRATE-01, MED-01/02/10/11/
  20/21/25/30/40, AUD-05, NET-12/13/18/20/21/22/30/50/70/75/76, STO-01,
  SEC-42/70, SVC-14/50/71, SHL-17/26/40, PKG-20, FS-16/19/30, I18N-05/
  10/11/12/13, A11Y-10, UIK-41, TXT-05/06, GFX-08, SDK-06, WEB-21/22/23/
  25/27, DOC-05, SET-04/07/21/29/30, DRV-USB-06/07/08/11, DRV-IN-06.
- Monthly wins: Mail with a real account; Photos library; Music plays
  FLAC; ojjyCode edits ojjyOS on ojjyOS; YouTube in ojjyWeb.

### 2030 Q2 — P7: the Linux/Arch side
- LNX-01…08/10/20/40, USR-03/30/31/32/33, PKG-05, FS-22, AUD-06,
  APP-CODE-02, APP-SHORTCUTS-01, MED-22/23/24, GFX-06, DRV-USB-09,
  DRV-STOR-05, SET-31, HW-11, WEB-24/29.
- Monthly wins: Linux binaries in Terminal; `pacman -S` in the Linux
  environment; git and Python.

### 2030 Q3–Q4 — P8: Windows apps (ojjyWin)
- WIN-00…WIN-18/20/80/90, LNX-09/30/31/32/41/42, GPU-41, FS-23, WEB-26.
- Monthly wins: first `.exe` prints hello; Notepad++ opens; GTK apps as
  native windows.

### 2031 Q1–Q2 — P9: real hardware
- HW-01/02/03/20/50, DRV-ACPI-*, DRV-PCI-03, DRV-CORE-03/04, DRV-UMD-01,
  DRV-DISP-04/05, DRV-NET-02/03/04, DRV-BT-01, DRV-IN-03/05,
  DRV-STOR-04, DRV-USB-10, DRV-AUD-02, DRV-GPIO-01, DRV-SENS-01, NET-19/
  31/40/60/61/62/71, SVC-42, SET-17/22/23, SEC-60, MEM-15, TIM-05, GPU-40,
  WS-20, WIN-19/60/63/81/82/84/91, FS-25.
- Monthly wins: boots on a real PC; Wi-Fi; Bluetooth headphones; x64
  Windows app via the translator.

### 2031 Q3–Q4 — P10: Horizon
- APP-OFFICE-01/02/03, SDK-09/40, LNX-50/51, WIN-61/62/83/85, HW-30/41/60,
  A11Y-11, SEC-71, SVC-80, plus accessibility audit, performance work and
  bug burn-down.
- Monthly wins: Firefox via Linux; self-hosted build; office suite; 8.0.

## 4. Suggested parallel tracks

Two or three AI sessions can run at once with strict file ownership
(`PARALLEL_PLAN.md` style). Recommended splits:

| Phase | Track A | Track B | Track C |
|---|---|---|---|
| P1 | Kernel memory/threads/scheduler (critical path) | Syscalls/processes/IPC (after THR-01) | Design specs, QA tooling, drivers framework |
| P2 | libc + shell + tools + services | WindowServer + ojjyDraw + text | ojjyKit controls + app ports |
| P3 | Block layer + ojjyFS | USB + storage + audio drivers | Shell features + app growth |
| P4 | GPU + compositor | Accessibility + i18n | Apps + Settings panes |
| P5 | Networking + TLS | Security + packages + updates | ojjyWeb JS |
| P6 | Media + codecs | Apps suite | ojjyWeb APIs |
| P7–P8 | Linux personality | ojjyWin | Wayland bridge + GPU APIs |
| P9 | PC drivers | Wireless | x86 translator |

## 5. Keeping momentum over five years

- **One visible win per month** (listed above): pick at least one card per
  month that Jonas can see or show friends.
- **Never break the daily driver**: `ojjyOS.app` with Jonas's installed
  disk must work after every delivery; user data is hash-verified.
- **If behind schedule**, cut in this order: P10 stretch cards, WIN-61/62,
  HW-30/41, MED-22/23, APP-OFFICE-*, A11Y-11, then shrink P6 apps to MVPs.
  Never cut security (SEC) or data safety (FS) cards.
- **If ahead**, pull forward cards from later phases whose dependencies
  are done (`roadmap.py next`).

## 6. Quarterly review (30 minutes, Jonas + one AI session)

1. `python3 roadmap/tools/roadmap.py stats` and `status` → progress.
2. Compare done cards with this file's quarter plan; move phases of cards
   that slipped (edit `Phase:` lines), keep `check` at 0 warnings.
3. Add cards for new ideas Jonas has; remove/drop cards he no longer wants
   (mark `done` with "dropped — reason").
4. Update `02-current-state.md` when subsystems changed.
5. Commit "roadmap: Q<n> <year> review".
