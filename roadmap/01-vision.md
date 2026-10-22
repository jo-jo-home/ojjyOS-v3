# 01 — Vision: what ojjyOS becomes

## 1. One sentence

**ojjyOS is a from-scratch, modern desktop operating system by Jonas Lee
that looks and feels like macOS Tahoe's Liquid Glass, behaves as
predictably as Windows, is as open and tweakable as Arch Linux, browses like
ChromeOS — and can run Linux and Windows apps.**

## 2. Pillars (every card should serve at least one)

1. **Beautiful** — Liquid Glass everywhere, smooth 60 fps motion, crisp
   HiDPI text, consistent spacing; nothing looks "hobby".
2. **Responsive** — input never waits for disk or network; preemptive
   multitasking; the desktop sleeps when idle.
3. **Reliable** — data is never lost (journaled/CoW filesystem, snapshots,
   backups), crashes are contained to one app, updates are atomic with
   rollback.
4. **Secure by default** — protected processes, sandboxed apps, signed
   updates, encrypted disks, verified HTTPS, honest permission prompts.
5. **Open and tweakable** — a real terminal, a package manager, plain-text
   configuration, everything scriptable, keyboard-driven where you want.
6. **Compatible** — the web (ojjyWeb), Linux programs, Windows programs,
   common file formats (images, audio, video, documents, archives).
7. **Original** — Jonas's own kernel and code; specifications are the only
   outside source.

## 3. The blend: what we take from each system

| From | We take | We leave |
|---|---|---|
| **macOS Tahoe** | Liquid Glass materials and motion; menu bar with app menus; Dock; Launchpad; Spotlight; Mission Control; Control Center; Notification Center with widgets; app bundles (`.app` folders, drag to install); Finder with tags, Quick Look (Space); Time Machine-style snapshots; Keychain; unified look across apps; Continuity-style clipboard (later) | Closed ecosystem; hidden configuration |
| **Windows 11** | Snap Layouts (window snapping grid), Alt-Tab with thumbnails, Task Manager detail, Settings app search, Win+V clipboard history, File Explorer tabs, `.exe` compatibility, optional bottom taskbar mode, emoji panel, broad hardware support, backward compatibility promise | Ads, forced accounts, registry as the only config |
| **Linux** | Real POSIX terminal and shell, `/proc`-style introspection, open file formats, sockets API, tiling window option, virtual desktops, multiple users, SSH, everything inspectable | Fragmented toolkits; config spread everywhere |
| **Arch Linux** | `ojjypkg` package manager modelled on pacman (fast, simple, rolling releases, clean package format), community build scripts (OUR — "ojjy User Repository" like the AUR), an excellent wiki (`DOC` cards), the "user is in control" attitude | Manual-only installs: we also ship a graphical Store |
| **ChromeOS** | ojjyWeb: a fast tabbed browser deeply integrated with the shell; web apps install as Dock apps (PWAs); profile sync later; the "it just works, updates itself" feel; Linux environment (Crostini-like) for developers | Browser-only OS |
| **Others** | Haiku's responsiveness ideas, Plan 9's "everything is a file server" for devices, Fuchsia/Mach-style capability IPC | — |

## 4. Product decisions made for this roadmap

Jonas said "idk what u want to do"; these are the chosen directions (details
and reasoning in `03-target-architecture.md`):

- **Kernel**: hybrid — in-kernel drivers, filesystems and network stack (for
  speed), user-space processes for the window server, services and all apps.
  Capability-based message IPC ("ports") between processes.
- **Primary platform**: ARM64 under Apple's Virtualization.framework
  (`ojjyOS.app` on Jonas's M2 Pro). Second: QEMU ARM64/x86_64 for tests.
  Third (P9): real x86_64 PCs with UEFI. Apple Silicon bare metal is a
  stretch goal.
- **Native app ABI**: ojjyOS native ELF programs using `libojjy` (C) and
  `ojjyKit` (UI). A POSIX layer (`libc`) makes porting easy.
- **Linux apps**: a Linux system-call personality (like WSL1) runs unmodified
  Linux ARM64 programs; an optional "Linux environment" installs an Arch
  Linux ARM userland for developers.
- **Windows apps**: **ojjyWin** — our own PE loader plus a from-scratch
  Win32 implementation mapped onto ojjyOS. ARM64 Windows programs run
  natively; x86_64 Windows programs run through an x86-64 → ARM64
  translator (P8/P9). On the x86_64 build, x86_64 Windows programs run
  natively.
- **Packages**: `ojjypkg` (+ Store app) for native packages; app bundles for
  drag-and-drop apps; both signed.
- **Browser**: ojjyWeb, evolved from today's engine — details refined later
  (`27-ojjyweb.md`).
- **Look**: Liquid Glass design system (`15-design-system.md`), blue Tahoe
  accent by default with user-selectable accents (already exists).

## 5. Experience principles (tie-breakers when designing)

1. **Direct manipulation first, keyboard always** — every action reachable
   by mouse and by keyboard shortcut; shortcuts follow macOS (Cmd) with
   Windows-style alternatives where natural (Alt-Tab works too).
2. **No modal surprises** — prompts only for real decisions; undo instead
   of "are you sure" where possible.
3. **Instant feedback** — every click shows a state change within one frame.
4. **Concentric, glassy, calm** — consistent radii (inner = outer −
   padding), translucent layers, restrained colour, motion with springs.
5. **Honest** — the UI never claims something it cannot do (e.g. "identity
   not verified" until certificates are checked).
6. **Data belongs to the user** — export everything, plain formats,
   no lock-in, no telemetry without consent.

## 6. Non-goals (explicitly out of scope unless Jonas changes his mind)

- Running real macOS apps (closed, no public ABI documentation to build on).
- Running real Chrome/Chromium binaries (needs a full Linux userland and GPU
  stack; may become possible late via the Linux environment, `LNX-40`).
- Kernel-level Windows drivers (`.sys`); ojjyWin is user-mode only.
- Anti-cheat games and DRM video services.
- Phones/tablets as a target (touch support exists for laptops/tablets PCs).

## 7. Releases and what each one means to a user

| Release | Phase | What a user notices |
|---|---|---|
| 3.2 | P0 (2026 Q4) | Notifications, window snapping, Terminal themes, polished apps, secure HTTPS |
| 4 "Foundation" | P1–P2 (2028 Q1) | Apps crash independently; nothing freezes the desktop; a real shell with `ls`, `cat`, pipes |
| 5 "Harbor" | P3–P4 (2029 Q1) | Big files, USB drives, sound, buttery GPU-accelerated glass, beautiful text, external displays |
| 6 "Shield" | P5–P6 (2030 Q1) | Safe by default, Mail/Photos/Music/Maps, the Store, ojjyWeb with JavaScript |
| 7 "Bridge" | P7–P8 (2030 Q4) | Install Linux and Windows programs; `ojjypkg install` anything |
| 8 "Horizon" | P9–P10 (2031 Q4) | Installs on a real PC/laptop with Wi-Fi and Bluetooth; builds itself |

## 8. Who it is for

- **Jonas** (owner, developer): wants to learn, build and show off a real OS;
  works in bursts with AI sessions. The roadmap must give him a visible win
  regularly (see `04-timeline.md`, "monthly win").
- **Friends trying it**: run `ojjyOS.app` on a Mac (or a VM / PC later); must
  be impressed in five minutes: setup assistant, glass desktop, browser,
  games, music.
- **Developers** (later): SDK, docs, packages, Linux and Windows apps.
