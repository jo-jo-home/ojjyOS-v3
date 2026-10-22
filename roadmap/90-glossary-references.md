# 90 — Glossary and references

## 1. Glossary (project terms first, then general OS terms)

| Term | Meaning |
|---|---|
| **ojjyOS** | Jonas Lee's operating system (this project). |
| **Card** | One unit of roadmap work with an ID (`SCH-03`). |
| **Track** | A parallel line of AI work in its own worktree/branch (`PARALLEL_PLAN.md`). |
| **VZ** | Apple's Virtualization.framework; `ojjyOS.app` uses it to run ojjyOS on Jonas's Mac. |
| **HVF / TCG** | QEMU with Apple's hypervisor (real speed) / pure emulation (≈8× slower). |
| **Installed images** | `build/aarch64/ojjyos3.img`, `build/x86_64/ojjyos3.img` — Jonas's disks with his data; rebuilt only with data preservation and no VM running. |
| **OJFS** | Read-only system image format (today's `/System`). |
| **RAMFS persistence** | Today's `/Users` and `/Library`: in-RAM trees saved as snapshots at LBA 67584. |
| **ojjyFS** | Planned copy-on-write native filesystem (D-009). |
| **WindowServer** | Planned user-space compositor process (D-010). |
| **ojjyKit** | Planned UI toolkit (D-012). |
| **ojjyDraw / ojjyGPU** | Planned 2D drawing library / GPU API. |
| **libojjy** | Planned native foundation library (USR-46). |
| **ojjyd** | Planned PID 1 service manager (SVC-01). |
| **ojsh** | Planned shell (USR-20). |
| **ojjypkg / OUR / OJBUILD** | Package manager / community repository / package build script (PKG-*). |
| **ojjyWin** | Windows compatibility layer (WIN-*). |
| **Linux environment** | Optional Arch Linux ARM userland running on the Linux personality (LNX-40). |
| **ojjyWeb** | The browser (WEB-*, today "ojjyOS Web"). |
| **Liquid Glass** | The macOS Tahoe-style translucent, refractive material (DS-01). |
| **Concentric rule** | Inner corner radius = outer radius − inset. |
| **Personality** | Which syscall ABI a process uses (native or Linux). |
| **Port (IPC)** | Kernel message queue object reachable through handles (D-005). |
| **Handle** | Per-process reference to a kernel object with rights (SYS-02). |
| **vm_object / VMO** | Kernel memory object that can be mapped/shared (MEM-11). |
| **EL0 / EL1** | ARM user / kernel privilege levels (ring 3 / ring 0 on x86). |
| **ECAM** | PCIe memory-mapped configuration space. |
| **GIC / APIC** | ARM / x86 interrupt controllers. |
| **MSI(-X)** | PCI message-signalled interrupts. |
| **DMA** | Devices reading/writing memory directly. |
| **IOMMU (SMMU / VT-d)** | Hardware that restricts device DMA. |
| **AML** | ACPI Machine Language (firmware bytecode the OS interprets). |
| **FDT / DTB** | Flattened device tree (hardware description on ARM boards). |
| **COW** | Copy-on-write. |
| **TLB shootdown** | Invalidating stale address translations on all CPUs. |
| **W^X** | Memory is never writable and executable at once. |
| **ASLR / KASLR** | Address randomization for user space / kernel. |
| **PAC / BTI** | ARM pointer authentication / branch target identification. |
| **TSO** | Total store ordering (x86 memory model). |
| **PE/COFF** | Windows executable format. |
| **ELF** | Unix/Linux/ojjyOS executable format. |
| **IME** | Input method editor (for CJK and similar input). |
| **CLDR** | Unicode's locale data repository. |
| **Shaping** | Converting text to positioned glyphs (ligatures, kerning, scripts). |

## 2. Project documents

| Document | Use |
|---|---|
| `AGENTS.md` | Rules, history, commands (read first). |
| `TODO.md` | Short-term list; points into this roadmap for the long term. |
| `PARALLEL_PLAN.md` | How two AI tracks share the repo. |
| `DESKTOP_PLAN.md`, `NET_WEB_PLAN.md` | P0 track plans. |
| `BETTER_OS_PLAN.md` | 2026-10-18 audit + improvement list. |
| `BROWSER_CHROME_PLAN.md` | Browser engine log (ojjyWeb starting point). |
| `NATIVE_VM_PLAN.md`, `HANDOFF_NATIVE_VM.md` | VZ app bring-up log and facts. |
| `PROTECTED_APPS.md` | EL0 runner rules (never expose `syscall_dispatch`). |
| `RENDERER_UI_PLAN.md`, `LIQUID_GLASS_APPS_PLAN.md` | Renderer and glass history. |
| `FILESYSTEM.md`, `ARCHITECTURE.md`, `APPS.md`, `DESKTOP.md` | Current docs. |
| `JONAS_MESSAGES.md` | Jonas's own words (requirements history). |

## 3. Specifications by area (public sources)

- **Boot/firmware**: UEFI Specification 2.10; ACPI Specification 6.5;
  Devicetree Specification 0.4; PSCI (Arm DEN0022); SMBIOS 3.x.
- **CPU architectures**: Arm Architecture Reference Manual for A-profile
  (memory model, VMSAv8-64, exceptions, PAC/BTI); Arm GICv3/v4 spec
  (IHI 0069); Arm SMMUv3; Intel 64 and IA-32 SDM (Vols 1–3); AMD64 APM;
  System V ABIs (AArch64 psABI, x86-64 psABI); AAPCS64.
- **Buses/devices**: PCI Local Bus 3.0, PCI Express Base 5.0; Virtio 1.2
  (OASIS); xHCI 1.2; USB 3.2 + class specs (HID 1.11 + Usage Tables,
  Mass Storage BOT/UAS, Audio 1/2, Video 1.5, CDC); NVM Express 2.0;
  AHCI 1.3.1; SD Host Controller; Intel HDA 1.0a; VESA E-EDID;
  TCG TPM 2.0 Library; Bluetooth Core 5.4; IEEE 802.11-2020.
- **Executables/ABI**: ELF-64 + gABI; ELF TLS (Drepper); Microsoft PE/COFF
  specification; Microsoft x64 and ARM64 ABI and exception handling docs;
  POSIX.1-2017; ISO C17; Linux man-pages (sections 2, 5, 7).
- **Filesystems**: FAT32 and exFAT specifications (Microsoft); ext4 disk
  layout (kernel.org wiki); ECMA-119/167 (ISO 9660/UDF); Apple File System
  Reference; TN1150 (HFS+).
- **Networking**: RFC 791/8200 (IPv4/6), 9293 (TCP), 768 (UDP), 826 (ARP),
  2131/8415 (DHCP/v6), 4861/4862 (NDP/SLAAC), 1035 (DNS), 6762/6763
  (mDNS/DNS-SD), 8446/5246 (TLS 1.3/1.2), 5280 (X.509), 9110–9114 (HTTP),
  9000–9002/9114 (QUIC/HTTP3), 6455 (WebSocket), 4251–4254 (SSH), 5905
  (NTP), 8484 (DoH); [MS-SMB2]; WireGuard whitepaper.
- **Crypto**: FIPS 180-4/202; NIST SP 800-38D, 800-90A/B; RFC 7748/8032
  (X25519/Ed25519), 9106 (Argon2), 8439 (ChaCha20-Poly1305), 6238 (TOTP);
  IEEE 1619 (XTS).
- **Graphics/text**: Vulkan 1.3 and SPIR-V (Khronos); OpenType 1.9;
  CFF/CFF2; COLR/CPAL; Unicode 16 (UAX #9, #14, #15, #29, UTS #10, #35
  CLDR); ICC.1; PNG; JPEG (ITU T.81); WebP (RFC 9649); SVG 2; PDF
  (ISO 32000-2).
- **Media**: ISO BMFF (14496-12); Matroska (RFC 9559); MP3 (11172-3);
  AAC (14496-3); Opus (RFC 6716); FLAC (RFC 9639); H.264, H.265, AV1, VP9
  specs; WebVTT; HLS (RFC 8216); DASH (23009-1).
- **Web**: WHATWG HTML, DOM, Fetch, URL; W3C CSS specifications; ECMA-262;
  WebAssembly Core; WebAuthn L3; Web App Manifest.
- **Accessibility/design**: WCAG 2.2; WAI-ARIA 1.2; Apple Human Interface
  Guidelines and Microsoft Fluent guidelines (behaviour concepts only).
- **Wayland/Linux desktop**: Wayland protocol XMLs; freedesktop.org
  specifications (Desktop Entry, XDG portals, D-Bus).
- **Windows APIs**: Microsoft Learn Win32/COM/DirectX references;
  Microsoft Open Specifications ([MS-*]).
