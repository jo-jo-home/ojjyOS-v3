#!/bin/bash
# Build the native Mac host without Xcode. Created by Jonas Lee.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${OJJY_VMAPP_DIR:-$ROOT/build/ojjyOS.app}"
# Sign outside iCloud: FileProvider can restore FinderInfo between xattr
# removal and codesign. Install the already signed bundle into build/.
STAGE_BASE="${TMPDIR:-/private/var/tmp/}opencode"
mkdir -p "$STAGE_BASE"
STAGE="$(mktemp -d "$STAGE_BASE/ojjyos-vmapp.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
APP="$STAGE/ojjyOS.app"
mkdir -p "$APP/Contents/MacOS"
swiftc -O -warnings-as-errors -target arm64-apple-macos13.0 "$ROOT/vmapp/main.swift" \
    -framework Virtualization -framework AppKit -o "$APP/Contents/MacOS/ojjyOS"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.jonaslee.ojjyos</string>
<key>CFBundleName</key><string>ojjyOS</string>
<key>CFBundleDisplayName</key><string>ojjyOS</string>
<key>CFBundleExecutable</key><string>ojjyOS</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>3.1.0</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSPrincipalClass</key><string>NSApplication</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSHumanReadableCopyright</key><string>Created by Jonas Lee</string>
</dict></plist>
PLIST
codesign --sign - --force --entitlements "$ROOT/vmapp/ojjyOS.entitlements" "$APP"
mkdir -p "$(dirname "$DEST")"
ditto --norsrc "$APP" "$DEST"
codesign --verify "$DEST/Contents/MacOS/ojjyOS"
echo "Built $DEST"
