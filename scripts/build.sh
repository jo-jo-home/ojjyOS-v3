#!/bin/bash
#
# ojjyOS v3 Build Script
#
# Builds the bootloader and kernel, creates a bootable disk image.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
BOOT_DIR="$ROOT_DIR/boot"
KERNEL_DIR="$ROOT_DIR/kernel"

# Output files
BOOTLOADER="$BUILD_DIR/BOOTX64.EFI"
KERNEL="$BUILD_DIR/kernel.bin"
IMAGE="$BUILD_DIR/ojjyos3.img"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  ojjyOS v3 Build System${NC}"
echo -e "${BLUE}========================================${NC}"
echo

# Create build directory
mkdir -p "$BUILD_DIR"

# Step 1: Build bootloader
echo -e "${YELLOW}[1/5] Building bootloader...${NC}"
if [ ! -f "/usr/include/efi/efi.h" ]; then
    echo -e "${RED}Error: gnu-efi headers not found (/usr/include/efi/efi.h)${NC}"
    echo "Install with: sudo apt install -y gnu-efi"
    exit 1
fi
cd "$BOOT_DIR"
make clean 2>/dev/null || true
make
cp ojjy-boot.efi "$BOOTLOADER"
echo -e "${GREEN}      Bootloader built: $BOOTLOADER${NC}"

# Step 2: Build filesystem image
echo -e "${YELLOW}[2/5] Building filesystem image...${NC}"
cd "$ROOT_DIR"
bash scripts/mkfs.sh

# Step 3: Build kernel
echo -e "${YELLOW}[3/5] Building kernel...${NC}"
cd "$KERNEL_DIR"
make clean 2>/dev/null || true
make
cp kernel.bin "$KERNEL"
echo -e "${GREEN}      Kernel built: $KERNEL${NC}"

# Step 4: Create disk image
echo -e "${YELLOW}[4/5] Creating disk image...${NC}"
cd "$ROOT_DIR"
bash scripts/mkimg.sh
echo -e "${GREEN}      Disk image created: $IMAGE${NC}"

# Step 5: Done
echo
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}  Build complete!${NC}"
echo -e "${BLUE}========================================${NC}"
echo
echo -e "Output files:"
echo -e "  Bootloader: ${BLUE}$BOOTLOADER${NC}"
echo -e "  Kernel:     ${BLUE}$KERNEL${NC}"
echo -e "  Disk Image: ${BLUE}$IMAGE${NC}"
echo
echo -e "To run in VirtualBox:"
echo -e "  ${YELLOW}bash scripts/run.sh${NC}"
echo
