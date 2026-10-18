#!/usr/bin/env python3
"""esp_read.py - print a file from the root of a disk image's EFI System
Partition (FAT12/16/32, 8.3 names), e.g. the bootloader's OJJYBOOT.LOG.
Read-only. Usage: esp_read.py disk.img OJJYBOOT.LOG
ojjyOS - Created by Jonas Lee
"""
import struct
import sys


def main(img, name):
    with open(img, 'rb') as f:
        f.seek(512); gpt = f.read(92)
        entries_lba, count, esize = struct.unpack_from('<QII', gpt, 72)
        f.seek(entries_lba * 512)
        start = None
        for _ in range(count):
            e = f.read(esize)
            if e[:16] == bytes.fromhex('28732ac11ff8d211ba4b00a0c93ec93b'):
                start = struct.unpack_from('<Q', e, 32)[0]; break
        if start is None: sys.exit('no EFI System Partition')
        base = start * 512
        f.seek(base); bs = f.read(512)
        bps, spc, rsv, nfats, rootents, tot16, _, fatsz16 = struct.unpack_from('<HBHBHHBH', bs, 11)
        fatsz = fatsz16 or struct.unpack_from('<I', bs, 36)[0]
        tot = tot16 or struct.unpack_from('<I', bs, 32)[0]
        root_secs = (rootents * 32 + bps - 1) // bps
        first_data = rsv + nfats * fatsz + root_secs
        clusters = (tot - first_data) // spc
        fat32 = clusters >= 65525
        f.seek(base + rsv * bps); fat = f.read(fatsz * bps)

        def chain(c):
            while c >= 2 and c < (0x0FFFFFF8 if fat32 else 0xFFF8):
                yield c
                c = (struct.unpack_from('<I', fat, c * 4)[0] & 0x0FFFFFFF) if fat32 else struct.unpack_from('<H', fat, c * 2)[0]

        def read_cluster(c):
            f.seek(base + (first_data + (c - 2) * spc) * bps); return f.read(spc * bps)

        if fat32:
            root = b''.join(read_cluster(c) for c in chain(struct.unpack_from('<I', bs, 44)[0]))
        else:
            f.seek(base + (rsv + nfats * fatsz) * bps); root = f.read(root_secs * bps)
        want = name.upper()
        for off in range(0, len(root), 32):
            d = root[off:off + 32]
            if d[0] == 0: break
            if d[0] == 0xE5 or d[11] == 0x0F: continue
            n = d[:8].decode('latin1').strip() + ('.' + d[8:11].decode('latin1').strip() if d[8:11].strip() else '')
            if n == want:
                hi, lo, size = struct.unpack_from('<H', d, 20)[0], struct.unpack_from('<H', d, 26)[0], struct.unpack_from('<I', d, 28)[0]
                data = b''.join(read_cluster(c) for c in chain(hi << 16 | lo))[:size]
                sys.stdout.write(data.decode('latin1')); return
        sys.exit(f'{name} not found')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
