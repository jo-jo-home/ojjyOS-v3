#!/usr/bin/env python3
"""Exercise actual shared-settings runner argv on private disks. Created by Jonas Lee."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("qemu_launch", ROOT / "scripts/qemu_launch.py")
launch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launch)


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda:source.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = Path(args.image).resolve()
    before = digest(source)
    build = output / "build"
    (build / args.arch).mkdir(parents=True)
    disk = build / args.arch / "disk.img"
    shutil.copyfile(source, disk)
    config = build / "launch-settings.json"
    config.write_text(json.dumps(dict(launch.DEFAULTS, memoryMB=args.memory_mb, screen=args.screen,
                                      network=args.network, fullScreen=True, future={"retained":True})))
    config_before = config.read_bytes()
    warnings = []
    env = dict(os.environ, OJJY_BUILD_DIR=str(build), OJJY_IMAGE=str(disk), OJJY_ACCEL=args.accel)
    for key in ("OJJY_LAUNCH_SETTINGS", "OJJY_FIRMWARE", "OJJY_SERIAL_LOG", "OJJY_MEMORY_MB",
                "OJJY_SCREEN", "OJJY_RES", "OJJY_FULLSCREEN", "OJJY_NETWORK", "OJJY_DISPLAY"):
        env.pop(key, None)
    result = launch.command(ROOT, args.arch, env, False, warnings.append)
    argv = result["argv"][:]
    argv[argv.index("-display")+1] = "none"
    with tempfile.TemporaryDirectory(prefix="hw14-qmp-") as sockets:
        address = Path(sockets) / "q.sock"
        argv += ["-qmp", f"unix:{address},server=on,wait=off", "-no-reboot"]
        (output / "command.json").write_text(json.dumps(dict(result, testedArgv=argv, warnings=warnings), indent=2))
        with (output / "qemu.log").open("w") as log:
            process = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT)
            connection = None
            try:
                deadline = time.monotonic() + 100
                while not address.exists():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("QEMU did not open QMP; see qemu.log")
                    time.sleep(.1)
                connection = socket.socket(socket.AF_UNIX)
                connection.settimeout(10)
                connection.connect(str(address))
                stream = connection.makefile("rwb", buffering=0)
                json.loads(stream.readline())

                def qmp(execute, arguments=None):
                    request = dict(execute=execute)
                    if arguments is not None:
                        request["arguments"] = arguments
                    stream.write(json.dumps(request).encode() + b"\n")
                    while True:
                        response = json.loads(stream.readline())
                        if "error" in response:
                            raise RuntimeError(response)
                        if "return" in response:
                            return response["return"]

                qmp("qmp_capabilities")
                serial = build / args.arch / "serial.log"
                while True:
                    text = serial.read_text(errors="replace") if serial.exists() else ""
                    if "[PANIC]" in text:
                        raise RuntimeError("Guest panicked")
                    if "[INIT] Entering main loop" in text and (not args.network or args.arch == "x86_64" or "[NET] DHCP lease" in text):
                        break
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("Guest boot/network timeout; see serial.log")
                    time.sleep(.2)
                time.sleep(2)
                capture = output / "screen.ppm"
                qmp("screendump", dict(filename=str(capture)))
                with capture.open("rb") as image:
                    if image.readline().strip() != b"P6":
                        raise RuntimeError("Expected PPM capture")
                    dimensions = image.readline().strip().decode().replace(" ", "x")
                if dimensions != result["scanout"]:
                    raise RuntimeError(f"Requested scanout {result['scanout']}, captured {dimensions}")
                total = int(re.search(r"\[PMM\] Total usable memory: (\d+) MB", text)[1])
                if not args.memory_mb - 72 <= total <= args.memory_mb:
                    raise RuntimeError(f"PMM disagrees with {args.memory_mb} MiB request: {total}")
                if result["settings"]["memoryMB"] != args.memory_mb:
                    raise RuntimeError("Host limits clamped the requested test memory")
                if not args.network and "[NET] DHCP lease" in text:
                    raise RuntimeError("Network attached despite saved network=false")
                qmp("quit")
                process.wait(timeout=10)
                stream.close()
            finally:
                if connection is not None:
                    connection.close()
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    assert config.read_bytes() == config_before, "Runner changed settings"
    assert digest(source) == before, "Source image changed"
    (output / "report.json").write_text(json.dumps(dict(passed=True, sourceUnchanged=True,
        settingsUnchanged=True, sourceSHA256=before, pmmTotalMB=total, scanout=dimensions,
        effective=result["settings"], accelerator=result["accelerator"]), indent=2) + "\n")
    print(f"PASS: runner {args.arch}/{args.accel} PMM={total} MB scanout={dimensions}; {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--arch", choices=("aarch64", "x86_64"), default="aarch64")
    parser.add_argument("--accel", choices=("tcg", "hvf"), default="tcg")
    parser.add_argument("--network", action="store_true")
    parser.add_argument("--memory-mb", type=int, choices=launch.MEMORY, default=3072)
    parser.add_argument("--screen", choices=launch.SCREENS, default="1280x800")
    run(parser.parse_args())
