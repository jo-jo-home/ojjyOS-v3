#!/bin/bash
#
# ojjyOS v3 Build Script
#
# Builds the bootloader and kernel, creates a bootable disk image.
# Supports both x86_64 and aarch64:
#   ARCH=x86_64  bash scripts/build.sh
#   ARCH=aarch64 bash scripts/build.sh
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Detect or accept ARCH
HOST_ARCH="$(uname -m)"
if [ "$HOST_ARCH" = "arm64" ]; then
    HOST_ARCH="aarch64"
fi
ARCH="${ARCH:-${1:-$HOST_ARCH}}"
if [ "$ARCH" = "arm64" ]; then ARCH="aarch64"; fi

if [ "$ARCH" = "aarch64" ]; then
    EFI_NAME="BOOTAA64.EFI"
else
    if [ "$ARCH" != "x86_64" ]; then
        echo "Unknown architecture: $ARCH" >&2
        exit 1
    fi
    EFI_NAME="BOOTX64.EFI"
fi

# Directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${OJJY_BUILD_DIR:-$ROOT_DIR/build/$ARCH}"
export OJJY_BUILD_DIR="$BUILD_DIR"
BOOT_DIR="$ROOT_DIR/boot"
KERNEL_DIR="$ROOT_DIR/kernel"

# Output files
BOOTLOADER="$BUILD_DIR/$EFI_NAME"
KERNEL="$BUILD_DIR/kernel.bin"
IMAGE="$BUILD_DIR/ojjyos3.img"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  ojjyOS v3 Build System ($ARCH)${NC}"
echo -e "${BLUE}========================================${NC}"
echo

# Create build directory
mkdir -p "$BUILD_DIR"
# The boot/kernel objects and generated system image are shared build inputs.
# Refuse concurrent full builds rather than mixing architectures silently.
LOCK_DIR="$ROOT_DIR/build/.build-lock"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    echo "Another build is active (or left $LOCK_DIR after interruption)." >&2
    echo "Check for an active build before removing that lock directory." >&2
    exit 1
fi
trap 'rmdir "$LOCK_DIR"' EXIT

# Step 1: Build bootloader
echo -e "${YELLOW}[1/5] Building bootloader ($ARCH)...${NC}"
cd "$BOOT_DIR"
make clean 2>/dev/null || true
make ARCH="$ARCH"
cp ojjy-boot.efi "$BOOTLOADER"
# For backwards compatibility with scripts expecting BOOTX64.EFI when ARCH=x86_64
if [ "$ARCH" = "x86_64" ]; then
    cp ojjy-boot.efi "$BUILD_DIR/BOOTX64.EFI"
fi
echo -e "${GREEN}      Bootloader built: $BOOTLOADER${NC}"

# Step 2: Build filesystem image
echo -e "${YELLOW}[2/5] Building filesystem image...${NC}"
cd "$ROOT_DIR"
bash scripts/mkfs.sh

# Step 3: Build kernel
echo -e "${YELLOW}[3/5] Building kernel ($ARCH)...${NC}"
cd "$KERNEL_DIR"
make clean 2>/dev/null || true
make ARCH="$ARCH"
cp kernel.bin "$KERNEL"
echo -e "${GREEN}      Kernel built: $KERNEL${NC}"

# Step 4: Create disk image
echo -e "${YELLOW}[4/5] Creating disk image...${NC}"
cd "$ROOT_DIR"
ARCH="$ARCH" bash scripts/mkimg.sh
echo -e "${GREEN}      Disk image created: $IMAGE${NC}"

# Step 5: Done
echo
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}  Build complete ($ARCH)!${NC}"
echo -e "${BLUE}========================================${NC}"
echo
echo -e "Output files:"
echo -e "  Bootloader: ${BLUE}$BOOTLOADER${NC}"
echo -e "  Kernel:     ${BLUE}$KERNEL${NC}"
echo -e "  Disk Image: ${BLUE}$IMAGE${NC}"
echo
echo -e "To run in QEMU (Apple Silicon / aarch64 or x86_64):"
echo -e "  ${YELLOW}bash scripts/run-qemu.sh $ARCH${NC}"
echo -e "To run in VirtualBox (x86_64):"
echo -e "  ${YELLOW}bash scripts/run.sh${NC}"
echo
