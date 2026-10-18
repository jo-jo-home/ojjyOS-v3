# TODO.md — everything that still needs doing in ojjyOS v3

Written 2026-10-18. If Jonas says "do what is needed in the project", this is
the list. Work top to bottom unless he names something. Each entry points at
the document that holds the detailed instructions. Rules for all work are in
`AGENTS.md` (never push, commit as Jonas Lee, keep the blue Tahoe theme,
credit Jonas, both builds 0 warnings, `scripts/test.sh` + live tests before
committing, never touch a disk image a running VM uses).

Status markers: [ ] not started, [~] started, [x] done.

---

## 0. Before you start (5 minutes)

1. `git worktree list` and `git log --oneline -5 main native-vm ui-polish-2`.
   Two jobs were running in parallel when this was written:
   - **UI job** — worktree `../ojjyOS-v3-ui`, branch `ui-polish-2`
     (merged into `main` after each step). Brief: `HANDOFF_UI.md`.
   - **Native VM job** — main folder `ojjyOS-v3`, branch `native-vm`.
     Brief: `HANDOFF_NATIVE_VM.md`, log: `NATIVE_VM_PLAN.md`.
2. `git status` in BOTH folders. Uncommitted changes belong to whoever was
   working there; never reset, stash or overwrite them. If you continue one
   job, work in that job's folder/branch only.
3. Read the handoff for the job you are continuing, all of it.

---

## 1. Job A — Native Mac app instead of QEMU  ([x])

Jonas: "qemu is laggy and i want it to feel smooth like a native virtualbox
windows aarch64 vm". Replace QEMU on Apple Silicon with our own
Virtualization.framework app (`vmapp/`, `ojjyOS.app`). QEMU stays for tests,
Intel and fallback.

- Detailed steps N1-N11 with addresses, device facts and verification:
  `HANDOFF_NATIVE_VM.md`; progress log: `NATIVE_VM_PLAN.md` (on `native-vm`).
- Native milestones N1–N11 implemented and tested; app commit `0054e86`,
  final capture/protected-smoke commit `9d3e78c`. Launch `build/ojjyOS.app`
  or `bash scripts/run-vm.sh`. See `NATIVE_VM_PLAN.md` for exact receipts.
- All native input/device/clock/display/persistence milestones are done.
  Remaining platform limits: live guest display-mode resize, USB hubs/hot-plug,
  non-US-ASCII control text and exhaustive manual focus-loss coverage.
- Integration preserves job B's browser/glass/wallpaper changes with a normal
  merge on `native-vm`, then advances local `main` only after combined-source
  tests. Sibling worktree/branch is not modified. Existing local picture
  originals/cache are copied read-only into this checkout's git-ignored
  asset directory; no licensed images are committed or downloaded again.
- The native fresh-data test clears only LBA 67584..102399, preserving the
  read-only wallpaper region at LBA 102400. Both installed-image user-data
  regions must match pre-build hashes and retained backups after delivery.

## 2. Active job B — Liquid Glass, wallpapers, customisation, apps  ([~])

Jonas: "i want liquid glass like tahoe and also improve all the apps" +
"add the official tahoe backgrounds and customizability for
themes/ui/background in settings" + yes to notifications, window snapping,
live resize, Terminal themes.

- Brief with exact functions, files and acceptance checks: `HANDOFF_UI.md`.
  Plan + log: `LIQUID_GLASS_APPS_PLAN.md`.
- Done: [x] LG1 Liquid Glass material, [x] LG2 glass on all floating chrome,
  [x] W1 JPEG decoder, [x] W2 wallpaper import + disk pack,
  [x] W3 wallpaper library, [x] W4 Settings grid + Preview viewer,
  [x] C Appearance (Light/Dark/Auto, 8 accents, Clear/Tinted glass,
  Glass/Solid menu bar, Standard/Round corners, Terminal theme row;
  `tests/qemu_appearance.py`),
  [x] LG3 window glass rims (`gfx_round_rect_rim`, clearer title sheen,
  radius from the corners setting).
- Remaining, in order:
  - [ ] **LG4** Liquid Glass rims on windows, radius 16, concentric panels.
  - [ ] **LG4** glass toolbar buttons / segmented controls / switches.
  - [ ] **S1** notification banners (+ posts from apps).
  - [ ] **S2** window snapping (left/right half, top = maximise, preview).
  - [ ] **S3** live resize smoothness.
  - [ ] **S4** Terminal themes + coloured prompt and `ls`.
  - [ ] **A** every app: Finder, Terminal, TextEdit, Notes, Calendar,
    Calculator, Activity Monitor, Settings, Web, About, Preview, Login.
  - [ ] Leftovers: drop the unused `.raw` wallpapers from `mkfs.sh` and make
    Spotlight list desktop pictures; desktop "Change Wallpaper..." opens
    Settings > Wallpaper.
  - [ ] Rebuild Jonas's installed images when no VM uses them; update
    `APPS.md`/`DESKTOP.md`/`README.md`/`AGENTS.md`; summary for Jonas.

Wallpaper licensing (permanent rule): the Tahoe pictures are Apple's. They
stay in git-ignored `wallpapers/local/` and only inside locally built disk
images. Never commit or push them.

---

## 3. Backlog after the two jobs (Jonas's priority order:
"reliable boot -> input -> persistent storage -> protected apps ->
dependable desktop -> visual polish")

Kernel / platform (`AGENTS.md` §0 item 4, §4.2; `PROTECTED_APPS.md`):
- [ ] Real preemptive scheduler: per-task kernel stacks and saved contexts,
      so network, autosave and apps never stall the desktop.
- [ ] Built-in apps as protected EL0 programs over a real syscall ABI
      (files, windows, events). Never expose the old raw `syscall_dispatch()`.
- [ ] Intel ring 3 (protected programs exist only on ARM64 today).
- [ ] Account authorization: admin-only account creation, per-user file
      ownership checks, password re-entry for sensitive Settings; stronger
      password hashing.
- [ ] Bigger heap backed by the page allocator; a `pmm_free_bytes()` so UI
      buffers can be released.
- [ ] Journaled on-disk filesystem to replace RAMFS snapshots (bigger files).
- [ ] virtio-gpu display-change events (live resize of the guest screen).
- [ ] Intel: PCI, e1000/virtio-pci NIC, absolute USB tablet.

Networking & web (`ROADMAP.md`, `BROWSER_APPS_PLAN.md`):
- [ ] HTTPS certificate verification (X.509, RSA/ECDSA, root store).
- [ ] Images in the browser (PNG inflate; progressive JPEG on top of
      `kernel/src/image/jpeg.c`).
- [ ] Downloads to ~/Downloads; NTP clock sync.

Desktop & apps (`ROADMAP.md`):
- [ ] Multiple windows per app; Trash Put Back; Quick Look.
- [ ] Screenshot shortcut; sound (virtio-sound + startup chime); games;
      Paint; Spotlight content search and calculator; accessibility
      (text size, high contrast).

---

## 4. Known limitations (be honest about these in summaries)

- Process table entries and CPU ticks are bookkeeping, not scheduling.
- Only one bounded protected program runs at a time (ARM64).
- Wi-Fi/Bluetooth: no such hardware in the VM; the Wi-Fi tile controls the
  virtual Ethernet NIC; Bluetooth reports "no hardware".
- HTTPS is encrypted but the server identity is not verified.
- The wallpaper setting is system-wide, not per user.
- No hard real-time latency guarantee (synchronous block I/O, snapshot CRC).

## 5. Where things are (quick map)

| Need | Look at |
|---|---|
| Project rules, history, build/test commands | `AGENTS.md` |
| UI job brief | `HANDOFF_UI.md`, `LIQUID_GLASS_APPS_PLAN.md` |
| Native VM job brief | `HANDOFF_NATIVE_VM.md`, `NATIVE_VM_PLAN.md` |
| Renderer, HiDPI, fonts, icons, animations | `RENDERER_UI_PLAN.md` |
| Network stack and browser | `BROWSER_APPS_PLAN.md` |
| Protected apps (EL0) | `PROTECTED_APPS.md`, `PROTECTED_APPS_PLAN.md` |
| Disk layout and persistence | `FILESYSTEM.md`, `ARCHITECTURE.md` (Disk Persistence) |
| Jonas's own words | `JONAS_MESSAGES.md` (older), the handoffs (newer) |
| Ready-made prompts for new AI sessions | `docs/handoff-prompts/` |
