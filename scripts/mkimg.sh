#!/bin/bash
#
# ojjyOS v3 Disk Image Creator
#
# Creates a GPT disk image with EFI System Partition containing
# the bootloader (BOOTX64.EFI or BOOTAA64.EFI) and kernel.
#

set -e

# Directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"

ARCH="${ARCH:-x86_64}"
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    ARCH="aarch64"
    EFI_NAME="BOOTAA64.EFI"
else
    ARCH="x86_64"
    EFI_NAME="BOOTX64.EFI"
fi

# Files
BOOTLOADER="$BUILD_DIR/$EFI_NAME"
KERNEL="$BUILD_DIR/kernel.bin"
IMAGE="$BUILD_DIR/ojjyos3.img"
ESP_IMG="$BUILD_DIR/esp.img"

# Image size: 128MB total
#   - Partition 1: EFI System Partition (32 MB)
#   - Partition 2: ojjyOS data partition (persistence, rest of disk)
IMG_SIZE_MB=128
SECTORS=$((IMG_SIZE_MB * 2048))
# GPT reserves the last 33 sectors for the backup header + table
PART_LAST=$((SECTORS - 34))
ESP_SECTORS=$((32 * 2048))
DATA_START=$((2048 + ESP_SECTORS))

echo "Creating disk image ($ARCH)..."

# Check required files
if [ ! -f "$BOOTLOADER" ]; then
    echo "Error: Bootloader not found: $BOOTLOADER"
    echo "Run: ARCH=$ARCH bash scripts/build.sh"
    exit 1
fi

if [ ! -f "$KERNEL" ]; then
    echo "Error: Kernel not found: $KERNEL"
    echo "Run: ARCH=$ARCH bash scripts/build.sh"
    exit 1
fi

if ! command -v sgdisk &> /dev/null; then
    echo "Error: sgdisk not found. Install with: sudo apt install -y gdisk (or brew install gptfdisk)"
    exit 1
fi

# Create FAT32 ESP image sized to fit the ESP partition
echo "  Creating ESP filesystem..."
dd if=/dev/zero of="$ESP_IMG" bs=512 count=$ESP_SECTORS 2>/dev/null
mkfs.fat -F 32 "$ESP_IMG" >/dev/null

# Create directory structure and copy files
echo "  Copying files to ESP (/EFI/BOOT/$EFI_NAME)..."
mmd -i "$ESP_IMG" ::/EFI
mmd -i "$ESP_IMG" ::/EFI/BOOT
mmd -i "$ESP_IMG" ::/EFI/ojjyos
mcopy -i "$ESP_IMG" "$BOOTLOADER" "::/EFI/BOOT/$EFI_NAME"
mcopy -i "$ESP_IMG" "$KERNEL" ::/EFI/ojjyos/kernel.bin

# Create the final GPT disk image
echo "  Creating GPT disk image..."
dd if=/dev/zero of="$IMAGE" bs=1M count=$IMG_SIZE_MB 2>/dev/null

# Partition 1: ESP.
# Partition 2: ojjyOS data partition, used by the kernel's persistence
# layer. The start sector must match OJJY_DATA_START_SECTOR in
# kernel/src/fs/persist.h.
sgdisk -Z "$IMAGE" >/dev/null 2>&1 || true
sgdisk -n 1:2048:$((DATA_START - 1)) -t 1:ef00 -c 1:"EFI System" "$IMAGE" >/dev/null
sgdisk -n 2:$DATA_START:$PART_LAST -t 2:8300 -c 2:"ojjyOS Data" "$IMAGE" >/dev/null

# Copy ESP content into partition 1 (at sector 2048 = byte 1048576)
dd if="$ESP_IMG" of="$IMAGE" bs=512 seek=2048 conv=notrunc 2>/dev/null

# Verify the layout
sgdisk -p "$IMAGE" | sed 's/^/  /'

# Clean up
rm -f "$ESP_IMG"

# Show image info
echo "  Disk image created: $IMAGE"
ls -lh "$IMAGE"
