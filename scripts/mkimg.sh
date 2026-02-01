#!/bin/bash
#
# ojjyOS v3 Disk Image Creator
#
# Creates a GPT disk image with EFI System Partition containing
# the bootloader and kernel.
#

set -e

# Directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"

# Files
BOOTLOADER="$BUILD_DIR/BOOTX64.EFI"
KERNEL="$BUILD_DIR/kernel.bin"
IMAGE="$BUILD_DIR/ojjyos3.img"
ESP_IMG="$BUILD_DIR/esp.img"

# Image size (64MB is plenty for MVP)
IMG_SIZE_MB=64

echo "Creating disk image..."

# Check required files
if [ ! -f "$BOOTLOADER" ]; then
    echo "Error: Bootloader not found: $BOOTLOADER"
    exit 1
fi

if [ ! -f "$KERNEL" ]; then
    echo "Error: Kernel not found: $KERNEL"
    exit 1
fi

# Create FAT32 ESP image
echo "  Creating ESP filesystem..."
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$((IMG_SIZE_MB - 1)) 2>/dev/null
mkfs.fat -F 32 "$ESP_IMG" >/dev/null

# Create directory structure and copy files
echo "  Copying files to ESP..."
mmd -i "$ESP_IMG" ::/EFI
mmd -i "$ESP_IMG" ::/EFI/BOOT
mmd -i "$ESP_IMG" ::/EFI/ojjyos
mcopy -i "$ESP_IMG" "$BOOTLOADER" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$ESP_IMG" "$KERNEL" ::/EFI/ojjyos/kernel.bin

# Create the final GPT disk image
echo "  Creating GPT disk image..."

# Create a raw disk image with GPT header space
dd if=/dev/zero of="$IMAGE" bs=1M count=$IMG_SIZE_MB 2>/dev/null

# Create GPT partition table
# Using sgdisk or fallback to parted
if command -v sgdisk &> /dev/null; then
    sgdisk -Z "$IMAGE" >/dev/null 2>&1 || true
    sgdisk -n 1:2048:+$((IMG_SIZE_MB - 1))M -t 1:ef00 -c 1:"EFI System" "$IMAGE" >/dev/null
else
    # Fallback: create a simple GPT-like structure manually
    # For simplicity, we'll just use the FAT image directly
    # VirtualBox can boot from a raw FAT image if configured as USB/removable
    cp "$ESP_IMG" "$IMAGE"
    echo "  Warning: sgdisk not found, using simple image format"
fi

# Copy ESP content into partition (at sector 2048 = byte 1048576)
if command -v sgdisk &> /dev/null; then
    dd if="$ESP_IMG" of="$IMAGE" bs=512 seek=2048 conv=notrunc 2>/dev/null
fi

# Clean up
rm -f "$ESP_IMG"

# Show image info
echo "  Disk image created: $IMAGE"
ls -lh "$IMAGE"
