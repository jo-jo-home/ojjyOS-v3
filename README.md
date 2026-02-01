# ojjyOS v3

A custom operating system with a macOS Tahoe-inspired user interface.

**Created by Jonas Lee**

## Current Milestone: M1 Prerequisites

This milestone implements:
- **Driver Model**: Registration, lifecycle, IRQ dispatch
- **Input Subsystem**: Unified event queue for keyboard and mouse
- **PS/2 Keyboard**: Integrated with driver model
- **PS/2 Mouse**: Full driver with scroll wheel support
- **ATA Disk**: PIO mode IDE driver
- **Block Cache**: LRU cache for disk sectors
- **RTC**: Real-time clock for date/time
- **Diagnostics**: In-OS status display

## Building on Debian 13

### Prerequisites

```bash
# Update package lists
sudo apt update

# Install build tools
sudo apt install -y \
    build-essential \
    clang \
    lld \
    nasm \
    gnu-efi \
    mtools \
    dosfstools \
    gdisk \
    vim-common

# Install VirtualBox
sudo apt install -y virtualbox
```

### Build

```bash
# Make scripts executable
chmod +x scripts/*.sh

# Build filesystem image (OJFS)
./scripts/mkfs.sh

# Build everything
./scripts/build.sh
```

### Run

```bash
./scripts/run.sh
```

## VirtualBox Configuration

### Required Settings

| Setting | Value | Notes |
|---------|-------|-------|
| **System → EFI** | ✓ Enabled | Required for UEFI boot |
| **System → RAM** | 2048 MB | Minimum recommended |
| **System → CPUs** | 2 | Optional |
| **Display → Video Memory** | 128 MB | For framebuffer |
| **Display → Graphics Controller** | VMSVGA | Best compatibility |
| **Storage → Controller** | PIIX4 (IDE) | For ATA disk driver |
| **Serial → Port 1** | COM1, Raw File | Debug output |

### Disk Controller Setup (Important for ATA)

For the ATA disk driver to work:

1. **Settings → Storage**
2. **Add Controller** → **IDE (PIIX4)**
3. Attach the VDI/IMG as IDE Primary Master

If using SATA (AHCI), the ATA driver won't detect the disk. The current implementation uses legacy IDE mode.

### Alternative: No Disk

The system works without a disk attached. Disk read test will report "no disk attached".

## Project Structure

```
ojjyos-v3/
├── boot/                     # UEFI bootloader
│   └── ojjy-boot.c
├── kernel/
│   ├── src/
│   │   ├── main.c           # Kernel entry
│   │   ├── drivers/         # Driver subsystem
│   │   │   ├── driver.c     # Driver model
│   │   │   ├── input.c      # Input event queue
│   │   │   ├── ps2_keyboard.c
│   │   │   ├── ps2_mouse.c
│   │   │   ├── ata.c        # ATA/IDE disk
│   │   │   ├── rtc.c        # Real-time clock
│   │   │   ├── block_cache.c
│   │   │   └── diagnostics.c
│   │   └── ...              # Core kernel
│   └── linker.ld
├── scripts/
│   ├── build.sh
│   ├── mkfs.sh
│   ├── mkimg.sh
│   └── run.sh
├── FILESYSTEM.md            # Filesystem layout + bundle spec
├── DESKTOP.md               # Desktop behaviors and shortcuts
├── APPS.md                  # Core app specs
├── RELEASE.md               # Build + run + debug guide
└── build/                    # Output
```

## Acceptance Tests

### Mouse Cursor

1. Boot the OS
2. Move mouse in VM window
3. **Expected**: Black arrow cursor follows mouse movement

### Disk Read

1. Boot with IDE disk attached
2. Look for "Testing disk read..." output
3. **Expected**: "Read sector 0 successfully!"

### Timer

1. Press 'T' in the running OS
2. **Expected**: Current date/time displayed

### Diagnostics

1. Press 'D' in the running OS
2. **Expected**: Full system status with driver list, memory, cache stats

### Filesystem (M2)

1. `ls /Applications`
2. **Expected**: `About.app` listed as an app bundle

1. `launch About.app`
2. **Expected**: About screen printed to console

1. `cat /System/Wallpapers/Tahoe\ Light.raw`
2. **Expected**: Raw wallpaper data prints

### UI Compositor (M2/M3)

1. `ui`
2. **Expected**: Glass window with blur, rounded corners, soft shadow
3. Optional: `ui dark` for Tahoe Dark mode

### Desktop Experience

1. Press Super+Space
2. **Expected**: Spotlight overlay appears

1. Move cursor over Dock icons
2. **Expected**: Icons magnify under cursor

1. Press Super+L
2. **Expected**: Launchpad grid of apps

1. Launch an app from Dock
2. **Expected**: Menu bar shows active app name

1. Press Super+Tab
2. **Expected**: App switcher appears and cycles selection

### Core Apps

1. Open Finder from Dock
2. Browse `/Users` and open a `.txt` file
3. **Expected**: TextEdit opens with file contents

1. Open Settings → Wallpaper
2. Select Tahoe Dark
3. **Expected**: Wallpaper switches

1. Open Notes from Dock
2. Type text, press Super+S
3. **Expected**: Notes.txt updated in `/Users/guest/Documents`

1. Open Preview
2. Click Tahoe Light/Dark thumbnail
3. **Expected**: Wallpaper updates immediately

1. Open Calendar
2. Switch month with arrows, select a day
3. **Expected**: Agenda updates for selected day

1. Toggle 12/24-hour time in Settings → Appearance
2. **Expected**: Calendar time display updates

## Debugging

### Serial Log

```bash
# Watch serial output in real-time
tail -f /tmp/ojjyos-serial.log
```

### Common Issues

| Problem | Solution |
|---------|----------|
| No mouse movement | Ensure VM captures mouse (click in VM window) |
| Disk not detected | Use IDE controller, not SATA/AHCI |
| Erratic cursor | PS/2 mouse sync issue, will recover |

## Driver Model

### Interface

```c
typedef struct {
    bool (*probe)(Driver *drv);      // Detect hardware
    int (*init)(Driver *drv);        // Initialize driver
    int (*shutdown)(Driver *drv);    // Clean shutdown
    bool (*handle_irq)(Driver *drv, uint8_t irq);  // IRQ handler
    ssize_t (*read)(Driver *drv, void *buf, size_t count, uint64_t offset);
    ssize_t (*write)(Driver *drv, const void *buf, size_t count, uint64_t offset);
    int (*ioctl)(Driver *drv, uint32_t cmd, void *arg);  // Device control
} DriverOps;
```

### Error Containment

- Drivers track error count, IRQ count, and I/O bytes
- After 10 errors, non-critical drivers are disabled
- Critical drivers (keyboard, timer) cannot be disabled
- All driver errors logged to serial with timestamps
- Driver state machine: REGISTERED -> PROBING -> INITIALIZING -> READY

## Input Event System

```c
typedef struct {
    InputEventType type;     // KEY_PRESS, MOUSE_MOVE, MOUSE_BUTTON_DOWN, etc.
    uint64_t timestamp;      // Tick count

    union {
        struct { uint8_t scancode; KeyCode keycode; char ascii; uint8_t modifiers; } key;
        struct { int32_t dx, dy, x, y; } motion;
        struct { MouseButton button; int32_t x, y; uint8_t modifiers; } button;
        struct { int32_t dx, dy, x, y; } scroll;
    };
} InputEvent;

// Usage
while (input_has_event()) {
    InputEvent event;
    input_poll_event(&event);  // Non-blocking poll
    // Process event
}

// Helper functions for drivers
input_post_key_event(type, scancode, keycode, ascii);
input_post_mouse_move(dx, dy);
input_post_mouse_button(type, button);
input_post_mouse_scroll(dx, dy);
```

## License

Copyright (c) 2024 Jonas Lee. All rights reserved.
