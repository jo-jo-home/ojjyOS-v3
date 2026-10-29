#!/usr/bin/env python3
"""Private ARM high-page/isolation/browser proof, not a VZ clock-test replacement.
Created by Jonas Lee. Only the child VM and a copied disk are touched.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import re
import shutil
import ssl
import subprocess
import threading
import time
from PIL import Image
from qemu_smoke import VM, protected_checks
from vz_smoke import NativeVM, digest


def run(args):
    out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=False)
    source=Path(args.image).resolve();before=digest(source)
    disk=out/'test-disk.img';shutil.copyfile(source,disk)
    with disk.open('r+b') as f:
        f.seek(67584*512);f.write(bytes((102400-67584)*512))
    native=args.backend=='vz'
    if native:
        app=Path(args.app).resolve();bundle=app.parents[2]
        if bundle.suffix!='.app': raise RuntimeError('--app must be the executable inside an app bundle')
        private=out/'ojjyOS.app';shutil.copytree(bundle,private)
        vm=NativeVM(private/'Contents/MacOS/ojjyOS',disk,out,1,memory=args.memory)
    else:
        vm=VM('aarch64',disk,out,1,memory=args.memory)
    servers=[]
    report={'backend':args.backend,'memory_mib':args.memory,'source_sha256':before}
    try:
        vm.wait('Entering main loop');vm.wait('[NET] DHCP lease')
        text=vm.text()
        proof=re.search(r'\[KVM\] HIGH zero/write/read/free pa=(?:0x)?([0-9a-fA-F]+) result=PASS',text)
        assert proof and int(proof[1],16)>=1<<32,'no actual HIGH write/read/free proof'
        assert '[KVM] ARM RAM-only alias ready' in text
        report['high_pattern_pa']=int(proof[1],16)
        # Shared existing input helpers; all writes target our fresh disk.
        if native:
            for value in ['Jonas Lee','tester','testpass','testpass']:
                vm.type(value);vm.press(43)
            vm.press(40)
        else:
            vm.type('Jonas Lee\ttester\ttestpass\ttestpass\n')
        vm.wait("User 'tester' logged in");vm.wait('[UI] Desktop presented')
        vm.console()
        mark=len(vm.text());protected_checks(vm,'aarch64',out)
        text=vm.text()[mark:]
        prepared=re.findall(r'\[USER\] Prepared high_pages=(\d+) first_high=(?:0x)?([0-9a-fA-F]+)',text)
        assert len(prepared)>=34 and all(int(n)>0 and int(pa,16)>=1<<32 for n,pa in prepared)
        cleanups=re.findall(r'free_before=(\d+) free_after=(\d+)',text)
        assert len(cleanups)>=34 and all(a==b for a,b in cleanups)
        # The probes use a known *present* alias; permission faults, not just
        # translation faults, establish that EL0 cannot use the shared TTBR1.
        for name in ['alias_read','alias_write','alias_exec']:
            pattern=rf'\[USER\] Cleanup[^\n]*far=ffff800100000000 esr=([0-9a-fA-F]+)'
            faults=[int(x,16) for x in re.findall(pattern,text)]
            assert any((x&63) in (13,14,15) and (x>>26)&63==(0x20 if name=='alias_exec' else 0x24)
                       and (name!='alias_write' or x&(1<<6)) for x in faults),name+' did not permission-fault'
        report.update(protected_high_runs=len(prepared),cleanup_equal=len(cleanups),el0_alias_permission_faults=True)

        # Bind only the private VM bridge for VZ; QEMU uses loopback forwarding.
        gateway=re.search(r'DHCP lease [\d.]+ gateway ([\d.]+)',vm.text())[1]
        bind=gateway if native else '127.0.0.1';host=gateway if native else '10.0.2.2'
        png=io.BytesIO();Image.new('RGB',(48,32),(20,110,220)).save(png,format='PNG')
        requests=[]
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*a): pass
            def do_GET(self):
                requests.append(self.path)
                image=self.path=='/memory.png'
                body=png.getvalue() if image else b'<html><body><h1>High RAM browser</h1><p>CPU-only mapped arenas and image decode.</p><img src="/memory.png"></body></html>'
                self.send_response(200);self.send_header('Content-Type','image/png' if image else 'text/html')
                self.send_header('Content-Length',str(len(body)));self.send_header('Connection','close');self.end_headers();self.wfile.write(body)
        cert,key=out/'cert.pem',out/'key.pem'
        subprocess.run(['openssl','req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:prime256v1',
                        '-keyout',str(key),'-out',str(cert),'-days','2','-nodes','-subj',f'/CN={host}',
                        '-addext',f'subjectAltName=IP:{host}'],capture_output=True,check=True)
        ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.minimum_version=ssl.TLSVersion.TLSv1_3
        ctx.load_cert_chain(cert,key)
        for tls in [False,True]:
            server=ThreadingHTTPServer((bind,0),Handler)
            if tls: server.socket=ctx.wrap_socket(server.socket,server_side=True)
            threading.Thread(target=server.serve_forever,daemon=True).start();servers.append(server)
        def press(name): vm.press({'spc':44,'l':15,'ret':40}.get(name,name) if native else name)
        def chord(name):
            vm.key('meta_l',True);press(name);vm.key('meta_l',False);time.sleep(.3)
        def visit(url,receipt):
            mark=len(vm.text());chord('l');vm.type(url+'\n');vm.wait(receipt,after=mark,timeout=90)
        chord('spc');vm.type('web\n');vm.wait('[WEB] view ')
        http=f'http://{host}:{servers[0].server_address[1]}/'
        https=f'https://{host}:{servers[1].server_address[1]}/'
        visit(http,'[WEB] loaded status=200')
        vm.wait('status=200 bytes=',timeout=30);time.sleep(2)
        assert '/memory.png' in requests,'image resource not fetched'
        vm.screenshot(out/'high-browser-http.png')
        visit(https,f'[WEB] cert warning {host}: self-signed certificate')
        # Direct user-entered omnibox navigation (not page-controlled exception).
        visit('ojjy:proceed?url='+https,'[WEB] loaded status=200')
        vm.wait(f'[WEB] security {host} exception self-signed certificate');time.sleep(2)
        vm.screenshot(out/'high-browser-https.png')
        text=vm.text()
        buffers=[(int(pa,16),int(n),int(high)) for pa,n,high in re.findall(
            r'\[PMM\] Mapped buffer pa=(?:0x)?([0-9a-fA-F]+) bytes=(\d+) high=(\d+)',text)]
        for size in [40<<20,28<<20,20<<20,48<<20]:
            assert any(pa>=1<<32 and n==size and high==1 for pa,n,high in buffers),size
        assert any(pa>=1<<32 and n<1<<20 and high==1 for pa,n,high in buffers),'TLS slot not HIGH'
        assert 'decode failed' not in text and 'undecodable' not in text and '[PANIC]' not in text
        assert digest(source)==before,'source disk changed'
        report.update(mapped_buffers=buffers,local_http_https_image=True,requests=requests,source_unchanged=True,
                      limitation='Does not replace full vz_boot clock test; BUG-1008 remains part 4')
        (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        print('PASS high RAM, protected isolation/cleanup and HTTP/HTTPS/image:',out,flush=True)
    finally:
        vm.close()
        for server in servers:server.shutdown();server.server_close()
        assert digest(source)==before,'source disk changed even during failed test'


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--backend',choices=['qemu','vz'],required=True)
    p.add_argument('--memory',type=int,default=4096)
    p.add_argument('--image',required=True);p.add_argument('--app')
    p.add_argument('--output',required=True)
    run(p.parse_args())
