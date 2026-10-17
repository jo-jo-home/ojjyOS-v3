#!/bin/bash
#
# ojjyOS v3 VirtualBox Runner
#
# Creates (if needed) and launches a VirtualBox VM to run ojjyOS.
#

set -e

# Configuration
VM_NAME="ojjyOS-v3"
VM_RAM=2048           # MB
VM_CPUS=2
VM_VRAM=128           # MB

# Directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
IMAGE="$BUILD_DIR/ojjyos3.img"

# Serial log location
SERIAL_LOG="/tmp/ojjyos-serial.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  ojjyOS v3 VirtualBox Runner${NC}"
echo -e "${BLUE}========================================${NC}"
echo

# Check if disk image exists
if [ ! -f "$IMAGE" ]; then
    echo -e "${RED}Error: Disk image not found: $IMAGE${NC}"
    echo "Run 'bash scripts/build.sh' first."
    exit 1
fi

# Check if VirtualBox is installed
if ! command -v VBoxManage &> /dev/null; then
    echo -e "${RED}Error: VirtualBox not installed${NC}"
    echo "Install VirtualBox first."
    exit 1
fi

# Convert raw image to VDI format (VirtualBox native)
VDI_IMAGE="$BUILD_DIR/ojjyos3.vdi"
echo -e "${YELLOW}Converting disk image to VDI format...${NC}"
rm -f "$VDI_IMAGE"
VBoxManage convertfromraw "$IMAGE" "$VDI_IMAGE" --format VDI 2>/dev/null || {
    echo -e "${YELLOW}Falling back to raw image...${NC}"
    VDI_IMAGE="$IMAGE"
}

# Check if VM already exists
if VBoxManage showvminfo "$VM_NAME" &>/dev/null; then
    echo -e "${YELLOW}VM '$VM_NAME' already exists.${NC}"

    # Check if running
    if VBoxManage list runningvms | grep -q "\"$VM_NAME\""; then
        echo -e "${YELLOW}VM is running. Stopping...${NC}"
        VBoxManage controlvm "$VM_NAME" poweroff 2>/dev/null || true
        sleep 2
    fi

    # Ensure IDE controller exists (ATA driver expects PIIX4 IDE)
    VBoxManage storagectl "$VM_NAME" --name "IDE" --add ide --controller PIIX4 2>/dev/null || true

    # Detach and remove old disk
    echo "Updating disk attachment..."
    VBoxManage storageattach "$VM_NAME" --storagectl "IDE" --port 0 --device 0 --medium none 2>/dev/null || true

    # Attach new disk
    VBoxManage storageattach "$VM_NAME" --storagectl "IDE" --port 0 --device 0 --type hdd --medium "$VDI_IMAGE"
else
    echo -e "${YELLOW}Creating new VM '$VM_NAME'...${NC}"

    # Create VM
    VBoxManage createvm --name "$VM_NAME" --ostype "Other_64" --register

    # Configure VM
    VBoxManage modifyvm "$VM_NAME" \
        --memory $VM_RAM \
        --cpus $VM_CPUS \
        --vram $VM_VRAM \
        --firmware efi \
        --graphicscontroller vmsvga \
        --mouse ps2 \
        --keyboard ps2 \
        --audio-enabled off \
        --usb off \
        --uart1 0x3F8 4 \
        --uartmode1 file "$SERIAL_LOG"

    # Add IDE controller (PIIX4) for ATA driver compatibility
    VBoxManage storagectl "$VM_NAME" --name "IDE" --add ide --controller PIIX4 --portcount 2

    # Attach disk
    VBoxManage storageattach "$VM_NAME" --storagectl "IDE" --port 0 --device 0 --type hdd --medium "$VDI_IMAGE"

    echo -e "${GREEN}VM created successfully.${NC}"
fi

# Clear serial log
> "$SERIAL_LOG"

# Start VM
echo
echo -e "${GREEN}Starting VM...${NC}"
echo -e "Serial output will be logged to: ${BLUE}$SERIAL_LOG${NC}"
echo -e "To view serial log in real-time: ${YELLOW}tail -f $SERIAL_LOG${NC}"
echo

VBoxManage startvm "$VM_NAME"

echo
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}  VM Started!${NC}"
echo -e "${BLUE}========================================${NC}"
echo
echo "Useful commands:"
echo "  View serial log:  tail -f $SERIAL_LOG"
echo "  Stop VM:          VBoxManage controlvm $VM_NAME poweroff"
echo "  Delete VM:        VBoxManage unregistervm $VM_NAME --delete"
echo
