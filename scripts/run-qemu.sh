#!/bin/bash
#
# ojjyOS v3 QEMU Runner (Apple Silicon / aarch64 & x86_64)
#
# Runs ojjyOS natively on Apple Silicon Macs via qemu-system-aarch64
# with Hypervisor.framework (HVF) acceleration, ramfb GOP display,
# PL011 UART, PL031 RTC, and virtio-mmio keyboard/tablet/block devices.
#
# Usage:
#   bash scripts/run-qemu.sh [aarch64|x86_64]
#
# ojjyOS - Created by Jonas Lee
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
IMAGE="$BUILD_DIR/ojjyos3.img"
SERIAL_LOG="/tmp/ojjyos-serial.log"

ARCH="${1:-aarch64}"
if [ "$ARCH" = "arm64" ]; then
    ARCH="aarch64"
fi

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  ojjyOS v3 QEMU Runner ($ARCH)${NC}"
echo -e "${BLUE}========================================${NC}"
echo

if [ ! -f "$IMAGE" ]; then
    echo -e "${RED}Error: Disk image not found: $IMAGE${NC}"
    echo -e "Build the $ARCH disk image first:"
    echo -e "  ${YELLOW}ARCH=$ARCH bash scripts/build.sh${NC}"
    exit 1
fi

rm -f "$SERIAL_LOG"

if [ "$ARCH" = "aarch64" ]; then
    if ! command -v qemu-system-aarch64 &> /dev/null; then
        echo -e "${RED}Error: qemu-system-aarch64 not found.${NC}"
        echo "Install on macOS with:   brew install qemu"
        echo "Install on Debian with:  sudo apt install -y qemu-system-arm qemu-efi-aarch64"
        exit 1
    fi

    # Probe EDK2 aarch64 UEFI firmware paths (Homebrew on Apple Silicon / Intel Mac & Linux)
    FIRMWARE=""
    for fw in \
        "/opt/homebrew/share/qemu/edk2-aarch64-code.fd" \
        "/usr/local/share/qemu/edk2-aarch64-code.fd" \
        "/opt/homebrew/opt/qemu/share/qemu/edk2-aarch64-code.fd" \
        "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd" \
        "/usr/share/AAVMF/AAVMF_CODE.fd" \
        "/usr/share/edk2/aarch64/QEMU_EFI.fd" \
        "/usr/share/ovmf/AAVMF_CODE.fd"; do
        if [ -f "$fw" ]; then
            FIRMWARE="$fw"
            break
        fi
    done

    if [ -z "$FIRMWARE" ]; then
        echo -e "${RED}Error: Could not find aarch64 UEFI firmware (edk2-aarch64-code.fd / QEMU_EFI.fd).${NC}"
        echo "Install QEMU via Homebrew (brew install qemu) or apt (sudo apt install qemu-efi-aarch64)."
        exit 1
    fi

    # QEMU on macOS: Apple Hypervisor.framework (HVF) does not emulate GICv2
    # or ramfb unaligned MMIO, so use multi-threaded TCG (4 cores) with
    # modern virtio-mmio v2 (force-legacy=false) for reliable boot + input + disk.
    ACCEL_ARGS=("-accel" "tcg,thread=multi" "-cpu" "cortex-a72" "-smp" "4")
    ACCEL_DESC="MTTCG (4-core ARM64 multi-threaded JIT)"

    echo -e "  Architecture: ${GREEN}aarch64 (virt, GICv2 + virtio-mmio v2)${NC}"
    echo -e "  Accelerator:  ${GREEN}$ACCEL_DESC${NC}"
    echo -e "  UEFI BIOS:    ${BLUE}$FIRMWARE${NC}"
    echo -e "  Disk Image:   ${BLUE}$IMAGE${NC}"
    echo -e "  Serial Log:   ${BLUE}$SERIAL_LOG${NC}"
    echo
    echo -e "In another terminal, watch serial output with:"
    echo -e "  ${YELLOW}tail -f $SERIAL_LOG${NC}"
    echo

    exec qemu-system-aarch64 \
        -M virt,gic-version=2 \
        -m 2048 \
        "${ACCEL_ARGS[@]}" \
        -bios "$FIRMWARE" \
        -global virtio-mmio.force-legacy=false \
        -device ramfb \
        -device virtio-keyboard-device \
        -device virtio-tablet-device \
        -drive file="$IMAGE",format=raw,if=none,id=hd \
        -device virtio-blk-device,drive=hd \
        -serial file:"$SERIAL_LOG"

elif [ "$ARCH" = "x86_64" ]; then
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        echo -e "${RED}Error: qemu-system-x86_64 not found.${NC}"
        echo "Install on macOS with:   brew install qemu"
        echo "Install on Debian with:  sudo apt install -y qemu-system-x86 ovmf"
        exit 1
    fi

    FIRMWARE=""
    for fw in \
        "/opt/homebrew/share/qemu/edk2-x86_64-code.fd" \
        "/usr/local/share/qemu/edk2-x86_64-code.fd" \
        "/usr/share/ovmf/OVMF.fd" \
        "/usr/share/OVMF/OVMF_CODE.fd" \
        "/usr/share/edk2/ovmf/OVMF_CODE.fd"; do
        if [ -f "$fw" ]; then
            FIRMWARE="$fw"
            break
        fi
    done

    if [ -z "$FIRMWARE" ]; then
        echo -e "${RED}Error: Could not find x86_64 OVMF UEFI firmware.${NC}"
        echo "Install QEMU via Homebrew (brew install qemu) or apt (sudo apt install ovmf)."
        exit 1
    fi

    echo -e "  Architecture: ${GREEN}x86_64 (pc, PIIX IDE + PS/2)${NC}"
    echo -e "  UEFI BIOS:    ${BLUE}$FIRMWARE${NC}"
    echo -e "  Disk Image:   ${BLUE}$IMAGE${NC}"
    echo -e "  Serial Log:   ${BLUE}$SERIAL_LOG${NC}"
    echo
    echo -e "In another terminal, watch serial output with:"
    echo -e "  ${YELLOW}tail -f $SERIAL_LOG${NC}"
    echo

    exec qemu-system-x86_64 \
        -M pc \
        -m 2048 \
        -bios "$FIRMWARE" \
        -vga std \
        -drive file="$IMAGE",format=raw \
        -serial file:"$SERIAL_LOG"
else
    echo -e "${RED}Error: Unknown architecture '$ARCH' (expected aarch64 or x86_64)${NC}"
    exit 1
fi
