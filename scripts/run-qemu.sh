#!/bin/bash
#
# ojjyOS v3 QEMU Runner (Apple Silicon / aarch64 & x86_64)
#
# Runs an ARM64 guest on Apple Silicon Macs via qemu-system-aarch64
# with one vCPU (Apple HVF when available, else TCG), virtio-gpu display,
# PL011 UART, PL031 RTC, and virtio-mmio keyboard/tablet/block devices.
#
# Usage:
#   bash scripts/run-qemu.sh [aarch64|x86_64]
#
# ARM display size: OJJY_RES=WIDTHxHEIGHT (default 2880x1800 with HVF = a
# 1440x900 layout at Retina 2x, or 1440x900 under emulation; 16:10 = the
# full-screen area of a notched MacBook). Use 3200x1800 (or 1600x900) on 16:9.
# OJJY_DISPLAY=ramfb restores the old 4:3-only firmware display.
# OJJY_FULLSCREEN=1 starts in macOS full screen (Zoom To Fit is always on).
# OJJY_ACCEL=hvf|tcg picks the accelerator (default: HVF when available).
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

    # Apple Silicon: hardware virtualization (HVF) runs the kernel on the
    # real CPU, ~4-20x faster than emulation. HVF needs GICv3 (it cannot
    # emulate GICv2) and every MMIO access must be a plain LDR/STR (see
    # kernel/src/arch/aarch64/mmio.h). OJJY_ACCEL=tcg forces emulation.
    # The kernel is single-CPU (secondary cores are parked in entry.S).
    ACCEL="${OJJY_ACCEL:-auto}"
    if [ "$ACCEL" = "auto" ]; then
        ACCEL="tcg"
        if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ] &&
           [ "$(sysctl -n kern.hv_support 2>/dev/null)" = "1" ]; then
            ACCEL="hvf"
        fi
    fi
    if [ "$ACCEL" = "hvf" ]; then
        ACCEL_ARGS=("-accel" "hvf" "-cpu" "host" "-smp" "1")
        MACHINE="virt,gic-version=3"
        ACCEL_DESC="HVF (Apple hardware virtualization, 1 vCPU)"
    else
        ACCEL_ARGS=("-accel" "tcg,thread=multi" "-cpu" "cortex-a72" "-smp" "1")
        MACHINE="virt,gic-version=2"
        ACCEL_DESC="MTTCG (ARM64 JIT emulation, 1 vCPU)"
    fi

    # virtio-gpu takes any screen size, so the guest can match the Mac's
    # screen shape; ramfb firmware only offers 4:3 modes (black bars).
    # HVF is fast enough for a Retina-density 2x UI (2880x1800 = 1440x900
    # layout at double resolution); emulation stays at 1x.
    if [ "$ACCEL" = "hvf" ]; then DEFAULT_RES="2880x1800"; else DEFAULT_RES="1440x900"; fi
    RES="${OJJY_RES:-$DEFAULT_RES}"
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
    echo    "  - Guest is $RES; on a 16:9 monitor use OJJY_RES=3200x1800 (or 1600x900)"
    echo    "  - Release a captured mouse/keyboard: Ctrl-Option-G"
    echo

    exec qemu-system-aarch64 \
        -M "$MACHINE" \
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
