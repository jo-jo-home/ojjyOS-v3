#!/usr/bin/env python3
"""HW-12 private-disk memory/display/launch lifecycle matrix. Created by Jonas Lee."""
import argparse
import json
import os
import plistlib
from pathlib import Path
import re
import shutil
import subprocess
from PIL import Image
from vz_smoke import digest, pointer_hotspot


def run(args):
    root = Path(__file__).resolve().parents[1]
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = Path(args.image).resolve()
    before = digest(source)
    bundle = output / 'ojjyOS.app'
    (bundle / 'Contents/MacOS').mkdir(parents=True)
    binary = bundle / 'Contents/MacOS/ojjyOS'
    info = plistlib.loads((root / 'build/ojjyOS.app/Contents/Info.plist').read_bytes())
    info['CFBundleIdentifier'] = 'com.jonaslee.ojjyos.launchtest'
    (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    with (output / 'build.log').open('w') as log:
        subprocess.run(['swiftc', '-O', '-warnings-as-errors', '-D', 'OJJY_VMAPP_LAUNCH_TEST',
                        *[str(root / p) for p in ('vmapp/LaunchSettings.swift', 'vmapp/LaunchWindow.swift',
                                                  'vmapp/main.swift', 'tests/launch_vm_checks.swift')],
                        '-framework', 'Virtualization', '-framework', 'AppKit', '-o', str(binary)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['codesign', '--sign', '-', '--force', '--entitlements',
                        str(root / 'vmapp/ojjyOS.entitlements'), str(bundle)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    results = []
    cases = [
            (2048, '1440x900', True, False, True, []),
            (3072, '1280x800', False, False, True, []),
            (3072, '1600x900', True, True, True, ['--start']),
            (2048, '1440x900', True, False, False, [])]
    if args.case is not None:
        cases = [cases[args.case]]
    for memory, screen, network, full, show, flags in cases:
        case = output / f'{memory}-{screen}-{len(results)}'
        case.mkdir()
        disk = case / 'disk.img'
        shutil.copyfile(source, disk)
        settings = dict(version=1, memoryMB=memory, cpus=1, screen=screen, network=network,
                        fullScreen=full, showAtStart=show)
        (case / 'launch-settings.json').write_text(json.dumps(settings))
        env = dict(os.environ, OJJY_LAUNCH_TEST_OUTPUT=str(case))
        with (case / 'app.log').open('w') as log:
            subprocess.run([str(binary), '--image', str(disk), '--vars', str(case / 'vars.fd'),
                            '--serial', str(case / 'serial.log'), *flags], env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        text = (case / 'serial.log').read_text(errors='replace')
        if '[PANIC]' in text or '[INIT] Entering main loop' not in text:
            raise RuntimeError(f'Guest boot failed: {case}')
        width, height = map(int, screen.split('x'))
        point = pointer_hotspot(Image.open(case / 'screen.png').convert('RGB'),
                                Image.open(case / 'pointer.png').convert('RGB'), 100, 100, (width, height))
        if not point or abs(point[0]-100) > 3 or abs(point[1]-100) > 3:
            raise RuntimeError(f'Pointer is not aligned for {screen}: {point}')
        result = json.loads((case / 'lifecycle.json').read_text())
        result.update(pmm_total_mb=int(re.search(r'\[PMM\] Total usable memory: (\d+) MB', text)[1]),
                      pmm_free_mb=int(re.search(r'\[PMM\] Free memory:  (\d+) MB', text)[1]),
                      pointer=point, directory=case.name)
        results.append(result)
    if args.case is None and results[0]['pmm_total_mb'] == results[1]['pmm_total_mb']:
        raise RuntimeError('2048/3072 MB boots did not produce distinct PMM totals')
    if digest(source) != before:
        raise RuntimeError('Source disk changed')
    (output / 'report.json').write_text(json.dumps(dict(cases=results, source_unchanged=True,
                                                     source_sha256=before), indent=2)+'\n')
    print(f'PASS: four live launch cases, all three screens, network/fullscreen/pending settings/pointer; {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--case', type=int, choices=range(4), help='diagnose one lifecycle case; default runs the full matrix')
    run(parser.parse_args())
