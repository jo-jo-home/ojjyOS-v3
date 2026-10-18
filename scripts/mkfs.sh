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

# Built-in pages for the Web browser (real HTML, opened as ojjy:<name>)
mkdir -p "$SYSROOT/System/Web"
rm -f "$SYSROOT"/System/Web/*.page
cat > "$SYSROOT/System/Web/home.html" << 'PAGE'
<html><head><title>Start Page</title></head><body>
<h1>ojjyOS Web</h1>
<p>A web browser written from scratch for the ojjyOS kernel &mdash; created by <b>Jonas Lee</b>.</p>
<form action="http://wiby.me/" method="get">
<input name="q" size="38" placeholder="Search the classic web with Wiby"> <input type="submit" value="Search">
</form>
<h3>Favorites</h3>
<ul>
<li><a href="http://wiby.me/">Wiby</a> &mdash; a search engine for the classic web</li>
<li><a href="http://frogfind.com/">FrogFind</a> &mdash; simplified search and reader (when online)</li>
<li><a href="http://68k.news/">68k.news</a> &mdash; today's headlines, lightweight</li>
<li><a href="http://info.cern.ch/hypertext/WWW/TheProject.html">The first website</a> (CERN, 1991)</li>
<li><a href="http://example.com/">example.com</a></li>
<li><a href="http://neverssl.com/">NeverSSL</a></li>
<li><a href="http://textfiles.com/">textfiles.com</a></li>
</ul>
<h3>Built-in pages</h3>
<p><a href="ojjy:about">About ojjyOS</a> &nbsp;|&nbsp; <a href="ojjy:shortcuts">Keyboard shortcuts</a>
&nbsp;|&nbsp; <a href="ojjy:accounts">Accounts guide</a> &nbsp;|&nbsp; <a href="ojjy:release">What's new</a></p>
<h3>Tips</h3>
<ul>
<li>Type words into the address bar to search, or a web address to open it.</li>
<li><b>Cmd+L</b> address bar, <b>Cmd+R</b> reload, <b>Cmd+[</b> / <b>Cmd+]</b> back and forward.</li>
<li>Secure <code>https://</code> sites are not supported yet &mdash; try their <code>http://</code> address.</li>
<li>No network? Turn on <b>Wi-Fi</b> in Control Center.</li>
</ul>
<pre>
   ~  ~  ~   T a h o e  -  L i q u i d   ~  ~  ~
</pre>
</body></html>
PAGE

cat > "$SYSROOT/System/Web/about.html" << 'PAGE'
<html><head><title>About ojjyOS</title></head><body>
<h1>About ojjyOS</h1>
<p>ojjyOS is a custom operating system with a macOS Tahoe-inspired user interface. It is built from
scratch on a custom kernel &mdash; not based on Linux, BSD, Windows or macOS.</p>
<p><b>Created by Jonas Lee.</b></p>
<h3>Inside</h3>
<ul>
<li>Custom kernel for ARM64 (Apple Silicon VMs) and x86-64</li>
<li>Its own network stack: Ethernet, ARP, IPv4, DHCP, DNS, TCP and HTTP</li>
<li>This browser: HTML layout engine, links, forms and history</li>
<li>Finder, Terminal, TextEdit, Notes, Preview, Calendar, Calculator, Settings and more</li>
</ul>
<p><a href="ojjy:home">Back to the start page</a></p>
</body></html>
PAGE

cat > "$SYSROOT/System/Web/accounts.html" << 'PAGE'
<html><head><title>Accounts Guide</title></head><body>
<h1>User Accounts &amp; Credentials</h1>
<p>ojjyOS includes a multi-user account system with salted SHA-256 password hashing.</p>
<ul>
<li>On first boot, Setup Assistant creates your account and password.</li>
<li>Open <b>Settings &rarr; Users &amp; Accounts</b> to add accounts, change your password, lock the
screen, or switch users.</li>
<li>In Terminal: <code>adduser</code>, <code>passwd</code>, <code>su</code>, <code>whoami</code>,
<code>users</code>, <code>lock</code>.</li>
</ul>
<p><a href="ojjy:home">Back to the start page</a></p>
</body></html>
PAGE

cat > "$SYSROOT/System/Web/shortcuts.html" << 'PAGE'
<html><head><title>Keyboard Shortcuts</title></head><body>
<h1>Keyboard shortcuts</h1>
<h3>System</h3>
<table>
<tr><td><b>Cmd+Space</b></td><td>Spotlight search</td></tr>
<tr><td><b>Cmd+L</b></td><td>Launchpad (address bar in Web)</td></tr>
<tr><td><b>Cmd+C</b></td><td>Control Center (copy in TextEdit, Notes and Finder)</td></tr>
<tr><td><b>Cmd+M</b></td><td>Mission Control</td></tr>
<tr><td><b>Cmd+Tab</b></td><td>Switch between running apps</td></tr>
<tr><td><b>Ctrl+Cmd+Q</b></td><td>Lock screen</td></tr>
</table>
<h3>Web</h3>
<table>
<tr><td><b>Cmd+L</b></td><td>Edit the address</td></tr>
<tr><td><b>Cmd+R</b></td><td>Reload</td></tr>
<tr><td><b>Cmd+[ / Cmd+]</b></td><td>Back / Forward</td></tr>
<tr><td><b>Space / Shift+Space</b></td><td>Page down / up</td></tr>
<tr><td><b>Tab</b></td><td>Next text field</td></tr>
</table>
<h3>Editing</h3>
<p><b>Cmd+S</b> save, <b>Cmd+O</b> open the last file, <b>Cmd+C / X / V</b> copy, cut and paste.</p>
<p><a href="ojjy:home">Back to the start page</a></p>
</body></html>
PAGE

cat > "$SYSROOT/System/Web/release.html" << 'PAGE'
<html><head><title>What's New</title></head><body>
<h1>ojjyOS 3.1</h1>
<h3>New</h3>
<ul>
<li><b>Networking</b> &mdash; a from-scratch network stack with DHCP, DNS and TCP.</li>
<li><b>A real web browser</b> &mdash; open http:// websites, follow links, search and fill in forms.</li>
<li><b>Terminal network tools</b> &mdash; <code>ifconfig</code>, <code>ping</code>, <code>nslookup</code>,
<code>curl</code>.</li>
<li>Accurate, fast mouse pointer and a cleaner blue desktop.</li>
</ul>
<h3>Earlier</h3>
<ul>
<li>Protected ARM64 apps (<code>run hello</code> in Terminal)</li>
<li>Reliable boot, input and saved storage</li>
<li>Dock, Spotlight, Launchpad, Control Center and Mission Control</li>
</ul>
<p>Created by Jonas Lee. <a href="ojjy:home">Back to the start page</a></p>
</body></html>
PAGE

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
bash "$SCRIPT_DIR/build-programs.sh"
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
