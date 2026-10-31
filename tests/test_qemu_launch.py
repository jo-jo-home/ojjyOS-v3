#!/usr/bin/env python3
"""Private-file shared launch contract and QEMU argv tests. Created by Jonas Lee."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("qemu_launch", ROOT / "scripts/qemu_launch.py")
launch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launch)
HOST = 16 << 30
PROBE = os.environ.get("OJJY_LAUNCH_PROBE")


class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.file = self.root / "launch-settings.json"
        self.warnings = []

    def read(self, host=HOST):
        return launch.settings(self.file, host, self.warnings.append)

    def resolve(self, env=None, arch="aarch64", system="Darwin", machine="arm64", hv=True):
        return launch.resolve(self.file, HOST, env or {}, arch, system, machine, hv, self.warnings.append)

    def probe(self, operation="load", value=None, revision=None, success=True):
        args = [PROBE, operation, str(self.file), str(HOST)]
        if operation == "save":
            args += [json.dumps(value), revision]
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stderr)
        return json.loads(result.stdout) if success else None

    def test_defaults_partial_types_bounds(self):
        self.assertEqual(self.read(), launch.DEFAULTS)
        for raw in (b"", b"null", b"[]", b"{bad", b'{"version":2}', b" " * 65537):
            self.file.write_bytes(raw)
            self.assertEqual(self.read(), launch.DEFAULTS)
            self.assertEqual(self.file.read_bytes(), raw)
        self.file.write_text('{"memoryMB":true,"cpus":8,"network":1,"screen":99}')
        self.assertEqual(self.read(), launch.DEFAULTS)
        self.file.write_text('{"memoryMB":999999,"screen":"injection; touch /tmp/no"}')
        self.assertEqual(self.read()["memoryMB"], 8192)
        self.assertEqual(self.read()["screen"], "1440x900")
        self.assertEqual(self.read(8 << 30)["memoryMB"], 4096)
        with self.assertRaises(ValueError):
            self.read(2 << 30)
        self.file.write_text('{"memoryMB":2560}')
        self.assertEqual(self.read()["memoryMB"], 2048)

    def test_override_precedence_and_units(self):
        raw = '{"memoryMB":3072,"screen":"1280x800","network":false,"fullScreen":true,"future":{"x":1}}'
        self.file.write_text(raw)
        value, accel, _, width, height = self.resolve()
        self.assertEqual((value["memoryMB"], value["network"], value["fullScreen"]), (3072, False, True))
        self.assertEqual((accel, width, height), ("hvf", 2560, 1600))
        value, accel, _, width, height = self.resolve({"OJJY_ACCEL":"tcg", "OJJY_MEMORY_MB":"4096",
            "OJJY_SCREEN":"1600x900", "OJJY_NETWORK":"1", "OJJY_FULLSCREEN":"0"})
        self.assertEqual((accel, width, height), ("tcg", 1600, 900))
        self.assertEqual((value["memoryMB"], value["network"], value["fullScreen"]), (4096, True, False))
        self.assertEqual(self.resolve({"OJJY_RES":"3200x1800"})[-2:], (3200, 1800))
        self.assertEqual(self.resolve({"OJJY_RES":"3200x1800", "OJJY_DISPLAY":"ramfb"})[-2:], (1024, 768))
        self.assertEqual(self.resolve(system="Linux", machine="aarch64", hv=False)[1], "tcg")
        self.assertEqual(self.file.read_text(), raw)

    def test_invalid_overrides_fail(self):
        for key, values in {"OJJY_MEMORY_MB":["0", "99999", "2048;echo pwn", "2G"],
                            "OJJY_SCREEN":["1x2", "$(touch pwn)"], "OJJY_NETWORK":["true", ""],
                            "OJJY_FULLSCREEN":["yes"], "OJJY_ACCEL":["invalid"],
                            "OJJY_DISPLAY":["sdl"], "OJJY_RES":["9999x9999", "2x2", "1600x900,foo=1"]}.items():
            for raw in values:
                with self.subTest(key=key, value=raw), self.assertRaises(ValueError):
                    self.resolve({key:raw})
        with self.assertRaises(ValueError):
            self.resolve({"OJJY_ACCEL":"hvf"}, system="Linux", machine="aarch64", hv=False)
        with self.assertRaises(ValueError):
            self.resolve({"OJJY_RES":"3200x1800"}, arch="x86_64")

    def make_command(self, arch="aarch64", **extra):
        build = self.root / "build"
        (build / arch).mkdir(parents=True, exist_ok=True)
        image = build / arch / "private, disk.img"
        image.write_bytes(b"private disk sentinel")
        code = self.root / "firmware.fd"
        code.write_bytes(b"code")
        (self.root / "edk2-i386-vars.fd").write_bytes(b"vars")
        env = dict(OJJY_BUILD_DIR=str(build), OJJY_IMAGE=str(image), OJJY_LAUNCH_SETTINGS=str(self.file),
                   OJJY_FIRMWARE=str(code), OJJY_ACCEL="tcg", **extra)
        with patch.object(launch, "host_memory", return_value=HOST), patch.object(launch.shutil, "which", return_value="qemu"), \
             patch.object(launch.platform, "system", return_value="Darwin"), patch.object(launch.platform, "machine", return_value="x86_64"):
            result = launch.command(ROOT, arch, env, True, self.warnings.append)
        self.assertEqual(image.read_bytes(), b"private disk sentinel")
        return result, build

    def test_argv_dry_run_network_screen_and_firmware(self):
        self.file.write_text('{"memoryMB":4096,"screen":"1600x900","network":false,"fullScreen":true}')
        result, build = self.make_command()
        argv = result["argv"]
        self.assertEqual(argv[argv.index("-m") + 1], "4096")
        self.assertIn("virtio-gpu-device,xres=1600,yres=900", argv)
        self.assertNotIn("-netdev", argv)
        self.assertIn("none", argv)
        self.assertIn("full-screen=on", argv[argv.index("-display") + 1])
        self.assertTrue(any("private,, disk.img" in value for value in argv))
        self.assertFalse((build / "aarch64/serial.log").exists())
        result, build = self.make_command(arch="x86_64")
        self.assertFalse(result["settings"]["network"])
        self.assertIn("VGA,xres=1600,yres=900", result["argv"])
        self.assertIn("name=opt/ojjyOS/display,string=1600x900", result["argv"])
        self.assertFalse((build / "x86_64/firmware.fd").exists())
        self.assertEqual(result["argv"][result["argv"].index("-smp")+1], "1")
        with self.assertRaises(ValueError):
            self.resolve({"OJJY_ACCEL":"hvf"}, arch="x86_64")

    def test_aliases_rejected(self):
        with self.assertRaises(ValueError):
            self.make_command(OJJY_SERIAL_LOG=str(self.file))

    @unittest.skipUnless(PROBE, "Swift probe supplied by scripts/test-vmapp.sh")
    def test_swift_python_decode_parity(self):
        values = [{}, {"unknown":{"x":[1, True]}}, {"memoryMB":True,"network":1},
                  {"memoryMB":-9223372036854775808}, {"memoryMB":9223372036854775807},
                  {"memoryMB":9223372036854775808}, {"memoryMB":2560},
                  {"version":2,"memoryMB":4096}, {"version":True,"memoryMB":4096},
                  {"version":1.0,"memoryMB":4096.0},
                  {"memoryMB":4096.5,"cpus":8,"screen":None},
                  {"fullScreen":True,"showAtStart":False,"screen":"1280x800"}]
        randomizer = random.Random(14)
        for _ in range(40):
            values.append({key:randomizer.choice([None, True, False, 2048, 3072, 999999, "1280x800", "bad", []])
                           for key in ("memoryMB", "cpus", "screen", "network", "fullScreen", "showAtStart")})
        for value in values:
            with self.subTest(value=value):
                self.file.write_text(json.dumps(value))
                self.assertEqual(self.probe()["settings"], self.read())
        for raw in (b"null", b"[]", b"{bad", b'{"future":NaN,"network":false}', b" " * 65537):
            self.file.write_bytes(raw)
            self.assertEqual(self.probe()["settings"], self.read())

    @unittest.skipUnless(PROBE, "Swift probe supplied by scripts/test-vmapp.sh")
    def test_swift_atomic_cross_tool_round_trip(self):
        self.file.write_text('{"future":{"nested":[1,true]},"memoryMB":3072,"screen":"1600x900","network":false}')
        loaded = self.probe()
        self.assertEqual(loaded["settings"], self.read())
        self.assertEqual(loaded["revision"], hashlib.sha256(self.file.read_bytes()).hexdigest())
        changed = dict(loaded["settings"], memoryMB=4096, fullScreen=True)
        saved = self.probe("save", changed, loaded["revision"])
        self.assertEqual(saved["settings"], self.read())
        self.assertEqual(json.loads(self.file.read_text())["future"], {"nested":[1, True]})
        before = self.file.read_bytes()
        self.probe("save", changed, loaded["revision"], success=False)
        self.assertEqual(before, self.file.read_bytes())
        self.assertEqual(self.resolve()[0]["memoryMB"], 4096)


if __name__ == "__main__":
    unittest.main()
