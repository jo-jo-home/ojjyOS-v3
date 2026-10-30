#!/usr/bin/env python3
"""Private guest 3GiB peak-owned pattern/free/recovery proof. Jonas Lee."""
import argparse
import json
from pathlib import Path
import re
import shutil
import time
from qemu_smoke import VM
from vz_smoke import NativeVM,digest


def run(args):
    out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=False)
    source=Path(args.image).resolve();before=digest(source)
    disk=out/'test-disk.img';shutil.copyfile(source,disk)
    with disk.open('r+b') as f:
        f.seek(67584*512);f.write(bytes((102400-67584)*512))
    native=args.backend=='vz'
    if native:
        app=Path(args.app).resolve();bundle=app.parents[2]
        if bundle.suffix!='.app':raise RuntimeError('Expected executable inside native app bundle')
        private=out/'ojjyOS.app';shutil.copytree(bundle,private)
        vm=NativeVM(private/'Contents/MacOS/ojjyOS',disk,out,1,memory=args.memory)
    else:vm=VM(args.arch,disk,out,1,memory=args.memory)
    try:
        vm.wait('Entering main loop');vm.wait('[KVM] HIGH zero/write/read/free')
        assert 'result=PASS' in vm.text()
        if native:
            for value in ['Jonas Lee','tester','testpass','testpass']:
                vm.type(value);vm.press(43)
            vm.press(40)
        else:vm.type('Jonas Lee\ttester\ttestpass\ttestpass\n')
        vm.wait("User 'tester' logged in");vm.wait('[UI] Desktop presented');vm.console()
        mark=len(vm.text());start=time.monotonic();vm.type('memtest\n')
        vm.wait('[MEMTEST] result=',after=mark,timeout=600)
        vm.wait('[MEMTEST] free ',after=mark,timeout=30)
        text=vm.text()[mark:]
        m=re.search(r'\[MEMTEST\] result=(\S+) requested=(\d+) peak=(\d+) low=(\d+) high=(\d+) high_min=(?:0x)?([0-9a-fA-F]+) high_max=(?:0x)?([0-9a-fA-F]+) written=(\d+) verified=(\d+) restored=(\d+)',text)
        assert m and m[1]=='PASS',text
        requested,peak,low,high=map(int,[m[2],m[3],m[4],m[5]])
        assert requested==peak==3<<30 and low+high==peak and high>0
        assert int(m[6],16)>=1<<32 and int(m[7],16)>=int(m[6],16)
        assert int(m[8])==int(m[9])==requested//4096 and m[10]=='1'
        free=re.search(r'free low_before=(\d+) low_after=(\d+) high_before=(\d+) high_after=(\d+)',text)
        assert free and free[1]==free[2] and free[3]==free[4]
        mark=len(vm.text());vm.type('run hello\n')
        if args.arch=='aarch64':vm.wait('status=exited code=0',after=mark)
        else:vm.wait('protected execution unsupported',after=mark)
        vm.type('whoami\n');vm.wait('tester (Jonas Lee)',after=mark)
        vm.type('ui\n');time.sleep(2);vm.screenshot(out/'desktop-after-stress.png')
        assert digest(source)==before
        report={'result':'PASS','backend':args.backend,'arch':args.arch,'memory_mib':args.memory,
                'simultaneous_peak_owned_bytes':peak,'low_peak_bytes':low,'high_peak_bytes':high,
                'high_pa_min':int(m[6],16),'high_pa_max':int(m[7],16),'written_pages':int(m[8]),'verified_pages':int(m[9]),
                'free_counts_equal':True,'recovery':'protected hello' if args.arch=='aarch64' else 'unsupported EL0 + whoami + desktop',
                'elapsed_seconds':time.monotonic()-start,'source_sha256':before,'source_unchanged':True}
        (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        print('PASS 3GiB simultaneous ownership/every-page verification/free/recovery:',out,flush=True)
    finally:
        vm.close()
        assert digest(source)==before,'source image changed'


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--backend',choices=['qemu','vz'],required=True)
    p.add_argument('--arch',choices=['aarch64','x86_64'],default='aarch64')
    p.add_argument('--memory',type=int,default=4096)
    p.add_argument('--image',required=True);p.add_argument('--app');p.add_argument('--output',required=True)
    run(p.parse_args())
