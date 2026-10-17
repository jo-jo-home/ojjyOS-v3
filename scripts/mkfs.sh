#!/bin/bash
# Build the ojjyOS filesystem image

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLS_DIR="$ROOT_DIR/tools"
SYSROOT="$ROOT_DIR/sysroot"
BUILD_DIR="$ROOT_DIR/build"

echo "=== Building ojjyOS Filesystem ==="

# Create build directory
mkdir -p "$BUILD_DIR"

# Build tools (includes mkarray, which embeds the image into a C header)
echo "Building tools..."
cd "$TOOLS_DIR"
make clean
make

# Create sysroot directories
echo "Creating directory structure..."
mkdir -p "$SYSROOT/System/Wallpapers"
mkdir -p "$SYSROOT/System/Fonts"
mkdir -p "$SYSROOT/System/Library"
mkdir -p "$SYSROOT/System/Library/Icons"
mkdir -p "$SYSROOT/System/Web"
mkdir -p "$SYSROOT/Applications/About.app/resources"
mkdir -p "$SYSROOT/Applications/Settings.app"
mkdir -p "$SYSROOT/Applications/Finder.app"
mkdir -p "$SYSROOT/Applications/Terminal.app"
mkdir -p "$SYSROOT/Applications/TextEdit.app"
mkdir -p "$SYSROOT/Applications/Notes.app"
mkdir -p "$SYSROOT/Applications/Preview.app"
mkdir -p "$SYSROOT/Applications/Calendar.app"
mkdir -p "$SYSROOT/Applications/Web.app"
mkdir -p "$SYSROOT/Applications/Calculator.app"
mkdir -p "$SYSROOT/Applications/Monitor.app"
mkdir -p "$SYSROOT/Users/guest/Desktop"
mkdir -p "$SYSROOT/Users/guest/Documents"
mkdir -p "$SYSROOT/Library/Preferences"
mkdir -p "$SYSROOT/Library/Accounts"
mkdir -p "$SYSROOT/Volumes"

# Generate icons
echo "Generating icons..."
./mkicon about "$SYSROOT/Applications/About.app/icon.raw"
./mkicon settings "$SYSROOT/Applications/Settings.app/icon.raw"
./mkicon finder "$SYSROOT/Applications/Finder.app/icon.raw"
./mkicon terminal "$SYSROOT/Applications/Terminal.app/icon.raw"
./mkicon textedit "$SYSROOT/Applications/TextEdit.app/icon.raw"
./mkicon notes "$SYSROOT/Applications/Notes.app/icon.raw"
./mkicon preview "$SYSROOT/Applications/Preview.app/icon.raw"
./mkicon calendar "$SYSROOT/Applications/Calendar.app/icon.raw"
./mkicon web "$SYSROOT/Applications/Web.app/icon.raw"
./mkicon calculator "$SYSROOT/Applications/Calculator.app/icon.raw"
./mkicon monitor "$SYSROOT/Applications/Monitor.app/icon.raw"
./mkicon folder "$SYSROOT/System/Library/Icons/Folder.raw"
./mkicon file "$SYSROOT/System/Library/Icons/File.raw"

# Generate wallpapers (smaller for testing - 640x480)
echo "Generating wallpapers..."
./mkwallpaper 640 480 tahoe_light "$SYSROOT/System/Wallpapers/Tahoe Light.raw"
./mkwallpaper 640 480 tahoe_dark "$SYSROOT/System/Wallpapers/Tahoe Dark.raw"
./mkwallpaper 640 480 gradient_blue "$SYSROOT/System/Wallpapers/Blue.raw"

# Create manifest for About.app
cat > "$SYSROOT/Applications/About.app/manifest.json" << 'EOF'
{
    "name": "About ojjyOS",
    "bundle_id": "com.ojjyos.about",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "system",
    "description": "System information and credits"
}
EOF

# Create About.app resources
cat > "$SYSROOT/Applications/About.app/resources/about.txt" << 'EOF'
About ojjyOS

This system app provides version info, display details, and credits.
EOF

# Create manifest for Settings.app
cat > "$SYSROOT/Applications/Settings.app/manifest.json" << 'EOF'
{
    "name": "Settings",
    "bundle_id": "com.ojjyos.settings",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "system",
    "description": "System preferences"
}
EOF

# Create manifest for Finder.app
cat > "$SYSROOT/Applications/Finder.app/manifest.json" << 'EOF'
{
    "name": "Finder",
    "bundle_id": "com.ojjyos.finder",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "system",
    "description": "File manager"
}
EOF

# Create manifest for Terminal.app
cat > "$SYSROOT/Applications/Terminal.app/manifest.json" << 'EOF'
{
    "name": "Terminal",
    "bundle_id": "com.ojjyos.terminal",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "utility",
    "description": "Command line interface"
}
EOF

# Create manifest for TextEdit.app
cat > "$SYSROOT/Applications/TextEdit.app/manifest.json" << 'EOF'
{
    "name": "TextEdit",
    "bundle_id": "com.ojjyos.textedit",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "productivity",
    "description": "Basic text editor"
}
EOF

# Create manifest for Notes.app
cat > "$SYSROOT/Applications/Notes.app/manifest.json" << 'EOF'
{
    "name": "Notes",
    "bundle_id": "com.ojjyos.notes",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "productivity",
    "description": "Quick notes"
}
EOF

# Create manifest for Preview.app
cat > "$SYSROOT/Applications/Preview.app/manifest.json" << 'EOF'
{
    "name": "Preview",
    "bundle_id": "com.ojjyos.preview",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "utility",
    "description": "Image viewer"
}
EOF

# Create manifest for Calendar.app
cat > "$SYSROOT/Applications/Calendar.app/manifest.json" << 'EOF'
{
    "name": "Calendar",
    "bundle_id": "com.ojjyos.calendar",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "productivity",
    "description": "Calendar"
}
EOF

# Create manifest for Web.app
cat > "$SYSROOT/Applications/Web.app/manifest.json" << 'EOF'
{
    "name": "Web",
    "bundle_id": "com.ojjyos.web",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "utility",
    "description": "Offline page browser"
}
EOF

# Create manifest for Calculator.app
cat > "$SYSROOT/Applications/Calculator.app/manifest.json" << 'EOF'
{
    "name": "Calculator",
    "bundle_id": "com.ojjyos.calculator",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "utility",
    "description": "Tahoe desktop calculator"
}
EOF

# Create manifest for Monitor.app
cat > "$SYSROOT/Applications/Monitor.app/manifest.json" << 'EOF'
{
    "name": "Activity Monitor",
    "bundle_id": "com.ojjyos.monitor",
    "version": "1.0.0",
    "executable": "executable",
    "icon": "icon.raw",
    "category": "utility",
    "description": "Live memory, driver, and system monitor"
}
EOF

# Create the Web app's offline pages (tiny markup:
#   "# text" = heading, ">id text" = link to <id>.page, else plain text)
mkdir -p "$SYSROOT/System/Web"
cat > "$SYSROOT/System/Web/home.page" << 'EOF'
# Welcome to ojjyOS Web

      *             .           *      .
          /\                   *
         /  \       /\        .      *
        /    \     /  \    *
   *   /      \   /    \       .
      /        \ /      \        *
     /   /\             \
    /   /  \   /\        \    .
   /___/    \ /  \___     \       *

  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   ~   T a h o e  -  L i q u i d   ~
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is the offline browser for ojjyOS. Pages are stored on the
system disk under /System/Web - no network connection needed.

>about About ojjyOS and its creator
>accounts User Accounts & Credentials Guide
>shortcuts Keyboard shortcuts
>release What is new in this release
EOF

cat > "$SYSROOT/System/Web/accounts.page" << 'EOF'
# User Accounts & Credentials

ojjyOS includes a built-in multi-user account and credential
system with salted SHA-256 password hashing.

Creating or managing accounts:
- On first boot into the Tahoe desktop, Setup Assistant prompts
  you to create your administrator account and password.
- Open Settings -> Users & Accounts to add accounts, change
  your password, lock the screen, or switch users.
- In Terminal or Console: adduser, passwd, login, su, whoami,
  users, lock, and logout.

>home Back to the start page
EOF

cat > "$SYSROOT/System/Web/about.page" << 'EOF'
# About ojjyOS

ojjyOS is a custom operating system with a macOS Tahoe-inspired
user interface. It is built from scratch on a custom kernel -
not based on Linux, BSD, Windows or macOS.

Created by Jonas Lee.

The system includes a Finder, a Terminal, TextEdit, Notes,
Preview, Calendar, Settings, and this offline Web browser.

>home Back to the start page
EOF

cat > "$SYSROOT/System/Web/shortcuts.page" << 'EOF'
# Keyboard shortcuts

Command (Super) key shortcuts:

Super + Space  -  Spotlight search
Super + L     -  Launchpad
Super + C     -  Control Center
Super + M     -  Mission Control
Super + Tab   -  Switch between running apps
Super + Q     -  Quit the active app

App shortcuts:

Super + S     -  Save (TextEdit, Notes)
Super + O     -  Open the last file
Super + C/X/V -  Copy / Cut / Paste

>home Back to the start page
EOF

cat > "$SYSROOT/System/Web/release.page" << 'EOF'
# ojjyOS 3.1.0

The "Make It Work" release.

The kernel now boots reliably: the bootloader loads a real ELF
kernel, the keyboard and mouse work through the driver model,
and the whole filesystem is reachable.

New in this release:

- Working Dock, Spotlight, Launchpad and Control Center
- Scroll wheel support across the system
- Desktop icons with double-click to open
- Working window close and minimize buttons
- The Web app: this offline browser
- Finder, Terminal, TextEdit and Calendar fixes throughout

Created by Jonas Lee.

>home Back to the start page
EOF

# Create system version file
cat > "$SYSROOT/System/version.txt" << 'EOF'
ojjyOS v3.1.0
Make It Work Release
Created by Jonas Lee
EOF

# Create sample user files
echo "Welcome to ojjyOS!" > "$SYSROOT/Users/guest/Desktop/Welcome.txt"
echo "This is a test document." > "$SYSROOT/Users/guest/Documents/Test.txt"

# Build OJFS image
echo "Building OJFS image..."
./mkojfs "$BUILD_DIR/system.ojfs" "$SYSROOT"

# Create C header with embedded filesystem
# (mkarray emits deterministic names - unlike the old xxd|sed pipeline,
#  which broke whenever the repo path differed from the expected one)
echo "Creating embedded filesystem header..."
./mkarray "$BUILD_DIR/system.ojfs" "$ROOT_DIR/kernel/src/fs/system_fs.h" embedded_fs

echo ""
echo "=== Filesystem build complete ==="
echo "OJFS image: $BUILD_DIR/system.ojfs"
echo "Embedded header: kernel/src/fs/system_fs.h"
ls -la "$BUILD_DIR/system.ojfs"
