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

# Build tools
echo "Building tools..."
cd "$TOOLS_DIR"
make clean
make

XXD_BIN="$(command -v xxd)"
if [ -z "$XXD_BIN" ]; then
    if [ -x "/usr/bin/xxd" ]; then
        XXD_BIN="/usr/bin/xxd"
    fi
fi
if [ -z "$XXD_BIN" ]; then
    echo "Error: xxd not found. Install vim-common."
    exit 1
fi

# Create sysroot directories
echo "Creating directory structure..."
mkdir -p "$SYSROOT/System/Wallpapers"
mkdir -p "$SYSROOT/System/Fonts"
mkdir -p "$SYSROOT/System/Library"
mkdir -p "$SYSROOT/System/Library/Icons"
mkdir -p "$SYSROOT/Applications/About.app/resources"
mkdir -p "$SYSROOT/Applications/Settings.app"
mkdir -p "$SYSROOT/Applications/Finder.app"
mkdir -p "$SYSROOT/Applications/Terminal.app"
mkdir -p "$SYSROOT/Applications/TextEdit.app"
mkdir -p "$SYSROOT/Applications/Notes.app"
mkdir -p "$SYSROOT/Applications/Preview.app"
mkdir -p "$SYSROOT/Applications/Calendar.app"
mkdir -p "$SYSROOT/Users/guest/Desktop"
mkdir -p "$SYSROOT/Users/guest/Documents"
mkdir -p "$SYSROOT/Library/Preferences"
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

# Create system version file
cat > "$SYSROOT/System/version.txt" << 'EOF'
ojjyOS v3.0.0-M2
Filesystem Milestone
Made by Jonas Lee
EOF

# Create sample user files
echo "Welcome to ojjyOS!" > "$SYSROOT/Users/guest/Desktop/Welcome.txt"
echo "This is a test document." > "$SYSROOT/Users/guest/Documents/Test.txt"

# Build OJFS image
echo "Building OJFS image..."
./mkojfs "$BUILD_DIR/system.ojfs" "$SYSROOT"

# Create C header with embedded filesystem
echo "Creating embedded filesystem header..."
"$XXD_BIN" -i "$BUILD_DIR/system.ojfs" > "$ROOT_DIR/kernel/src/fs/system_fs.h"
sed -i.bak 's/build_system_ojfs/embedded_fs/g' "$ROOT_DIR/kernel/src/fs/system_fs.h"
sed -i.bak 's/unsigned char/const uint8_t/g' "$ROOT_DIR/kernel/src/fs/system_fs.h"
rm -f "$ROOT_DIR/kernel/src/fs/system_fs.h.bak"

echo ""
echo "=== Filesystem build complete ==="
echo "OJFS image: $BUILD_DIR/system.ojfs"
echo "Embedded header: kernel/src/fs/system_fs.h"
ls -la "$BUILD_DIR/system.ojfs"
