#!/usr/bin/env python3
"""Install a rebuilt OS image without discarding ojjyOS user data.

Created by Jonas Lee. Both images must have the expected, checksummed GPT
layout. Existing disks are locked, backed up, and only replaced atomically
after the user-data part of their data partition (everything before the
desktop picture pack) has been copied into the candidate.
"""

import argparse
import fcntl
import os
from pathlib import Path
import shutil
import struct
import tempfile
import uuid
import zlib

SECTOR = 512
IMAGE_BYTES = 128 * 1024 * 1024
DATA_FIRST = 67584
DATA_LAST = IMAGE_BYTES // SECTOR - 34
# The data partition's tail holds the read-only desktop picture pack
# (kernel/src/image/wallpack.h). It belongs to the build, so it comes from
# the candidate; only the sectors before it are user data and preserved.
PACK_FIRST = 102400
DATA_TYPE = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4").bytes_le


def data_partition(file):
    """Fail closed on truncated, corrupt, or unfamiliar disk layouts."""
    if os.fstat(file.fileno()).st_size != IMAGE_BYTES:
        raise ValueError("unexpected disk size; refusing to discard or guess data")
    file.seek(SECTOR)
    header = bytearray(file.read(SECTOR))
    size = struct.unpack_from("<I", header, 12)[0]
    if header[:8] != b"EFI PART" or not 92 <= size <= SECTOR:
        raise ValueError("invalid GPT header")
    expected_crc = struct.unpack_from("<I", header, 16)[0]
    struct.pack_into("<I", header, 16, 0)
    if zlib.crc32(header[:size]) != expected_crc:
        raise ValueError("GPT header checksum mismatch")
    entries_lba, count, entry_size, expected_crc = struct.unpack_from("<QIII", header, 72)
    if entries_lba != 2 or count != 128 or entry_size != 128:
        raise ValueError("unrecognized GPT entry layout")
    file.seek(entries_lba * SECTOR)
    entries = file.read(count * entry_size)
    if zlib.crc32(entries) != expected_crc:
        raise ValueError("GPT partition checksum mismatch")
    entry = entries[entry_size:2 * entry_size]
    first, last = struct.unpack_from("<QQ", entry, 32)
    if entry[:16] != DATA_TYPE or (first, last) != (DATA_FIRST, DATA_LAST):
        raise ValueError("unexpected data partition; refusing destructive migration")
    return first * SECTOR, (PACK_FIRST - first) * SECTOR


def _install_locked(candidate, destination, legacy=None):
    candidate, destination = Path(candidate), Path(destination)
    source = destination if destination.exists() else Path(legacy) if legacy else None
    if source is not None and not source.exists():
        source = None
    if candidate.resolve() == destination.resolve():
        raise ValueError("candidate must not be the saved disk")
    if candidate.is_symlink() or destination.is_symlink() or (source is not None and source.is_symlink()):
        raise ValueError("refusing to replace or migrate symlinked disk images")
    with candidate.open("r+b") as new:
        offset, length = data_partition(new)
        if source is not None:
            with source.open("r+b") as old:
                # QEMU's POSIX byte-range locks conflict with this whole-file
                # lock. Do not rebuild a disk that is attached to a running VM.
                fcntl.lockf(old, fcntl.LOCK_EX | fcntl.LOCK_NB)
                old_offset, old_length = data_partition(old)
                if old_length != length:
                    raise ValueError("data partition size changed")
                backups = destination.parent / "backups"
                backups.mkdir(parents=True, exist_ok=True)
                fd, backup = tempfile.mkstemp(prefix="ojjyos3-", suffix=".img", dir=backups)
                with os.fdopen(fd, "wb") as saved:
                    old.seek(0)
                    shutil.copyfileobj(old, saved)
                    saved.flush()
                    os.fsync(saved.fileno())
                print(f"  Saved disk backup: {backup}")
                old.seek(old_offset)
                new.seek(offset)
                remaining = length
                while remaining:
                    chunk = old.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise ValueError("source disk was truncated during migration")
                    new.write(chunk)
                    remaining -= len(chunk)
                new.flush()
                os.fsync(new.fileno())
                os.replace(candidate, destination)
                print(f"  Preserved user data from: {source}")
        else:
            new.flush()
            os.fsync(new.fileno())
            os.replace(candidate, destination)
    directory = os.open(destination.parent, os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def install(candidate, destination, legacy=None):
    destination = Path(destination)
    # Serialize two builders even when the destination does not exist yet.
    with (destination.parent / ".image-update.lock").open("a+b") as lock:
        fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        _install_locked(candidate, destination, legacy)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate")
    parser.add_argument("destination")
    parser.add_argument("--legacy")
    args = parser.parse_args()
    try:
        install(args.candidate, args.destination, args.legacy)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Disk update refused; saved disk unchanged: {error}\n")


if __name__ == "__main__":
    main()
