#!/bin/bash
#
# ojjyOS v3 QEMU Runner (Apple Silicon / aarch64 & x86_64)
#
# Runs an ARM64 guest on Apple Silicon Macs via qemu-system-aarch64
# with single-vCPU TCG emulation (not HVF), ramfb GOP display,
# PL011 UART, PL031 RTC, and virtio-mmio keyboard/tablet/block devices.
#
# Usage:
#   bash scripts/run-qemu.sh [aarch64|x86_64]
#
# ARM display size: OJJY_RES=WIDTHxHEIGHT (default 1440x900, 16:10 = the
# full-screen area of a notched MacBook). Use 1600x900 on a 16:9 monitor.
# OJJY_DISPLAY=ramfb restores the old 4:3-only firmware display.
# OJJY_FULLSCREEN=1 starts in macOS full screen (Zoom To Fit is always on).
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

ARCH="${1:-aarch64}"
if [ "$ARCH" = "arm64" ]; then
    ARCH="aarch64"
fi

IMAGE="$BUILD_DIR/$ARCH/ojjyos3.img"
SERIAL_LOG="$BUILD_DIR/$ARCH/serial.log"
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

    # QEMU on macOS: Apple HVF does not emulate this machine config
    # (GICv2/ramfb), so use multi-threaded TCG. The kernel is strictly
    # single-CPU (secondary cores are parked in entry.S), so one vCPU
    # is correct and fast; the io threads still run alongside.
    ACCEL_ARGS=("-accel" "tcg,thread=multi" "-cpu" "cortex-a72" "-smp" "1")
    ACCEL_DESC="MTTCG (ARM64 JIT, 1 vCPU)"

    # virtio-gpu takes any screen size, so the guest can match the Mac's
    # screen shape; ramfb firmware only offers 4:3 modes (black bars).
    RES="${OJJY_RES:-1440x900}"
    if [[ ! "$RES" =~ ^[0-9]+x[0-9]+$ ]]; then
        echo -e "${RED}Error: OJJY_RES must look like 1440x900${NC}"; exit 1
    fi
    if [ "${OJJY_DISPLAY:-gpu}" = "ramfb" ]; then
        GPU_DEVICE="ramfb"; RES="1024x768"
    else
        GPU_DEVICE="virtio-gpu-device,xres=${RES%x*},yres=${RES#*x}"
    fi

    # One visible pointer: the guest's fast scanout overlay. Absolute tablet
    # coordinates remain 1:1; do not show a second host pointer over it.
    if [ "$(uname -s)" = "Darwin" ]; then
        # zoom-to-fit scales the guest to the window/screen; OJJY_FULLSCREEN=1
        # starts in full screen right away.
        FULL="off"; [ "${OJJY_FULLSCREEN:-0}" = "1" ] && FULL="on"
        DISPLAY_ARGS=(-display "cocoa,show-cursor=off,zoom-to-fit=on,full-screen=$FULL")
    else
        DISPLAY_ARGS=()
    fi

    echo -e "  Architecture: ${GREEN}aarch64 (virt, GICv2 + virtio-mmio v2)${NC}"
    echo -e "  Accelerator:  ${GREEN}$ACCEL_DESC${NC}"
    echo -e "  Display:      ${GREEN}$RES ($GPU_DEVICE)${NC}"
    echo -e "  UEFI BIOS:    ${BLUE}$FIRMWARE${NC}"
    echo -e "  Disk Image:   ${BLUE}$IMAGE${NC}"
    echo -e "  Serial Log:   ${BLUE}$SERIAL_LOG${NC}"
    echo
    echo -e "In another terminal, watch serial output with:"
    echo -e "  ${YELLOW}tail -f $SERIAL_LOG${NC}"
    echo
    echo -e "${YELLOW}Display tips (macOS):${NC}"
    echo    "  - Fill the screen: QEMU menu View -> Enter Fullscreen (Zoom To Fit is"
    echo    "    already on). Or start that way: OJJY_FULLSCREEN=1 bash scripts/run-qemu.sh"
    echo    "  - Guest is $RES; set OJJY_RES=1600x900 for a 16:9 monitor"
    echo    "  - Release a captured mouse/keyboard: Ctrl-Option-G"
    echo

    exec qemu-system-aarch64 \
        -M virt,gic-version=2 \
        -m 2048 \
        "${ACCEL_ARGS[@]}" \
        "${DISPLAY_ARGS[@]}" \
        -bios "$FIRMWARE" \
        -global virtio-mmio.force-legacy=false \
        -device "$GPU_DEVICE" \
        -device virtio-keyboard-device \
        -device virtio-tablet-device \
        -drive file="$IMAGE",format=raw,if=none,id=hd \
        -device virtio-blk-device,drive=hd \
        -netdev user,id=net0 \
        -device virtio-net-device,netdev=net0 \
        -device virtio-rng-device \
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

    FIRMWARE_ARGS=(-bios "$FIRMWARE")
    VARS_TEMPLATE="$(dirname "$FIRMWARE")/edk2-i386-vars.fd"
    if [ -f "$VARS_TEMPLATE" ]; then
        # Homebrew's split code file is not a complete, power-of-two BIOS.
        # Keep a private writable pflash image, preserving NVRAM on reruns.
        PFLASH="$BUILD_DIR/x86_64/firmware.fd"
        if [ ! -f "$PFLASH" ]; then
            python3 - "$VARS_TEMPLATE" "$FIRMWARE" "$PFLASH" <<'PY'
import pathlib, sys
pathlib.Path(sys.argv[3]).write_bytes(pathlib.Path(sys.argv[1]).read_bytes() + pathlib.Path(sys.argv[2]).read_bytes())
PY
        fi
        FIRMWARE_ARGS=(-drive "if=pflash,format=raw,file=$PFLASH")
    fi

    exec qemu-system-x86_64 \
        -M pc \
        -m 2048 \
        "${FIRMWARE_ARGS[@]}" \
        -vga std \
        -drive file="$IMAGE",format=raw \
        -serial file:"$SERIAL_LOG"
else
    echo -e "${RED}Error: Unknown architecture '$ARCH' (expected aarch64 or x86_64)${NC}"
    exit 1
fi
