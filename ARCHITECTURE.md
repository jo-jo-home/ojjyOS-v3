# ojjyOS v3 Architecture Document

**Author:** Jonas Lee
**Created by:** Jonas Lee
**Version:** 3.0.0-M1
**Last Updated:** January 2026

---

## Table of Contents

1. [Vision](#vision)
2. [System Architecture](#system-architecture)
3. [Milestone Roadmap](#milestone-roadmap)
4. [Completed Work: M1 Prerequisites](#completed-work-m1-prerequisites)
5. [Component Details](#component-details)
6. [File Structure](#file-structure)
7. [Build & Run](#build--run)

---

## Vision

ojjyOS v3 is a custom operating system built entirely from scratch with the following goals:

- **Not based on Linux, BSD, Windows, or macOS** - A truly custom kernel and userspace
- **Boots in VirtualBox on x86_64** - Using UEFI with GOP framebuffer
- **macOS Tahoe-inspired UI** - Blue liquid-glass glassmorphism aesthetic
- **Educational & Functional** - Clean, well-documented code that actually works

### Design Philosophy

```
┌─────────────────────────────────────────────────────────────┐
│                    User Applications                         │
├─────────────────────────────────────────────────────────────┤
│                     Desktop Shell                            │
│              (Window Manager + Compositor)                   │
├─────────────────────────────────────────────────────────────┤
│    Graphics Stack    │    System Services    │    IPC       │
├─────────────────────────────────────────────────────────────┤
│                      Kernel                                  │
│  ┌─────────┬─────────┬─────────┬─────────┬─────────┐       │
│  │ Memory  │ Process │  Timer  │   VFS   │  Driver │       │
│  │ Manager │ Sched   │         │         │  Model  │       │
│  └─────────┴─────────┴─────────┴─────────┴─────────┘       │
├─────────────────────────────────────────────────────────────┤
│                    Hardware (x86_64)                         │
│         CPU, Memory, Framebuffer, PS/2, IDE/ATA             │
└─────────────────────────────────────────────────────────────┘
```

---

## System Architecture

### Boot Flow

```
UEFI Firmware
     │
     ▼
UEFI Bootloader (ojjy-boot.efi)
  - Initialize GOP framebuffer
  - Parse memory map
  - Load kernel.bin
  - Jump to kernel entry
     │
     ▼
Kernel Entry (entry.asm)
  - Set up 64-bit stack
  - Call kernel_main()
     │
     ▼
Kernel Main (main.c)
  - Initialize serial debug
  - Initialize framebuffer
  - Set up GDT, IDT, paging
  - Initialize drivers
  - Enter main loop
```

### Memory Layout

```
0x0000_0000_0000_0000 ┌─────────────────────┐
                      │    NULL Guard       │
0x0000_0000_0010_0000 ├─────────────────────┤
                      │    Kernel Code      │
                      │    Kernel Data      │
                      │    Kernel BSS       │
                      ├─────────────────────┤
                      │    Kernel Heap      │
                      ├─────────────────────┤
                      │    Page Tables      │
                      ├─────────────────────┤
                      │    Driver Memory    │
                      ├─────────────────────┤
                      │    Framebuffer      │  (mapped from physical)
                      ├─────────────────────┤
                      │    Free Memory      │
0xFFFF_FFFF_FFFF_FFFF └─────────────────────┘
```

---

## Milestone Roadmap

### M0: Boot to Framebuffer ✅
- UEFI bootloader with GOP
- Kernel entry in long mode
- Framebuffer initialization
- Serial debug output
- Physical memory manager
- Paging with identity mapping
- GDT and IDT setup
- PIC remapping
- Timer (PIT)
- Basic keyboard input
- Console with text rendering
- Panic screen

### M1: Prerequisites (Current) ✅
- Driver model with lifecycle management
- Unified input event queue
- PS/2 keyboard driver (refactored)
- PS/2 mouse driver with scroll wheel
- ATA/IDE disk driver (PIO mode)
- Block cache with LRU
- Real-time clock driver
- In-OS diagnostics screen

### M2: Graphics Foundation (Planned)
- 2D graphics primitives
- Bitmap/image loading
- Alpha blending
- Gradient rendering
- Font rendering improvements
- Double buffering

### M3: Window System (Planned)
- Window manager
- Compositor with transparency
- Event dispatch to windows
- Basic window decorations
- Mouse cursor sprites

### M4: Desktop Shell (Planned)
- Desktop background
- Dock/taskbar
- Application launcher
- System menu
- File manager basics

### M5: Polish & Apps (Planned)
- Terminal emulator
- Text editor
- Settings app
- Tahoe visual polish
- Sound (if time permits)

---

## Completed Work: M1 Prerequisites

### Overview

The M1 milestone implements the Hardware Abstraction Layer (HAL) and driver infrastructure needed for higher-level features. This creates a clean separation between hardware-specific code and the rest of the kernel.

### Components Implemented

#### 1. Driver Model (`src/drivers/driver.h`, `driver.c`)

A unified driver framework for all hardware drivers with:

**Driver Structure:**
```c
typedef struct Driver {
    const char      *name;          // "ps2_keyboard"
    const char      *description;   // "PS/2 Keyboard Driver"
    uint32_t         version;       // DRIVER_VERSION(1, 0, 0)
    DriverType       type;          // DRIVER_TYPE_INPUT
    uint32_t         flags;         // DRIVER_FLAG_CRITICAL
    DriverState      state;         // DRIVER_STATE_READY
    DriverOps       *ops;           // Callbacks
    void            *private_data;  // Driver-specific data

    // Statistics
    uint64_t         irq_total;     // IRQs handled
    uint64_t         read_bytes;    // Bytes read
    uint64_t         write_bytes;   // Bytes written
    uint64_t         error_count;   // Errors encountered
} Driver;
```

**Driver Operations:**
```c
typedef struct {
    bool (*probe)(Driver *drv);     // Detect hardware
    int (*init)(Driver *drv);       // Initialize driver
    int (*shutdown)(Driver *drv);   // Clean shutdown
    bool (*handle_irq)(Driver *drv, uint8_t irq);  // IRQ handler
    ssize_t (*read)(Driver *drv, void *buf, size_t count, uint64_t offset);
    ssize_t (*write)(Driver *drv, const void *buf, size_t count, uint64_t offset);
    int (*ioctl)(Driver *drv, uint32_t cmd, void *arg);
} DriverOps;
```

**Driver Lifecycle:**
```
UNLOADED → REGISTERED → PROBING → INITIALIZING → READY
                                              ↓
                                         SUSPENDED
                                              ↓
                                          ERROR → DISABLED
```

**Features:**
- Registration and discovery by name/type
- IRQ dispatch to registered handlers
- Error containment (auto-disable after 10 errors)
- Statistics tracking for monitoring
- Critical driver protection (cannot disable keyboard/timer)

#### 2. Input Event Subsystem (`src/drivers/input.h`, `input.c`)

A unified input event queue that collects events from all input devices:

**Event Types:**
```c
typedef enum {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY_PRESS,
    INPUT_EVENT_KEY_RELEASE,
    INPUT_EVENT_KEY_REPEAT,
    INPUT_EVENT_MOUSE_MOVE,
    INPUT_EVENT_MOUSE_BUTTON_DOWN,
    INPUT_EVENT_MOUSE_BUTTON_UP,
    INPUT_EVENT_MOUSE_SCROLL,
} InputEventType;
```

**Event Structure:**
```c
typedef struct {
    InputEventType  type;
    uint64_t        timestamp;

    union {
        struct { uint8_t scancode; KeyCode keycode; char ascii; uint8_t modifiers; } key;
        struct { int32_t dx, dy, x, y; } motion;
        struct { MouseButton button; int32_t x, y; uint8_t modifiers; } button;
        struct { int32_t dx, dy, x, y; } scroll;
    };
} InputEvent;
```

**Features:**
- 256-entry ring buffer (lock-free single-producer/single-consumer)
- Timestamp on all events for ordering
- Mouse position tracking with bounds clamping
- Modifier key state tracking (Shift, Ctrl, Alt, Super, CapsLock, NumLock)
- Key state bitmap for "is key pressed" queries
- Helper functions for drivers to post events easily

**API:**
```c
// For drivers (producers)
void input_post_key_event(InputEventType type, uint8_t scancode, KeyCode keycode, char ascii);
void input_post_mouse_move(int32_t dx, int32_t dy);
void input_post_mouse_button(InputEventType type, MouseButton button);
void input_post_mouse_scroll(int32_t dx, int32_t dy);

// For consumers
bool input_has_event(void);
bool input_poll_event(InputEvent *event);   // Non-blocking
bool input_wait_event(InputEvent *event);   // Blocking

// State queries
void input_get_mouse_position(int32_t *x, int32_t *y);
bool input_is_key_down(KeyCode keycode);
bool input_is_mouse_button_down(MouseButton button);
uint8_t input_get_modifiers(void);
```

#### 3. PS/2 Keyboard Driver (`src/drivers/ps2_keyboard.c`)

**Features:**
- Full scancode-to-keycode mapping
- ASCII translation with shift/caps handling
- Modifier key tracking (Shift, Ctrl, Alt, CapsLock, NumLock)
- Integrated with driver model (probe, init, shutdown, handle_irq)
- Marked as CRITICAL (cannot be disabled)

**How It Works:**
1. Registers with driver subsystem on boot
2. During init, registers for IRQ 1 and enables it in PIC
3. On IRQ, reads scancode from port 0x60
4. Translates to keycode and ASCII
5. Posts event to input queue via `input_post_key_event()`

#### 4. PS/2 Mouse Driver (`src/drivers/ps2_mouse.c`)

**Features:**
- Standard 3-button mouse support
- Scroll wheel detection and support (4-byte packets)
- Relative motion with absolute position tracking
- Button press/release events with position
- Packet synchronization recovery

**Scroll Wheel Detection:**
Uses the "magic sequence" to enable scroll wheel mode:
```
Set sample rate to 200
Set sample rate to 100
Set sample rate to 80
Get device ID → If ID is 3 or 4, scroll wheel is enabled
```

**Packet Format:**
```
Standard (3 bytes):     Scroll (4 bytes):
┌──────────────────┐    ┌──────────────────┐
│ Byte 0: Status   │    │ Byte 0: Status   │
│ Byte 1: X delta  │    │ Byte 1: X delta  │
│ Byte 2: Y delta  │    │ Byte 2: Y delta  │
└──────────────────┘    │ Byte 3: Z delta  │
                        └──────────────────┘
```

#### 5. ATA/IDE Disk Driver (`src/drivers/ata.c`)

**Features:**
- PIO mode read/write (no DMA for simplicity)
- LBA28 and LBA48 addressing
- Primary and secondary channel support
- Master and slave drive detection
- IDENTIFY command for device info
- Works with VirtualBox PIIX4 IDE controller

**Channels:**
| Channel   | I/O Base | Control | IRQ |
|-----------|----------|---------|-----|
| Primary   | 0x1F0    | 0x3F6   | 14  |
| Secondary | 0x170    | 0x376   | 15  |

**Device Detection:**
```c
AtaDevice *dev = ata_get_device(0);  // Get first device
if (dev && dev->present) {
    printf("Disk: %s, %d MB\n", dev->model, dev->sectors * 512 / 1024 / 1024);
}
```

#### 6. Block Cache (`src/drivers/block_cache.c`)

**Features:**
- 64-entry LRU cache for disk sectors
- Write-through policy (immediate write to disk)
- Automatic eviction of least-recently-used entries
- Cache statistics (hits, misses, hit rate)

**API:**
```c
int block_cache_read(uint64_t block_num, void *buffer);   // Uses cache
int block_cache_write(uint64_t block_num, const void *buffer);  // Write-through
void block_cache_invalidate(uint64_t block_num);  // Remove from cache
void block_cache_flush(void);  // Write all dirty blocks
```

#### 7. RTC Driver (`src/drivers/rtc.c`)

**Features:**
- Reads date/time from CMOS RTC
- Handles BCD and binary formats
- Handles 12-hour and 24-hour modes
- Century register support

**API:**
```c
typedef struct {
    uint16_t year;    // 2024
    uint8_t  month;   // 1-12
    uint8_t  day;     // 1-31
    uint8_t  hour;    // 0-23
    uint8_t  minute;  // 0-59
    uint8_t  second;  // 0-59
    uint8_t  weekday; // 1=Sunday
} RtcTime;

void rtc_read_time(RtcTime *time);
void rtc_print_time(void);  // Prints to console
```

#### 8. Diagnostics (`src/drivers/diagnostics.c`)

**Features:**
- Full system status display (press 'D' in OS)
- Shows memory, uptime, display resolution
- Lists all drivers with state and statistics
- Shows ATA devices and block cache stats
- Shows input state (mouse position, modifiers)
- Proper mouse cursor with background save/restore

---

## Component Details

### Interrupt Handling

```
Hardware IRQ
     │
     ▼
PIC (8259)
  - Remapped to vectors 32-47
     │
     ▼
IDT Entry
     │
     ▼
ISR Stub (entry.asm)
  - Save registers
  - Call isr_handler()
     │
     ▼
isr_handler (idt.c)
  - Dispatch to driver via driver_dispatch_irq()
     │
     ▼
Driver IRQ Handler
  - Process hardware event
  - Post to input queue (if input device)
     │
     ▼
Send EOI to PIC
```

### Memory Management

**Physical Memory Manager:**
- Bitmap allocator for 4KB pages
- Supports up to 4GB RAM
- Tracks total/free memory
- Reserves kernel and framebuffer regions

**Paging:**
- 4-level page tables (PML4, PDPT, PD, PT)
- Identity mapping for first 4GB
- Framebuffer mapped at physical address

### Console System

- 8x16 bitmap font
- Scrolling support
- Configurable foreground/background colors
- Printf-style formatting
- Serial mirror for debugging

---

## File Structure

```
ojjyos-v3/
├── boot/
│   └── ojjy-boot.c         # UEFI bootloader
├── kernel/
│   ├── Makefile
│   ├── linker.ld
│   └── src/
│       ├── main.c          # Kernel entry point
│       ├── types.h         # Common types
│       ├── boot_info.h     # Boot information structure
│       │
│       ├── serial.c/h      # Serial debug output
│       ├── framebuffer.c/h # Framebuffer drawing
│       ├── console.c/h     # Text console
│       ├── font.c/h        # Bitmap font
│       ├── string.c/h      # String utilities
│       │
│       ├── gdt.c/h         # Global Descriptor Table
│       ├── idt.c/h         # Interrupt Descriptor Table
│       ├── entry.asm       # Assembly entry + ISR stubs
│       │
│       ├── memory.c/h      # Physical memory manager
│       ├── paging.c/h      # Virtual memory / paging
│       │
│       ├── timer.c/h       # PIT timer
│       ├── panic.c/h       # Kernel panic handler
│       │
│       ├── ui/
│       │   ├── compositor.c/h # Tahoe compositor
│       │   ├── theme.c/h      # UI theme tokens
│       │   └── services.c/h   # App registry + search + settings
│       │
│       ├── fs/
│       │   ├── ramfs.c/h        # RAM-backed writable overlay
│       │
│       └── drivers/
│           ├── driver.c/h      # Driver model
│           ├── input.c/h       # Input event queue
│           ├── ps2_keyboard.c/h
│           ├── ps2_mouse.c/h
│           ├── ata.c/h         # IDE disk driver
│           ├── block_cache.c/h
│           ├── rtc.c/h         # Real-time clock
│           └── diagnostics.c/h
├── scripts/
│   ├── build.sh            # Build everything
│   ├── mkimg.sh            # Create disk image
│   └── run.sh              # Run in VirtualBox
├── build/                  # Build output
├── README.md
├── VISUALS.md              # Tahoe UI and compositor spec
├── DESKTOP.md              # Desktop behaviors and shortcuts
├── APPS.md                 # Core app specs
└── ARCHITECTURE.md         # This file
```

---

## Build & Run

### Prerequisites (Debian 13)

```bash
sudo apt install -y build-essential clang lld nasm gnu-efi mtools dosfstools gdisk virtualbox
```

### Build

```bash
chmod +x scripts/*.sh
./scripts/build.sh
```

### Run

```bash
./scripts/run.sh
```

### VirtualBox Settings

| Setting | Value |
|---------|-------|
| System → EFI | Enabled |
| System → RAM | 2048 MB |
| Display → Video Memory | 128 MB |
| Display → Graphics | VMSVGA |
| Storage → Controller | IDE (PIIX4) |

### Debug

```bash
# Watch serial output
tail -f /tmp/ojjyos-serial.log
```

---

## Filesystem Strategy

For layout, bundle format, metadata, and phased filesystem plan, see `FILESYSTEM.md`.

---

## Recent Changes (Filesystem + Build/Run)

This section documents the concrete updates made to support the filesystem MVP and Debian+VirtualBox workflow, and to reinforce project authorship.

### Filesystem Documentation

**What changed**
- Added `FILESYSTEM.md` with a complete Finder-style layout spec, bundle format, metadata plan, and phased filesystem roadmap.
- Added acceptance tests for listing apps, launching bundles, and reading system assets.

**Why**
- Centralizes filesystem strategy and ensures the MVP and Phase 2 plans are explicit and original.
- Provides a clear, repeatable validation path for VM testing.

**How**
- Wrote a dedicated filesystem strategy document and referenced it here for architecture alignment.

### Build Pipeline (Filesystem Image Integration)

**What changed**
- `scripts/build.sh` now runs `scripts/mkfs.sh` before building the kernel to guarantee `kernel/src/fs/system_fs.h` exists.
- `scripts/mkfs.sh` now creates `/Volumes` and a sample resource file for the About app to match the layout spec.

**Why**
- The kernel embeds the OJFS image; missing `system_fs.h` breaks builds.
- The on-disk layout should be complete and consistent with the spec before packing.

**How**
- The build script executes mkfs first; mkfs generates the OJFS image and the embedded header.
- The sysroot directory structure is extended to include `/Volumes` and About.app resources.

### VirtualBox Run Configuration (ATA Compatibility)

**What changed**
- `scripts/run.sh` now uses a PIIX4 IDE controller instead of SATA for VM disk attachment.

**Why**
- The ATA driver expects a legacy IDE controller; SATA prevents disk detection in the VM.

**How**
- The runner creates/ensures an IDE controller and reattaches the disk to it for both new and existing VMs.

### Authorship Emphasis

**What changed**
- UI copy and documentation now use “Created by Jonas Lee”.

**Why**
- Strengthens attribution in user-facing and reference materials.

**How**
- Updated `README.md`, `ARCHITECTURE.md`, and boot/console strings in `kernel/src/main.c`.

---

## Recent Changes (Graphics + Compositor)

This section documents the Tahoe UI foundation added for the M2/M3 visual milestone.

### Visual System + Theme Tokens

**What changed**
- Added `VISUALS.md` to define the Tahoe Light palette from `lighttahoe.png`, dark mode rules, typography, icon style, compositor architecture, and acceptance tests.
- Added centralized theme tokens in `kernel/src/ui/theme.h` and `kernel/src/ui/theme.c`.

**Why**
- The UI is the primary user-facing surface; consistent material tokens are required for blur, opacity, highlights, shadows, and corner radii.
- The wallpaper reference must drive all UI color decisions.

**How**
- Encoded palette colors into a `ThemeTokens` struct and exposed light/dark variants.
- Documented how gradients and wave highlights translate into glass materials.

### Compositor MVP

**What changed**
- Added a basic compositor in `kernel/src/ui/compositor.c` and `kernel/src/ui/compositor.h`.
- Added a Tahoe glass demo window, soft shadows, rounded corners, and blur sampling.
- Added an in-OS `ui` command to launch the compositor demo.
- Wired input routing so mouse events drag the demo window and keyboard `q` exits UI mode.

**Why**
- M2/M3 requires a layered, translucent window system with a blurred wallpaper backdrop.
- A demo window validates glass materials before a full window manager.

**How**
- Compositor draws a wallpaper layer, then windows, then dock and cursor.
- Blur is a multi-sample software blur from the wallpaper buffer (fast approximation).
- Rounded rect clipping and shadow layers provide depth.
- Animation uses a simple fixed-step open transition.
- Main loop switches between console mode and UI mode without losing the shell.

### Wallpaper Generation

**What changed**
- Enhanced `tools/mkwallpaper.c` to generate liquid-wave Tahoe Light and Tahoe Dark variants.

**Why**
- The wallpaper is the base visual material for all glass effects.
- Dark mode requires an original counterpart that feels like a sibling.

**How**
- Added wave bands and highlight contours to the gradient algorithm.
- Dark wallpaper uses deeper indigo base with teal highlights.

---

## Recent Changes (Desktop Experience)

This section documents the desktop shell behaviors layered on top of the compositor.

### Desktop Behavior Spec

**What changed**
- Added `DESKTOP.md` with detailed behavior specifications for the menu bar, dock, spotlight, launchpad, control center, and mission control (Phase 2).
- Defined keyboard shortcuts and focus rules aligned with macOS-like interactions.

**Why**
- Desktop interaction is the primary user-facing surface, and must match Tahoe feel consistently.
- Detailed specs reduce ambiguity for future M3/M4 work.

**How**
- Documented interaction geometry, animation rules, and UI expectations.

### Desktop Services (MVP)

**What changed**
- Implemented app registry, search index, settings, and notifications in `kernel/src/ui/services.c`.

**Why**
- These services provide the data layer for Spotlight, Dock, and Control Center.

**How**
- App registry enumerates `/Applications` bundles and loads icons.
- Search index scans app names and `/System/Wallpapers` for Spotlight.
- Settings stores toggles + sliders (dark mode, Wi-Fi, Bluetooth, volume, brightness).
- Notifications store the latest message (stub).

### Desktop Shell UI

**What changed**
- Extended the compositor to draw a global menu bar, macOS-like dock with magnification and running indicators, Spotlight overlay, Launchpad grid, and Control Center panel.
- Added a Mission Control overlay stub for Phase 2.
- Integrated Super-key shortcuts and overlay focus handling.

**Why**
- UI shells must exist early to validate the glass material system and interaction model.

**How**
- `compositor_handle_key()` handles Super shortcuts and Spotlight input.
- `compositor_handle_mouse()` routes dock clicks, Launchpad selections, and Control Center toggles.
- Menu bar shows active app name and status placeholders.
- Added app switcher overlay (Super+Tab) and menu bar click for Control Center.
- Added overlay animations (fade/slide) for Spotlight, Control Center, Launchpad, and Mission Control.
- Dock magnification now lifts icons as they scale for a stronger macOS-like feel.
- Added WM hook stubs in the compositor for create/move/resize/focus.

---

## Recent Changes (Core Apps)

This section documents the addition of Tahoe-style core apps and their integration with the desktop shell.

### App Specs + UI Consistency

**What changed**
- Added `APPS.md` with UI layouts, data models, and implementation notes for Finder, Settings, Terminal, and TextEdit.

**Why**
- Core apps define the primary user experience; consistent glass materials and behaviors must be documented.

**How**
- Documented layout geometry, data models, and VFS integration per app.

### Finder, Settings, Terminal, TextEdit (MVP)

**What changed**
- Implemented in-compositor app shells for Finder, Settings, Terminal, and TextEdit in `kernel/src/ui/compositor.c`.
- Added new app bundles (Finder, Terminal, TextEdit) in the sysroot build pipeline.
- Added new icons via `tools/mkicon.c` for Finder and TextEdit.
- Added new manifests under `sysroot/Applications/*.app/manifest.json` for the new apps.

**Why**
- App shells validate UI consistency, app launching, and menu bar integration.
- Finder and Settings are required to make the OS usable in M3.

**How**
- Finder uses VFS to list directories and handles bundles as single items.
- Settings toggles dark mode and wallpapers via compositor API and settings service.
- Terminal provides a minimal shell with `ls`, `cat`, and `help`.
- TextEdit loads text via VFS and supports in-memory save/restore.
- Finder opens `.txt` files directly in TextEdit and keeps a last-opened path for Super+O.
- Dock running indicators reflect app launch state from the registry.
- Finder supports list and icon views, with toolbar view toggle and sidebar favorites.
- Settings includes sidebar sections and wallpaper selection buttons.

---

## Recent Changes (Premium App Pass + RAMFS)

This section documents the upgrade to a writable RAM-backed filesystem and premium app behaviors.

### Writable Overlay (RAMFS)

**What changed**
- Added `kernel/src/fs/ramfs.c` and `kernel/src/fs/ramfs.h`.
- Extended VFS with `vfs_mkdir()` and optional `mkdir` ops.
- Mounted RAMFS at `/Users` and `/Library` to enable real writes.

**Why**
- A macOS-like Terminal and TextEdit require actual filesystem writes.
- User data must be writable even with a read-only system image.

**How**
- RAMFS stores nodes and data in fixed in-memory pools.
- Boot initializes `/Users/guest` and seed files in RAMFS.
- Settings preferences now persist to `/Library/Preferences/system.conf` within RAMFS.

### Terminal Upgrade (macOS-like)

**What changed**
- Terminal now has a zsh-style prompt and supports `cd`, `pwd`, `ls`, `cat`, `echo`, `touch`, `mkdir`, `open`, and `clear`.
- Working directory is tracked per Terminal window.

**Why**
- The Terminal must feel real and operate on the filesystem, not placeholders.

**How**
- Added path resolution, prompt rendering, and command parsing.
- Commands call VFS for file operations and app launching.
- Terminal `open` can launch apps or open directories in Finder.

### Finder + Settings + TextEdit Improvements

**What changed**
- Finder now supports back/forward history, live search, and proper icon rendering (app bundles, folders, files).
- Settings writes preferences to `/Library/Preferences/system.conf` and loads them on boot.
- TextEdit saves to RAMFS-backed files; Notes app uses the same editor.

**Why**
- Premium UX requires real functionality and persistence within the session.

**How**
- Finder integrates icon cache from `/System/Library/Icons` and filters VFS listings.
- Settings uses a small preference store saved via VFS.
- TextEdit writes content using VFS create/truncate.
- Dock size/magnification and mouse speed are now user-configurable in Settings.
- Keyboard shortcuts can be toggled in Settings → Keyboard.

### New Apps (Notes + Preview)

**What changed**
- Added Notes and Preview apps with new icons and manifests.
- Preview shows Tahoe Light/Dark thumbnails and updates wallpaper on click.

**Why**
- Broader core app set strengthens the macOS-like experience.

**How**
- Notes is a TextEdit variant bound to `/Users/guest/Documents/Notes.txt`.
- Preview loads thumbnails from wallpaper files and switches live.

### Calendar (Premium)

**What changed**
- Added Calendar app bundle and icon, plus Calendar rendering in the compositor.
- Added a RAMFS-backed calendar file (`/Users/guest/Documents/Calendar.txt`) seeded at boot.
- Added a 12/24-hour time format toggle stored in `/Library/Preferences/system.conf`.

**Why**
- Calendar is a core macOS-like app and anchors the productivity suite.
- Time format needs to be user-configurable and consistent across the UI.

**How**
- Calendar renders a month grid, agenda pane, and view switcher; events are parsed from a simple text format.
- Settings → Appearance toggles 12/24-hour display and updates Calendar rendering.

### Premium App Enhancements

**What changed**
- Finder list view now includes Name/Type/Size columns and keeps live filtering by search.
- Terminal now has command history navigation with Up/Down.
- Terminal now includes rm/mv/cp for RAMFS-backed file operations.
- TextEdit shows line/column and dirty state, and Notes auto-saves on edits.
 - Added line-based clipboard operations (Super+C/V/X) for TextEdit/Notes.
 - Finder now supports rename (R) and delete (Delete) for selected items.
 - Finder now shows a preview pane for selected items.
 - Finder has file clipboard copy/cut/paste for RAMFS.
 - TextEdit supports cursor navigation and line split/merge.
 - Calendar supports selecting, editing, and deleting events from the agenda list.

**Why**
- These behaviors are expected for a premium macOS-like app suite.

**How**
- Finder uses `VfsDirEntry.size` for file sizes and renders column labels.
- Terminal tracks history in a fixed ring and applies previous commands to input.
- RAMFS implements `unlink` and `rename` to support delete/move.
- TextEdit/Notes use `dirty` state and saved status strings for UI feedback.
 - Clipboard is a simple line buffer shared across editor apps.
 - Finder rename uses the toolbar field and `vfs_rename` under the hood.
 - Finder preview reads a few lines of text for selected files and caches by path.
 - Finder file clipboard uses `vfs_rename` or `copy_file_path` when pasting.
 - TextEdit splits lines on Enter and merges on Backspace at line start.
 - Calendar edit mode uses an inline title buffer and persists via `calendar_save()`.
 - Finder drag-move triggers `vfs_rename` when dropping onto a folder.
 - TextEdit supports Ctrl+Left/Right word navigation.
 - Calendar supports editing title/time/location/notes and deleting events.
 - Calendar validates time input and keeps edit mode active on errors.
 - Finder drag indicator renders filename near cursor while dragging.
 - Finder highlights folder drop targets while dragging.
 - TextEdit supports Shift+Arrow selection with highlight.

### Build Robustness

**What changed**
- Added a preflight check in `scripts/build.sh` for gnu-efi headers.

**Why**
- Build failures due to missing `efi.h` should fail fast with a clear fix.

**How**
- Build script checks for `/usr/include/efi/efi.h` and prints the install command if missing.

---

## Release Documentation

**What changed**
- Added `RELEASE.md` with Debian 13 setup, build outputs, VirtualBox click‑by‑click, debugging, and end‑user guide.

**Why**
- A reproducible build and beginner‑friendly boot flow are required for release readiness.

**How**
- Documented environment setup, artifacts, and known issues in a single guide.
- Expanded VirtualBox steps to true click‑by‑click beginner flow.

### Time Format Consistency

**What changed**
- Menu bar clock now respects the 12/24-hour setting.

**Why**
- Time format should be consistent across Calendar and the global menu bar.

**How**
- Menu bar uses `settings_get()->time_24h` for formatting.

### Controls

| Key | Action |
|-----|--------|
| D | Show diagnostics |
| T | Show current time |
| Any | Echo to console |
| Mouse | Move cursor |

---

## Next Steps

The M1 prerequisites provide the foundation for M2 (Graphics). Next steps include:

1. **Double Buffering** - Eliminate screen tearing
2. **Gradient Rendering** - For Tahoe backgrounds
3. **Alpha Blending** - For glassmorphism effects
4. **Image Loading** - PNG/BMP for icons and wallpapers
5. **Improved Fonts** - Anti-aliased text rendering

---

*This document is maintained alongside the codebase. For the latest information, see the source code comments.*
