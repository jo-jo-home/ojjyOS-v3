#!/usr/bin/env python3
"""Desktop picture pack builder for ojjyOS. Created by Jonas Lee.

The official macOS Tahoe wallpapers are Apple's images. They are downloaded
for personal use into wallpapers/local/ (git-ignored) and only ever baked
into the local disk image: never commit or publish them.

  python3 tools/import_wallpapers.py --fetch        # download the originals
  python3 tools/import_wallpapers.py --pack OUT     # build the pack file

The pack lives at a fixed LBA in the tail of the data partition (see
kernel/src/image/wallpack.h). Every picture is stored as a baseline 4:4:4
JPEG at 2880x1800 plus a 320x200 thumbnail, because the kernel decoder
only handles baseline JPEG and upsamples chroma by repetition.

Layout (little endian, 512-byte sectors, offsets relative to the pack):
  header  16 bytes: "OJWP", version 1, count, crc32 of the entry table
  entries count x 128 bytes from byte 16:
          name[64], kind u32 (0 any, 1 light, 2 dark), width, height,
          image_sector, image_bytes, image_crc, thumb_sector, thumb_bytes,
          thumb_crc, reserved[6] u32
  data    each JPEG starts on a sector boundary after the 16 KiB header.
"""

import argparse
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / "wallpapers" / "local"
ORIGINALS = LOCAL / "originals"
CACHE = LOCAL / "cache"

SECTOR = 512
HEADER_BYTES = 16 * 1024
ENTRY_BYTES = 128
MAX_ENTRIES = (HEADER_BYTES - 16) // ENTRY_BYTES
WIDTH, HEIGHT = 2880, 1800
THUMB_W, THUMB_H = 320, 200
QUALITY, THUMB_QUALITY = 88, 85
KIND = {"any": 0, "light": 1, "dark": 2}

PIX = "https://media.512pixels.net/downloads/macos-wallpapers-6k/"
BAG = "https://basicappleguy.com/s/"
# (display name, kind, url). Order is the order shown in Settings.
CATALOG = [
    ("Tahoe Light", "light", PIX + "26-Tahoe-Light-6K.png"),
    ("Tahoe Dark", "dark", PIX + "26-Tahoe-Dark-6K.png"),
    ("Tahoe Beach Dawn", "light", PIX + "26-Tahoe-Beach-Dawn.png"),
    ("Tahoe Beach Day", "light", PIX + "26-Tahoe-Beach-Day.png"),
    ("Tahoe Beach Dusk", "dark", PIX + "26-Tahoe-Beach-Dusk.png"),
    ("Tahoe Beach Night", "dark", PIX + "26-Tahoe-Beach-Night.png"),
    ("Goa Coast", "any", BAG + "Goa_A_1_Mac.heic"),
    ("Goa Shore", "any", BAG + "Goa_B_1_Mac.heic"),
    ("Ganges", "any", BAG + "Ganges_1_Mac.heic"),
    ("Ghansali", "any", BAG + "Ghansali_1_Mac.heic"),
    ("Himalaya Day", "any", BAG + "Him_Day_1_Mac.heic"),
    ("Himalaya Peaks", "any", BAG + "Him_Peaks_1_Mac.heic"),
    ("Through the Himalaya", "any", BAG + "Through_Him_1_Mac.heic"),
    ("Reservoir", "any", BAG + "Reservoir_1_Mac.heic"),
    ("Tea Above", "any", BAG + "Tea_Above_1_Mac.heic"),
    ("Tea Day", "any", BAG + "Tea_Day_1_Mac.heic"),
    ("Tea Mist", "any", BAG + "Tea_Mist_1_Mac.heic"),
]


def original_path(url):
    return ORIGINALS / url.rsplit("/", 1)[1]


def fetch():
    ORIGINALS.mkdir(parents=True, exist_ok=True)
    for name, _kind, url in CATALOG:
        dest = original_path(url)
        if dest.exists() and dest.stat().st_size > 0:
            print(f"  have {dest.name}")
            continue
        print(f"  fetching {name}: {url}")
        part = dest.with_suffix(dest.suffix + ".part")
        # curl uses the system trust store (python.org builds often lack one).
        done = subprocess.run(["curl", "-fsSL", "--retry", "2", "-A", "Mozilla/5.0",
                               "-o", str(part), url])
        if done.returncode == 0:
            part.replace(dest)
        else:
            part.unlink(missing_ok=True)
            print(f"  failed {name}: curl exit {done.returncode}", file=sys.stderr)


def open_image(path):
    from PIL import Image
    if path.suffix.lower() == ".heic":
        # Pillow cannot read HEIC; macOS sips can.
        with tempfile.TemporaryDirectory() as tmp:
            png = Path(tmp) / "x.png"
            subprocess.run(["sips", "-s", "format", "png", str(path), "--out", str(png)],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            with Image.open(png) as img:
                return img.convert("RGB")
    with Image.open(path) as img:
        return img.convert("RGB")


def cover(img, w, h):
    """Scale to fill w x h, cropping the excess around the centre."""
    from PIL import Image
    scale = max(w / img.width, h / img.height)
    sw, sh = max(w, round(img.width * scale)), max(h, round(img.height * scale))
    img = img.resize((sw, sh), Image.LANCZOS)
    left, top = (sw - w) // 2, (sh - h) // 2
    return img.crop((left, top, left + w, top + h))


def baseline_jpeg(img, quality):
    out = io.BytesIO()
    img.save(out, "JPEG", quality=quality, subsampling=0, progressive=False, optimize=True)
    return out.getvalue()


def converted(name, url):
    """(image, thumb) JPEG bytes, cached next to the originals."""
    src = original_path(url)
    key = f"{src.stem}-{WIDTH}x{HEIGHT}-q{QUALITY}"
    big, small = CACHE / f"{key}.jpg", CACHE / f"{key}-thumb.jpg"
    if big.exists() and small.exists() and big.stat().st_mtime >= src.stat().st_mtime:
        return big.read_bytes(), small.read_bytes()
    print(f"  converting {name}")
    img = cover(open_image(src), WIDTH, HEIGHT)
    data = baseline_jpeg(img, QUALITY)
    thumb = baseline_jpeg(cover(img, THUMB_W, THUMB_H), THUMB_QUALITY)
    CACHE.mkdir(parents=True, exist_ok=True)
    big.write_bytes(data)
    small.write_bytes(thumb)
    return data, thumb


def build_pack(items):
    """items: list of (name, kind, image_bytes, thumb_bytes) -> pack bytes."""
    if len(items) > MAX_ENTRIES:
        raise ValueError("too many wallpapers")
    table = bytearray()
    blob = bytearray()

    def place(data):
        sector = (HEADER_BYTES + len(blob)) // SECTOR
        blob.extend(data)
        blob.extend(b"\0" * (-len(blob) % SECTOR))
        return sector

    for name, kind, data, thumb in items:
        raw = name.encode("utf-8")[:63]
        img_sector = place(data)
        thumb_sector = place(thumb)
        entry = bytearray(ENTRY_BYTES)
        entry[:len(raw)] = raw
        struct.pack_into("<10I", entry, 64, KIND[kind], WIDTH, HEIGHT,
                         img_sector, len(data), zlib.crc32(data),
                         thumb_sector, len(thumb), zlib.crc32(thumb), 0)
        table.extend(entry)
    header = bytearray(HEADER_BYTES)
    struct.pack_into("<4sIII", header, 0, b"OJWP", 1, len(items), zlib.crc32(table))
    header[16:16 + len(table)] = table
    return bytes(header + blob)


def pack(out, limit):
    items = []
    for name, kind, url in CATALOG:
        if original_path(url).exists():
            items.append((name, kind) + converted(name, url))
    data = build_pack(items)
    if len(data) > limit:
        raise SystemExit(f"wallpaper pack is {len(data)} bytes; the disk region holds {limit}")
    Path(out).write_bytes(data)
    print(f"  wallpaper pack: {len(items)} pictures, {len(data) // 1024} KiB")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fetch", action="store_true", help="download the originals into wallpapers/local/")
    parser.add_argument("--pack", metavar="OUT", help="write the pack file")
    parser.add_argument("--limit", type=int, default=1 << 62, help="maximum pack size in bytes")
    args = parser.parse_args()
    if not args.fetch and not args.pack:
        parser.error("nothing to do")
    if args.fetch:
        fetch()
    if args.pack:
        pack(args.pack, args.limit)


if __name__ == "__main__":
    main()
