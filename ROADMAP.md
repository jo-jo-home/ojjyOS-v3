# ojjyOS roadmap — ideas list (superseded)

> Superseded on 2026-10-22 by the detailed five-year plan in
> `roadmap/README.md`; every idea below is covered by a card there.
> Kept for history.

Created by Jonas Lee. Ideas proposed during AI-assisted sessions; ordered
roughly by value to the "reliable boot -> input -> storage -> protected apps
-> dependable desktop -> polish" priority. Status markers: [ ] idea,
[~] in progress, [x] done (with the commit/doc that proves it).

## Platform / kernel
- [ ] Real scheduler: preemptive tasks with per-task kernel stacks, so
      network, autosave and apps never stall the desktop.
- [ ] Run built-in apps as protected EL0 programs over a real syscall ABI
      (file handles, windows, events) instead of kernel-hosted code.
- [ ] Account authorization: admin-only account creation, per-user file
      ownership checks, password re-entry for sensitive Settings.
- [ ] Bigger kernel heap backed by page allocator; slab caches.
- [ ] Persistent, journaled filesystem (replace RAMFS snapshots) with
      larger files and folder sizes.
- [ ] HVF acceleration + fullscreen/display-mode work (Retina scaling).
- [ ] Real Bluetooth/Wi-Fi only become possible on bare metal or with
      passthrough; in QEMU the network is virtual Ethernet (shown in the
      Wi-Fi tile honestly).
- [ ] Intel: PCI enumeration, e1000/virtio-pci NIC, absolute USB tablet.

## Networking & web
- [x] (legacy) Network stack + working browser over http:// (BROWSER_APPS_PLAN.md).
- [x] (legacy) HTTPS (TLS 1.3) in the browser (encrypted; identity not yet verified).
- [ ] HTTPS certificate verification (X.509, RSA/ECDSA, root store).
- [ ] PNG/GIF image decoding in the browser (inflate + palette); JPEG
      baseline decoding exists (`kernel/src/image/jpeg.c`), progressive not.
- [ ] Downloads to ~/Downloads with Finder integration.
- [ ] Network time (NTP/SNTP) to correct the clock.
- [ ] Simple chat/mail-like app over a home-made protocol to a host server.

## Desktop & apps
- [x] (legacy) Liquid Glass material on floating chrome; official Tahoe wallpapers
      with a Settings grid and Preview viewer (`LIQUID_GLASS_APPS_PLAN.md`).
- [~] Appearance customisation, window glass rims, notifications, window
      snapping, live resize, Terminal themes, every-app polish
      (`HANDOFF_UI.md`).
- [x] (legacy) Trash (Delete moves, Empty Trash). [ ] Put Back/restore; Finder tabs; Quick Look.
- [ ] Multiple windows per app (two Finder/Terminal windows).
- [ ] Notifications center (persist events, e.g. download done).
- [ ] Screenshot shortcut that saves a .raw/.bmp into ~/Desktop.
- [ ] Music/sound: virtio-sound driver + startup chime (blue-theme UI).
- [ ] Games: Snake / 2048 / Minesweeper in Tahoe style.
- [ ] Text editor: syntax highlighting, find/replace, word wrap.
- [ ] Paint app (pixels saved to Desktop).
- [ ] Spotlight: search file contents and calculator expressions.
- [ ] Accessibility: larger text scale, high-contrast blue theme.
