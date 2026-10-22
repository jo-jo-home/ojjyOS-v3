# ojjyOS Master Roadmap (2026 → 2031)

Created by Jonas Lee. Written 2026-10-22 at Jonas's request:

> "create a detailed road map for this os - we are making it a full on modern
> os like macos or windows but its gonna take a while ... we will do
> everything step by step; from every single driver one by one and etc every
> app one by one ... it should be able to be used for giving the ai to do one
> part of the roadmap to finish ... This will be a multiyear project, and i
> want to work on it bit by bit for 5 years ... we are going for the macos
> tahoe liquid glass look, and also i want to combine good elements from
> macos, windows, linux, arch all into one os ... it should be able to run
> windows apps ... a browser thats the same as chromeos but called ojjyWeb."

This folder is the **single source of truth for long-term work**. Every unit
of work is a **card** with a stable ID (for example `SCH-03`). One AI session
takes one card (or one numbered step of a large card), finishes it to the
card's "Done when" list, records evidence, marks it done and commits locally.

---

## 1. Reading order for a new AI session

1. `../AGENTS.md` — project rules, history, build/test commands. Mandatory.
2. `00-how-to-work.md` — the session protocol: picking a card, branches,
   tests, evidence, Definition of Done, commits, status updates.
3. `02-current-state.md` — what exists today, file by file.
4. `03-target-architecture.md` — where the system is going and the key
   decisions (D-001 … ). Cards assume these decisions.
5. The area file that owns your card (table below), **whole card + every
   card it depends on**.
6. `STATUS.md` — generated table of every card and its status;
   `python3 roadmap/tools/roadmap.py next` lists cards whose dependencies
   are all done (= ready to start).

Jonas can hand a card to an AI with the prompt in
`templates/session-prompt.md` (fill in the card ID).

## 2. Files

| File | Contents | ID prefixes |
|---|---|---|
| `00-how-to-work.md` | Session protocol, Definition of Done, evidence, quality bar | — |
| `01-vision.md` | Vision, pillars, the macOS/Windows/Linux/Arch/ChromeOS blend, non-goals, releases | — |
| `02-current-state.md` | Baseline snapshot (2026-10-22) of every subsystem | — |
| `03-target-architecture.md` | End-state architecture, decisions D-001…, migration path | — |
| `04-timeline.md` | Five-year schedule by quarter, milestones, critical path | — |
| `10-kernel.md` | Boot, memory, scheduler, threads, IPC, syscalls, processes, loader, time, debugging | `BOOT MEM SCH THR IPC SYS PROC ELF TIM IRQ SMP KDB` |
| `11-drivers.md` | Driver model and every driver, one card each | `DRV` |
| `12-storage.md` | Block layer, partitions, ojjyFS, other filesystems, backups, encryption | `BLK STO FS` |
| `13-networking.md` | Sockets, TCP/IP v4+v6, DNS, TLS, HTTP, Wi-Fi management, VPN, sharing | `NET` |
| `14-graphics.md` | GPU, WindowServer, surfaces, rendering, text and fonts, animation | `GPU WS GFX TXT ANI` |
| `15-design-system.md` | Liquid Glass specification and the ojjyKit toolkit, one card per control | `DS UIK` |
| `16-desktop-shell.md` | Menu bar, Dock, windows, Spaces, Spotlight, Control/Notification Center … | `SHL` |
| `17-apps.md` | Every app, one or more cards each; every Settings pane | `APP SET` |
| `18-userland-platform.md` | libc, shell, coreutils, SDK, packages (ojjypkg), Store backend, ports | `USR PKG SDK` |
| `19-system-services.md` | init/service manager, logging, settings store, updates, installer, recovery … | `SVC` |
| `20-security-privacy.md` | Permissions, sandbox, signing, keychain, encryption, hardening | `SEC` |
| `21-media-audio.md` | Audio server, codecs, video, camera, MIDI | `AUD MED` |
| `22-compat-linux.md` | Linux binary compatibility, Linux environment, Wayland bridge | `LNX` |
| `23-compat-windows.md` | ojjyWin: running Windows apps (PE loader, Win32, x86 translation) | `WIN` |
| `24-hardware-platforms.md` | VZ, QEMU, real PCs, Raspberry Pi, Apple Silicon, laptops, installer media | `HW` |
| `25-accessibility-i18n.md` | VoiceOver-like screen reader, zoom, input methods, languages | `A11Y I18N` |
| `26-quality-tooling.md` | Tests, CI, fuzzing, profiling, crash reports, docs | `QA DOC` |
| `27-ojjyweb.md` | ojjyWeb (ChromeOS-style browser) — outline, refined later | `WEB` |
| `90-glossary-references.md` | Glossary and the specifications every area uses | — |
| `STATUS.md` | Generated status table (do not edit by hand) | — |
| `templates/` | Card template, session prompt, decision record | — |
| `tools/roadmap.py` | Validate cards, generate `STATUS.md`, list ready cards | — |

## 3. Card format (every card follows it — the tool checks it)

```markdown
### SCH-03 — Preemptive round-robin scheduler
- Phase: P1 · Size: L · Status: todo
- Depends: SCH-02, TIM-02, IRQ-02
- Files: kernel/src/sched/sched.c (new), kernel/src/arch/aarch64/timer_a64.c

**Context.** What exists now and why this card matters.
**Goal.** The end state in one or two sentences.
**Steps.**
1. Concrete step …
**Done when.**
- Measurable acceptance criterion …
**Tests.** Which host/QEMU/VZ tests prove it (new or existing).
**Pitfalls.** Known traps.
**Refs.** Specifications or project documents.
```

- **Phase** `P0`–`P10` (see `04-timeline.md`). **Size**: `S` = one session,
  `M` = 2–3 sessions, `L` = 4–8 sessions, `XL` = must be split into new
  cards before starting (write the split into the file first).
- **Status**: `todo`, `doing`, `done`, `blocked`. Change it in the card
  itself, then run `python3 roadmap/tools/roadmap.py status` to regenerate
  `STATUS.md`.
- **Depends** lists card IDs that must be `done` first (`—` for none).

## 4. The phases in one table

| Phase | When | Theme | Release |
|---|---|---|---|
| P0 | 2026 Q4 | Finish current tracks, split the giant compositor, roadmap tooling | ojjyOS 3.2 |
| P1 | 2027 Q1–Q2 | Kernel foundations: virtual memory, threads, preemption, syscalls, processes, IPC | ojjyOS 4 "Foundation" (beta) |
| P2 | 2027 Q3 – 2028 Q1 | Userland: libc, shell, init, user-space WindowServer, ojjyKit, apps as processes | ojjyOS 4 "Foundation" |
| P3 | 2028 Q2–Q3 | Storage + drivers: block layer, ojjyFS, USB stack, NVMe/AHCI, audio, input | ojjyOS 5 "Harbor" (beta) |
| P4 | 2028 Q4 – 2029 Q1 | Graphics: GPU paths, real Liquid Glass, font engine, multi-display, accessibility | ojjyOS 5 "Harbor" |
| P5 | 2029 Q2–Q3 | Networking + security: sockets, IPv6, TLS, packages, sandbox, signing, encryption, updates | ojjyOS 6 "Shield" (beta) |
| P6 | 2029 Q4 – 2030 Q1 | App suite: Mail, Photos, Music, Maps, Code, Store; media; ojjyWeb JavaScript | ojjyOS 6 "Shield" |
| P7 | 2030 Q2 | Linux compatibility + Linux environment + ports (the Arch side) | ojjyOS 7 "Bridge" (beta) |
| P8 | 2030 Q3–Q4 | Windows compatibility (ojjyWin), Wayland bridge | ojjyOS 7 "Bridge" |
| P9 | 2031 Q1–Q2 | Real hardware: PCs, laptops, Wi-Fi, Bluetooth, installer, x86-on-ARM | ojjyOS 8 "Horizon" (beta) |
| P10 | 2031 Q3–Q4 | Polish, office apps, self-hosting, stretch goals | ojjyOS 8 "Horizon" |

Phases are guidance, not walls: a card may start as soon as its
dependencies are done. `04-timeline.md` explains the critical paths, the quarter plans and the
"one visible win per month" rule that keeps the project fun.

**Size of the plan:** ~700 cards (run `roadmap.py stats`). The tool's
`critical` command shows the longest remaining dependency chains.

## 5. Ground rules (summary — full text in `00-how-to-work.md` and `AGENTS.md`)

- Custom kernel only; other OSes are build tools. All code is original:
  never copy code from Linux, BSD, Windows, ReactOS, Wine, Haiku, Serenity,
  Chromium or WebKit. Public specifications and documentation are fine.
- Never push to a remote unless Jonas says so. Commit locally as Jonas Lee
  with the project's co-author trailer.
- Both architectures build with 0 warnings; `scripts/test.sh` and the
  sanitizer run pass; live tests for anything visible or device-related.
- Never touch a disk image a running VM uses; user data is preserved on
  every rebuild (hash-checked).
- Keep the Liquid Glass / blue Tahoe look, and credit Jonas Lee in every
  user-visible "about" surface.
