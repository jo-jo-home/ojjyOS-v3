#!/usr/bin/env python3
"""Private Control API → native menu → QEMU round trip. Created by Jonas Lee."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import signal
import subprocess
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("qemu_launch", ROOT / "scripts/qemu_launch.py")
launch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launch)


def run(args):
    control = Path(args.control_repo).resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = ["deno", "run", "--allow-read", "--allow-write", "--allow-env", "--allow-sys",
               "--allow-run=python3,git", "--allow-net=127.0.0.1", str(control / "tests/launch-settings-ui-server.ts")]
    with (output / "server.log").open("w") as server_log:
        server = subprocess.Popen(command, cwd=control, stdout=subprocess.PIPE, stderr=server_log, text=True)
        try:
            info = json.loads(server.stdout.readline())
            base, fixture = info["base"], Path(info["root"])

            def api(method, value=None, expect=200):
                headers = dict(Origin=base)
                if token is not None:
                    headers["x-ojjy-token"] = token
                if value is not None:
                    headers["Content-Type"] = "application/json"
                request = urllib.request.Request(base + "/api/ojjyos/launch-settings",
                    data=None if value is None else json.dumps(value).encode(), method=method, headers=headers)
                try:
                    with urllib.request.urlopen(request, timeout=20) as response:
                        assert response.status == expect
                        return json.load(response)
                except urllib.error.HTTPError as error:
                    assert error.code == expect, error.read().decode()
                    return json.loads(error.read())

            with urllib.request.urlopen(base + "/api/session", timeout=10) as response:
                token = json.load(response)["token"]
            initial = api("GET")
            requested = dict(initial["settings"], memoryMB=4096, screen="1600x900", network=False,
                             fullScreen=False, showAtStart=True)
            saved = api("PUT", dict(revision=initial["revision"], settings=requested))
            api("PUT", dict(revision=initial["revision"], settings=initial["settings"]), expect=409)
            path = fixture / "build/launch-settings.json"
            before = path.read_bytes()
            assert json.loads(before)["extension"] == dict(keep=True)
            probe = ROOT / "build/tests-vmapp/launch-settings-probe"

            def native(operation, value=None, revision=None):
                argv = [str(probe), operation, str(path), str(launch.host_memory())]
                if operation == "save":
                    argv += [json.dumps(value), revision]
                return json.loads(subprocess.check_output(argv, text=True))

            loaded = native("load")
            assert loaded["settings"] == saved["settings"]
            assert loaded["revision"] == saved["revision"]
            assert launch.settings(path, launch.host_memory(), lambda _:None) == requested
            bundle = fixture / "build/ojjyOS.app"
            subprocess.run(["ditto", "--norsrc", str(ROOT / "build/ojjyOS.app"), str(bundle)], check=True)
            plist = bundle / "Contents/Info.plist"
            metadata = plistlib.loads(plist.read_bytes())
            metadata["CFBundleIdentifier"] = "com.jonaslee.ojjyos.hw14control"
            plist.write_bytes(plistlib.dumps(metadata))
            with (output / "native.log").open("w") as native_log:
                subprocess.run(["codesign", "--sign", "-", "--force", "--entitlements",
                    str(ROOT / "vmapp/ojjyOS.entitlements"), str(bundle)], check=True, stdout=native_log, stderr=subprocess.STDOUT)
                for appearance in ("light", "dark"):
                    subprocess.run([str(bundle / "Contents/MacOS/ojjyOS"), "--capture-launch",
                        str(output / f"native-{appearance}.png"), "--appearance", appearance], check=True,
                        stdout=native_log, stderr=subprocess.STDOUT, timeout=20)
            assert path.read_bytes() == before, "Menu capture must not save or start a VM"
            returned = dict(requested, memoryMB=3072, network=True, showAtStart=False)
            native_saved = native("save", returned, saved["revision"])
            reloaded = api("GET")
            assert reloaded["settings"] == returned
            assert reloaded["revision"] == native_saved["revision"]
            assert launch.settings(path, launch.host_memory(), lambda _:None) == returned
            assert json.loads(path.read_bytes())["extension"] == dict(keep=True)
            (output / "report.json").write_text(json.dumps(dict(passed=True, controlToNative=requested,
                nativeToControlAndQemu=returned, staleSaveStatus=409, unknownKeysRetained=True,
                nativeMenuCaptures=["native-light.png", "native-dark.png"], privateFilesOnly=True,
                savedSHA256=hashlib.sha256(before).hexdigest()), indent=2) + "\n")
        finally:
            server.send_signal(signal.SIGTERM)
            try:
                server.wait(timeout=20)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            if server.stdout is not None:
                server.stdout.close()
    print(f"PASS: private Control API/native menu/QEMU round trip; {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control-repo", required=True)
    parser.add_argument("--output", required=True)
    run(parser.parse_args())
