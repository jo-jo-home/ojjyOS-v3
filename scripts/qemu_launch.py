#!/usr/bin/env python3
"""Shared-settings QEMU runner. Created by Jonas Lee."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

DEFAULTS = dict(version=1, memoryMB=2048, cpus=1, screen="1440x900",
                network=True, fullScreen=False, showAtStart=True)
SCREENS = ("1440x900", "1280x800", "1600x900")
MEMORY = (2048, 3072, 4096, 6144, 8192)


def host_memory():
    if platform.system() == "Darwin":
        return int(subprocess.check_output(["sysctl", "-n", "hw.memsize"], text=True))
    return os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")


def memory_choices(host_bytes):
    return [value for value in MEMORY if value * 1024 * 1024 <= host_bytes // 2]


def invalid_constant(value):
    raise ValueError(f"Invalid JSON constant {value}")


def integer(value):
    if type(value) is float and value.is_integer():
        value = int(value)
    return value if type(value) is int and -(1 << 63) <= value < (1 << 63) else None


def settings(path, host_bytes, warn):
    choices = memory_choices(host_bytes)
    if not choices:
        raise ValueError("No supported RAM setting within half host memory")
    result = dict(DEFAULTS)
    try:
        with path.open("rb") as source:
            data = source.read(65537)
        if len(data) > 65536:
            raise ValueError("settings exceed 64 KiB")
        value = json.loads(data, parse_constant=invalid_constant)
        if not isinstance(value, dict):
            raise ValueError("expected a JSON object")
        version = integer(value.get("version", 1))
        if version is None:
            warn("Invalid version; using default")
            version = 1
        if version != 1:
            raise ValueError("unsupported settings version")
        for key, fallback in DEFAULTS.items():
            candidate = value.get(key, fallback)
            if type(fallback) is int:
                candidate = integer(candidate)
            if type(candidate) is type(fallback):
                result[key] = candidate
            else:
                warn(f"Invalid {key}; using default")
    except (OSError, ValueError, UnicodeError, RecursionError) as error:
        warn(f"Cannot read launch settings ({error}); using defaults")
    result["memoryMB"] = min(choices, key=lambda value: abs(value - result["memoryMB"]))
    result["cpus"] = 1
    if result["screen"] not in SCREENS:
        warn("Unsupported screen; using default")
        result["screen"] = DEFAULTS["screen"]
    return result


def resolve(path, host_bytes, env, arch, system, machine, hv_support, warn):
    value = settings(path, host_bytes, warn)
    if "OJJY_MEMORY_MB" in env:
        raw = env["OJJY_MEMORY_MB"]
        if not re.fullmatch(r"[0-9]{1,5}", raw) or int(raw) not in memory_choices(host_bytes):
            raise ValueError("OJJY_MEMORY_MB must be a supported RAM choice within half host memory")
        value["memoryMB"] = int(raw)
    if "OJJY_SCREEN" in env:
        if env["OJJY_SCREEN"] not in SCREENS:
            raise ValueError("OJJY_SCREEN must be 1440x900, 1280x800 or 1600x900")
        value["screen"] = env["OJJY_SCREEN"]
    for variable, key in (("OJJY_NETWORK", "network"), ("OJJY_FULLSCREEN", "fullScreen")):
        if variable in env:
            if env[variable] not in ("0", "1"):
                raise ValueError(f"{variable} must be 0 or 1")
            value[key] = env[variable] == "1"
    accel = env.get("OJJY_ACCEL", "auto")
    if accel not in ("auto", "hvf", "tcg"):
        raise ValueError("OJJY_ACCEL must be auto, hvf or tcg")
    available = system == "Darwin" and machine == "arm64" and hv_support
    if arch == "x86_64":
        if accel == "hvf":
            raise ValueError("Intel runner uses TCG, not ARM HVF")
        accel = "tcg"
        if value["network"]:
            warn("Intel guest networking unsupported; disabled for this launch")
        value["network"] = False
    else:
        if accel == "auto":
            accel = "hvf" if available else "tcg"
        if accel == "hvf" and not available:
            raise ValueError("ARM HVF requires an Apple Silicon Mac with virtualization support")
    width, height = map(int, value["screen"].split("x"))
    scale = 2 if arch == "aarch64" and accel == "hvf" else 1
    width, height = width * scale, height * scale
    if "OJJY_RES" in env:
        raw = env["OJJY_RES"]
        if not re.fullmatch(r"[0-9]{3,4}x[0-9]{3,4}", raw):
            raise ValueError("OJJY_RES must be a bounded physical WIDTHxHEIGHT")
        width, height = map(int, raw.split("x"))
        if not 640 <= width <= 4096 or not 480 <= height <= 2160:
            raise ValueError("OJJY_RES outside 640..4096 by 480..2160")
    display = env.get("OJJY_DISPLAY", "gpu")
    if display not in ("gpu", "ramfb"):
        raise ValueError("OJJY_DISPLAY must be gpu or ramfb")
    if display == "ramfb":
        if arch != "aarch64":
            raise ValueError("ramfb is only supported by the ARM runner")
        width, height = 1024, 768
        warn("ramfb fixed 1024x768 overrides the saved screen")
    if arch == "x86_64" and f"{width}x{height}" not in (*SCREENS, "1920x1080"):
        raise ValueError("Intel physical resolution must be a supported logical screen or 1920x1080")
    return value, accel, display, width, height


def firmware(arch, env):
    if "OJJY_FIRMWARE" in env:
        path = Path(env["OJJY_FIRMWARE"])
        if not path.is_file():
            raise ValueError("OJJY_FIRMWARE does not exist")
        return path
    names = (f"/opt/homebrew/share/qemu/edk2-{arch}-code.fd",
             f"/usr/local/share/qemu/edk2-{arch}-code.fd")
    names += (("/usr/share/qemu-efi-aarch64/QEMU_EFI.fd", "/usr/share/AAVMF/AAVMF_CODE.fd",
               "/usr/share/edk2/aarch64/QEMU_EFI.fd", "/usr/share/ovmf/AAVMF_CODE.fd")
              if arch == "aarch64" else ("/usr/share/ovmf/OVMF.fd", "/usr/share/OVMF/OVMF_CODE.fd",
                                         "/usr/share/edk2/ovmf/OVMF_CODE.fd"))
    for name in names:
        if Path(name).is_file():
            return Path(name)
    raise ValueError(f"UEFI firmware missing for {arch}; install QEMU and EDK2")


def command(root, arch, env, dry_run, warn):
    build = Path(env.get("OJJY_BUILD_DIR", str(root / "build"))).resolve()
    config = Path(env.get("OJJY_LAUNCH_SETTINGS", str(build / "launch-settings.json")))
    image = Path(env.get("OJJY_IMAGE", str(build / arch / "ojjyos3.img"))).resolve()
    serial = Path(env.get("OJJY_SERIAL_LOG", str(build / arch / "serial.log"))).resolve()
    system, machine = platform.system(), platform.machine()
    hv_support = False
    if system == "Darwin" and machine == "arm64":
        hv_support = subprocess.check_output(["sysctl", "-n", "kern.hv_support"], text=True).strip() == "1"
    value, accel, display, width, height = resolve(config, host_memory(), env, arch, system, machine, hv_support, warn)
    executable = f"qemu-system-{arch}"
    if not shutil.which(executable):
        raise ValueError(f"{executable} missing; install QEMU")
    if not image.is_file():
        raise ValueError(f"Disk image missing: {image}; build {arch} first")
    code = firmware(arch, env).resolve()
    pflash = (build / arch / "firmware.fd").resolve()
    paths = [image, serial, config.resolve(), code, pflash]
    for index, path in enumerate(paths):
        for other in paths[index + 1:]:
            if path == other or (path.exists() and other.exists() and os.path.samefile(path, other)):
                raise ValueError("Disk, serial, settings and firmware paths must be separate")
    escape = lambda path: str(path).replace(",", ",,")
    argv = [executable, "-m", str(value["memoryMB"]), "-smp", "1"]
    if system == "Darwin":
        argv += ["-display", "cocoa,show-cursor=off,zoom-to-fit=on,full-screen=" + ("on" if value["fullScreen"] else "off")]
    else:
        argv += ["-display", "gtk,show-cursor=off,full-screen=" + ("on" if value["fullScreen"] else "off")]
    if arch == "aarch64":
        argv += ["-M", "virt,gic-version=" + ("3" if accel == "hvf" else "2"),
                 "-accel", "hvf" if accel == "hvf" else "tcg,thread=multi",
                 "-cpu", "host" if accel == "hvf" else "cortex-a72", "-bios", str(code),
                 "-global", "virtio-mmio.force-legacy=false", "-device",
                 "ramfb" if display == "ramfb" else f"virtio-gpu-device,xres={width},yres={height}",
                 "-device", "virtio-keyboard-device", "-device", "virtio-tablet-device",
                 "-drive", f"file={escape(image)},format=raw,if=none,id=hd",
                 "-device", "virtio-blk-device,drive=hd", "-device", "virtio-rng-device"]
        if value["network"]:
            argv += ["-netdev", "user,id=net0", "-device", "virtio-net-device,netdev=net0"]
        else:
            argv += ["-nic", "none"]
    else:
        argv += ["-M", "pc", "-accel", "tcg", "-vga", "none", "-device", f"VGA,xres={width},yres={height}",
                 "-fw_cfg", f"name=opt/ojjyOS/display,string={width}x{height}",
                 "-drive", f"file={escape(image)},format=raw", "-nic", "none"]
        template = code.parent / "edk2-i386-vars.fd"
        if template.is_file():
            if not dry_run and not pflash.exists():
                with pflash.open("xb") as target:
                    target.write(template.read_bytes() + code.read_bytes())
            argv += ["-drive", f"if=pflash,format=raw,file={escape(pflash)}"]
        else:
            argv += ["-bios", str(code)]
    argv += ["-serial", f"file:{serial}"]
    return dict(argv=argv, settings=value, accelerator=accel, scanout=f"{width}x{height}", settingsPath=str(config))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", nargs="?", choices=("aarch64", "arm64", "x86_64"), default="aarch64")
    parser.add_argument("--dry-run", action="store_true")
    options = parser.parse_args()
    arch = "aarch64" if options.arch == "arm64" else options.arch
    try:
        result = command(Path(__file__).resolve().parents[1], arch, os.environ, options.dry_run,
                         lambda text: print(f"ojjyOS: {text}", file=sys.stderr))
        if options.dry_run:
            print(json.dumps(result, sort_keys=True))
        else:
            print(f"ojjyOS — Created by Jonas Lee: {arch}, {result['accelerator']}, "
                  f"{result['settings']['memoryMB']} MiB, scanout {result['scanout']}", flush=True)
            os.execvp(result["argv"][0], result["argv"])
    except (OSError, ValueError) as error:
        print(f"ojjyOS: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
