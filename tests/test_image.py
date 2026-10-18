"""Non-destructive image update regression tests. Created by Jonas Lee."""
import importlib.util
import os
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("image", Path(__file__).resolve().parents[1] / "tools/finalize_image.py")
image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image)


def make_disk(path, payload=b"saved-account", first=image.DATA_FIRST):
    entries = bytearray(128 * 128)
    entries[128:144] = image.DATA_TYPE
    struct.pack_into("<QQ", entries, 128 + 32, first, image.DATA_LAST)
    header = bytearray(512)
    header[:8] = b"EFI PART"
    struct.pack_into("<II", header, 8, 0x10000, 92)
    struct.pack_into("<QIII", header, 72, 2, 128, 128, zlib.crc32(entries))
    struct.pack_into("<I", header, 16, zlib.crc32(header[:92]))
    with path.open("wb") as file:
        file.truncate(image.IMAGE_BYTES)
        file.seek(512); file.write(header); file.write(entries)
        file.seek(image.DATA_FIRST * 512); file.write(payload)


def payload(path):
    with path.open("rb") as file:
        file.seek(image.DATA_FIRST * 512)
        return file.read(32)


class ImageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="image-test-", dir=os.environ.get("OJJY_TEST_DIR"))
        self.root = Path(self.temp.name)
        self.old, self.new = self.root / "disk.img", self.root / "candidate.img"
        make_disk(self.old)
        make_disk(self.new, b"fresh-empty-data")

    def tearDown(self):
        self.temp.cleanup()

    def test_update_preserves_data_and_keeps_backup(self):
        expected = payload(self.old)
        image.install(self.new, self.old)
        self.assertEqual(payload(self.old), expected)
        backups = list((self.root / "backups").glob("*.img"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(payload(backups[0]), expected)
        self.assertFalse(self.new.exists())

    def test_update_takes_wallpaper_pack_from_candidate(self):
        def write_at(path, lba, data):
            with path.open("r+b") as file:
                file.seek(lba * 512); file.write(data)
        def read_at(path, lba):
            with path.open("rb") as file:
                file.seek(lba * 512); return file.read(16)
        last_user = image.PACK_FIRST - 1
        write_at(self.old, last_user, b"user-tail-sector")
        write_at(self.old, image.PACK_FIRST, b"OJWP-old-pack...")
        write_at(self.new, image.PACK_FIRST, b"OJWP-new-pack...")
        image.install(self.new, self.old)
        self.assertEqual(read_at(self.old, last_user), b"user-tail-sector")
        self.assertEqual(read_at(self.old, image.PACK_FIRST), b"OJWP-new-pack...")

    def test_first_architecture_build_imports_legacy_without_modifying_it(self):
        destination = self.root / "arm.img"
        expected = payload(self.old)
        image.install(self.new, destination, self.old)
        self.assertEqual(payload(destination), expected)
        self.assertEqual(payload(self.old), expected)

    def test_architecture_disk_takes_precedence_over_legacy(self):
        legacy = self.root / "legacy.img"
        make_disk(legacy, b"different-account")
        expected = payload(self.old)
        image.install(self.new, self.old, legacy)
        self.assertEqual(payload(self.old), expected)

    def test_invalid_old_gpt_refuses_without_touching_saved_disk(self):
        with self.old.open("r+b") as file:
            file.seek(512); file.write(b"BROKEN!!")
        expected = self.old.stat().st_mtime_ns
        with self.assertRaises(ValueError):
            image.install(self.new, self.old)
        self.assertEqual(self.old.stat().st_mtime_ns, expected)
        self.assertTrue(self.new.exists())

    def test_unfamiliar_partition_refuses(self):
        make_disk(self.old, first=12345)
        with self.assertRaises(ValueError):
            image.install(self.new, self.old)
        self.assertTrue(self.new.exists())

    def test_bad_candidate_refuses_before_backup_or_replacement(self):
        self.new.write_bytes(b"incomplete-build")
        expected = payload(self.old)
        with self.assertRaises(ValueError):
            image.install(self.new, self.old)
        self.assertEqual(payload(self.old), expected)
        self.assertFalse((self.root / "backups").exists())

    def test_running_vm_style_posix_lock_refuses_update(self):
        child = subprocess.Popen([sys.executable, "-c",
            "import fcntl,sys; f=open(sys.argv[1],'r+b'); fcntl.lockf(f,fcntl.LOCK_EX); "
            "print('locked',flush=True); sys.stdin.readline()", str(self.old)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(child.stdout.readline().strip(), "locked")
            with self.assertRaises(BlockingIOError):
                image.install(self.new, self.old)
            self.assertTrue(self.new.exists())
        finally:
            child.communicate("release\n", timeout=5)

    @unittest.skipUnless(shutil.which("qemu-system-aarch64"), "QEMU not installed")
    def test_real_qemu_disk_lock_refuses_update(self):
        child = subprocess.Popen(["qemu-system-aarch64", "-M", "virt", "-m", "64",
            "-accel", "tcg", "-S", "-nodefaults", "-display", "none", "-qmp", "stdio",
            "-drive", f"file={self.old},format=raw,if=none,id=held",
            "-device", "virtio-blk-device,drive=held"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertIn('"QMP"', child.stdout.readline())
            with self.assertRaises(BlockingIOError):
                image.install(self.new, self.old)
            self.assertTrue(self.new.exists())
        finally:
            child.terminate()
            child.communicate(timeout=5)

    def test_new_disk_needs_no_existing_checkpoint(self):
        destination = self.root / "new.img"
        image.install(self.new, destination)
        self.assertTrue(destination.exists())
        self.assertTrue(payload(destination).startswith(b"fresh-empty-data"))


if __name__ == "__main__":
    unittest.main()
