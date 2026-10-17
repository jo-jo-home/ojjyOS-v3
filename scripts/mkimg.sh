#!/bin/bash
#
# ojjyOS v3 Disk Image Creator
#
# Creates a 128 MB GPT disk image with:
#   - Partition 1: EFI System Partition (32 MB, LBA 2048..67583) containing
#                  /EFI/BOOT/BOOTAA64.EFI (or BOOTX64.EFI) and /EFI/ojjyos/kernel.bin
#   - Partition 2: ojjyOS Data partition (LBA 67584..end) for RAMFS persistence
#
# Works natively on macOS (via python3 fallback) and on Linux (sgdisk + mtools).
# Also generates build/ojjyos3.vdi automatically if VirtualBox is installed.
#

set -e

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

BOOTLOADER="$BUILD_DIR/$EFI_NAME"
KERNEL="$BUILD_DIR/kernel.bin"
IMAGE="$BUILD_DIR/ojjyos3.img"
VDI_IMAGE="$BUILD_DIR/ojjyos3.vdi"
ESP_IMG="$BUILD_DIR/esp.img"

IMG_SIZE_MB=128
SECTORS=$((IMG_SIZE_MB * 2048))
PART_LAST=$((SECTORS - 34))
ESP_SECTORS=$((32 * 2048))
DATA_START=$((2048 + ESP_SECTORS))

echo "Creating disk image ($ARCH)..."

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

if command -v sgdisk &>/dev/null && command -v mkfs.fat &>/dev/null && command -v mcopy &>/dev/null; then
    echo "  Creating ESP filesystem (mkfs.fat + mtools)..."
    dd if=/dev/zero of="$ESP_IMG" bs=512 count=$ESP_SECTORS 2>/dev/null
    mkfs.fat -F 32 "$ESP_IMG" >/dev/null

    echo "  Copying files to ESP (/EFI/BOOT/$EFI_NAME)..."
    mmd -i "$ESP_IMG" ::/EFI
    mmd -i "$ESP_IMG" ::/EFI/BOOT
    mmd -i "$ESP_IMG" ::/EFI/ojjyos
    mcopy -i "$ESP_IMG" "$BOOTLOADER" "::/EFI/BOOT/$EFI_NAME"
    mcopy -i "$ESP_IMG" "$KERNEL" ::/EFI/ojjyos/kernel.bin

    echo "  Creating GPT disk image..."
    dd if=/dev/zero of="$IMAGE" bs=1M count=$IMG_SIZE_MB 2>/dev/null
    sgdisk -Z "$IMAGE" >/dev/null 2>&1 || true
    sgdisk -n 1:2048:$((DATA_START - 1)) -t 1:ef00 -c 1:"EFI System" "$IMAGE" >/dev/null
    sgdisk -n 2:$DATA_START:$PART_LAST -t 2:8300 -c 2:"ojjyOS Data" "$IMAGE" >/dev/null
    dd if="$ESP_IMG" of="$IMAGE" bs=512 seek=2048 conv=notrunc 2>/dev/null
    rm -f "$ESP_IMG"
else
    echo "  Creating GPT + FAT32 disk image natively via python3..."
    python3 - "$IMAGE" "$BOOTLOADER" "$EFI_NAME" "$KERNEL" <<'PYEOF'
import sys, os, struct, zlib, uuid

img_path, boot_path, efi_name, kernel_path = sys.argv[1:5]

SECTOR = 512
TOTAL_SECTORS = 128 * 2048          # 262144 sectors (128 MB)
ESP_START_LBA = 2048
ESP_SECTORS   = 32 * 2048           # 65536 sectors (32 MB)
DATA_START_LBA = ESP_START_LBA + ESP_SECTORS  # 67584 (contract with persist.h)
DATA_END_LBA   = TOTAL_SECTORS - 34

with open(boot_path, "rb") as f:
    boot_bytes = f.read()
with open(kernel_path, "rb") as f:
    kernel_bytes = f.read()

# Build 32 MB FAT32 image in memory
esp = bytearray(ESP_SECTORS * SECTOR)
RSRV = 32
NFATS = 2
SPF = 512  # 512 sectors per FAT -> 65536 clusters max (1 sector/cluster)
SPC = 1
DATA_SEC_START = RSRV + NFATS * SPF

# Boot Sector (BPB)
bpb = bytearray(SECTOR)
bpb[0:3] = b"\xEB\x58\x90"
bpb[3:11] = b"OJJYOS3 "
struct.pack_into("<H", bpb, 11, SECTOR)
bpb[13] = SPC
struct.pack_into("<H", bpb, 14, RSRV)
bpb[16] = NFATS
struct.pack_into("<H", bpb, 17, 0)
struct.pack_into("<H", bpb, 19, 0)
bpb[21] = 0xF8
struct.pack_into("<H", bpb, 22, 0)
struct.pack_into("<H", bpb, 24, 32)
struct.pack_into("<H", bpb, 26, 8)
struct.pack_into("<I", bpb, 28, ESP_START_LBA)
struct.pack_into("<I", bpb, 32, ESP_SECTORS)
struct.pack_into("<I", bpb, 36, SPF)
struct.pack_into("<H", bpb, 40, 0)
struct.pack_into("<H", bpb, 42, 0)
struct.pack_into("<I", bpb, 44, 2)      # Root cluster = 2
struct.pack_into("<H", bpb, 48, 1)      # FSInfo sector = 1
struct.pack_into("<H", bpb, 50, 6)      # Backup boot sector = 6
bpb[64] = 0x80
bpb[66] = 0x29
struct.pack_into("<I", bpb, 67, 0x4F4A4A59)
bpb[71:82] = b"OJJYOS EFI "
bpb[82:90] = b"FAT32   "
bpb[510:512] = b"\x55\xAA"
esp[0:SECTOR] = bpb
esp[6*SECTOR:7*SECTOR] = bpb

# FSInfo sector
fsinfo = bytearray(SECTOR)
struct.pack_into("<I", fsinfo, 0, 0x41615252)
struct.pack_into("<I", fsinfo, 484, 0x61417272)
struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
fsinfo[510:512] = b"\x55\xAA"
esp[SECTOR:2*SECTOR] = fsinfo

fat = [0] * (SPF * (SECTOR // 4))
fat[0] = 0x0FFFFFF8
fat[1] = 0x0FFFFFFF
next_clus = 6  # Clusters: 2=root(/), 3=/EFI, 4=/EFI/BOOT, 5=/EFI/ojjyos

def clus_offset(c):
    return (DATA_SEC_START + (c - 2) * SPC) * SECTOR

def alloc_file(data):
    global next_clus
    if len(data) == 0:
        c = next_clus
        next_clus += 1
        fat[c] = 0x0FFFFFFF
        return c
    n = (len(data) + SECTOR - 1) // SECTOR
    first = next_clus
    for i in range(n):
        c = first + i
        fat[c] = (c + 1) if (i + 1 < n) else 0x0FFFFFFF
        chunk = data[i*SECTOR:(i+1)*SECTOR]
        off = clus_offset(c)
        esp[off:off+len(chunk)] = chunk
    next_clus += n
    return first

def dirent_83(name8, ext3, attr, clus, size):
    ent = bytearray(32)
    ent[0:8] = name8.ljust(8, " ").encode("ascii")[:8]
    ent[8:11] = ext3.ljust(3, " ").encode("ascii")[:3]
    ent[11] = attr
    struct.pack_into("<H", ent, 20, (clus >> 16) & 0xFFFF)
    struct.pack_into("<H", ent, 26, clus & 0xFFFF)
    struct.pack_into("<I", ent, 28, size)
    return ent

# Reserve directory clusters 2..5
for c in (2, 3, 4, 5):
    fat[c] = 0x0FFFFFFF

boot_clus = alloc_file(boot_bytes)
kern_clus = alloc_file(kernel_bytes)

# Cluster 2: Root directory -> EFI
esp[clus_offset(2):clus_offset(2)+32] = dirent_83("EFI", "", 0x10, 3, 0)

# Cluster 3: /EFI -> ., .., BOOT, OJJYOS
off3 = clus_offset(3)
esp[off3+0:off3+32]   = dirent_83(".", "", 0x10, 3, 0)
esp[off3+32:off3+64]  = dirent_83("..", "", 0x10, 0, 0)
esp[off3+64:off3+96]  = dirent_83("BOOT", "", 0x10, 4, 0)
esp[off3+96:off3+128] = dirent_83("OJJYOS", "", 0x10, 5, 0)

# Cluster 4: /EFI/BOOT -> ., .., BOOTAA64.EFI or BOOTX64.EFI
bbase, bext = efi_name.split(".")
off4 = clus_offset(4)
esp[off4+0:off4+32]  = dirent_83(".", "", 0x10, 4, 0)
esp[off4+32:off4+64] = dirent_83("..", "", 0x10, 3, 0)
esp[off4+64:off4+96] = dirent_83(bbase.upper(), bext.upper(), 0x20, boot_clus, len(boot_bytes))

# Cluster 5: /EFI/OJJYOS -> ., .., KERNEL.BIN
off5 = clus_offset(5)
esp[off5+0:off5+32]  = dirent_83(".", "", 0x10, 5, 0)
esp[off5+32:off5+64] = dirent_83("..", "", 0x10, 3, 0)
esp[off5+64:off5+96] = dirent_83("KERNEL", "BIN", 0x20, kern_clus, len(kernel_bytes))

# Write both FAT copies
fat_bytes = struct.pack(f"<{len(fat)}I", *fat)
for fidx in range(NFATS):
    foff = (RSRV + fidx * SPF) * SECTOR
    esp[foff:foff+len(fat_bytes)] = fat_bytes

# Helper for GPT GUIDs (mixed-endian) and headers
def guid_bytes(u):
    return uuid.UUID(u).bytes_le

ESP_TYPE_GUID  = guid_bytes("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
DATA_TYPE_GUID = guid_bytes("0FC63DAF-8483-4772-8E79-3D69D8477DE4")
DISK_GUID      = guid_bytes("4F4A4A59-0003-4000-8000-000000000001")
PART1_GUID     = guid_bytes("4F4A4A59-0003-4000-8000-000000000002")
PART2_GUID     = guid_bytes("4F4A4A59-0003-4000-8000-000000000003")

entries = bytearray(128 * 128)
def put_part(idx, type_guid, u_guid, first_lba, last_lba, name):
    ent = bytearray(128)
    ent[0:16] = type_guid
    ent[16:32] = u_guid
    struct.pack_into("<QQQ", ent, 32, first_lba, last_lba, 0)
    uname = name.encode("utf-16le")[:72]
    ent[56:56+len(uname)] = uname
    entries[idx*128:(idx+1)*128] = ent

put_part(0, ESP_TYPE_GUID,  PART1_GUID, ESP_START_LBA, DATA_START_LBA - 1, "EFI System")
put_part(1, DATA_TYPE_GUID, PART2_GUID, DATA_START_LBA, DATA_END_LBA, "ojjyOS Data")
entries_crc = zlib.crc32(entries) & 0xFFFFFFFF

def make_gpt_hdr(my_lba, alt_lba, entries_lba):
    hdr = bytearray(SECTOR)
    hdr[0:8] = b"EFI PART"
    struct.pack_into("<II", hdr, 8, 0x00010000, 92)
    struct.pack_into("<I", hdr, 16, 0)
    struct.pack_into("<QQQQ", hdr, 24, my_lba, alt_lba, 34, DATA_END_LBA)
    hdr[56:72] = DISK_GUID
    struct.pack_into("<QIII", hdr, 72, entries_lba, 128, 128, entries_crc)
    crc = zlib.crc32(hdr[:92]) & 0xFFFFFFFF
    struct.pack_into("<I", hdr, 16, crc)
    return hdr

# Protective MBR
mbr = bytearray(SECTOR)
mbr[446+0] = 0x00
mbr[446+1:446+4] = b"\x00\x02\x00"
mbr[446+4] = 0xEE
mbr[446+5:446+8] = b"\xFF\xFF\xFF"
struct.pack_into("<II", mbr, 446+8, 1, TOTAL_SECTORS - 1)
mbr[510:512] = b"\x55\xAA"

with open(img_path, "wb") as f:
    f.truncate(TOTAL_SECTORS * SECTOR)
    f.seek(0)
    f.write(mbr)
    f.write(make_gpt_hdr(1, TOTAL_SECTORS - 1, 2))
    f.write(entries)
    f.seek(ESP_START_LBA * SECTOR)
    f.write(esp)
    f.seek((TOTAL_SECTORS - 33) * SECTOR)
    f.write(entries)
    f.write(make_gpt_hdr(TOTAL_SECTORS - 1, 1, TOTAL_SECTORS - 33))
PYEOF
fi

# Also convert to VirtualBox .vdi automatically if VBoxManage is available
VBOX_MGR="$(command -v VBoxManage 2>/dev/null || true)"
if [ -z "$VBOX_MGR" ] && [ -x "/Applications/VirtualBox.app/Contents/MacOS/VBoxManage" ]; then
    VBOX_MGR="/Applications/VirtualBox.app/Contents/MacOS/VBoxManage"
fi
if [ -n "$VBOX_MGR" ]; then
    rm -f "$VDI_IMAGE"
    "$VBOX_MGR" convertfromraw "$IMAGE" "$VDI_IMAGE" --format VDI >/dev/null 2>&1 || true
    if [ -f "$VDI_IMAGE" ]; then
        echo "  VirtualBox disk created: $VDI_IMAGE"
    fi
fi

echo "  Disk image created: $IMAGE"
ls -lh "$IMAGE"
