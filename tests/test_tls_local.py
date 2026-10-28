#!/usr/bin/env python3
"""Local TLS 1.3 handshakes: the kernel's TLS client (net/tls.c through
tests/tls_probe.c) against Python's TLS 1.3 server, with throwaway CAs from
tests/x509_fixtures.py as the only trusted roots. Covers the verified path
(ECDSA P-256 / P-384 and RSA-PSS CertificateVerify, IP host), each
certificate failure and its reason, and -k (connect anyway).

usage: test_tls_local.py [--out DIR] -- <cc> <flags...>   (macOS; Linux: skipped)
Created by Jonas Lee.
"""
import argparse
import os
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import x509_fixtures as fx  # noqa: E402


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"ok from the local TLS 1.3 server\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def serve(chain_pem, key, weak=False):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_3
    if weak:
        ctx.set_ciphers("DEFAULT:@SECLEVEL=0")       # lets OpenSSL load a 1024-bit key
    ctx.load_cert_chain(chain_pem, key)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None)
    ap.add_argument("cc", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    if sys.platform != "darwin":
        print("test_tls_local: SKIP (tests/tls_probe.c uses macOS sockets)")
        return 0
    cc = [a for a in args.cc if a != "--"] or ["clang"]
    out = args.out or tempfile.mkdtemp(prefix="ojjy-tls-local-")
    os.makedirs(out, exist_ok=True)

    now = time.time()
    stamp = lambda t: time.strftime("%Y%m%d%H%M%SZ", time.gmtime(t))     # noqa: E731
    pki = fx.build(os.path.join(out, "pki"), stamp(now - 86400), stamp(now + 3650 * 86400))
    c = pki.certs
    roots_pem = os.path.join(out, "roots.pem")
    with open(roots_pem, "w") as f:
        for name in ("root_a", "root_b"):
            f.write(open(c[name].pem).read())
    roots_c = os.path.join(out, "test_roots.c")
    with open(roots_c, "w") as f:
        subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "mkroots.py"), "--all", "--pem", roots_pem],
                       stdout=f, check=True, stderr=subprocess.DEVNULL)
    probe = os.path.join(out, "tls_probe_local")
    src = os.path.join(ROOT, "kernel", "src")
    crypto = [os.path.join(src, "crypto", n + ".c") for n in
              ("x509", "x509_chain", "sha256", "sha512", "bignum", "rsa", "ecdsa", "x25519", "aes_gcm", "chacha_poly")]
    subprocess.run(cc + [f"-I{src}", os.path.join(HERE, "tls_probe.c"), os.path.join(src, "net", "tls.c"), roots_c,
                         os.path.join(src, "random.c"), os.path.join(src, "string.c")] + crypto + ["-o", probe],
                   check=True)

    def chain(*names):
        path = os.path.join(out, "-".join(names) + ".chain.pem")
        with open(path, "w") as f:
            for n in names:
                f.write(open(c[n].pem).read())
        return path

    cases = [
        # (label, certificates served (leaf first), host asked for, -k, expected probe output)
        ("ECDSA P-256 leaf", ("leaf_a", "int_a"), "www.example.test", False, "verified=1 root=ojjyOS Test Root A"),
        ("RSA-PSS leaf", ("leaf_b", "int_b"), "rsa.example.test", False, "verified=1 root=ojjyOS Test Root B"),
        ("ECDSA P-384 leaf", ("leaf_c", "int_c"), "p256.example.test", False, "verified=1 root=ojjyOS Test Root A"),
        ("PSS-384 signed leaf", ("leaf_b384", "int_b"), "pss384.example.test", False, "verified=1"),
        ("IP address host", ("leaf_a", "int_a"), "127.0.0.1", False, "verified=1"),
        ("expired", ("leaf_expired", "int_a"), "www.example.test", False,
         "fail handshake: certificate expired on 2021-01-01 cert_failed=1"),
        ("wrong host", ("leaf_a", "int_a"), "other.example.test", False,
         "fail handshake: name mismatch: other.example.test vs www.example.test cert_failed=1"),
        ("untrusted root", ("leaf_untrusted", "root_x"), "www.example.test", False,
         "fail handshake: unknown issuer: ojjyOS Untrusted Root cert_failed=1"),
        ("self-signed", ("leaf_self",), "www.example.test", False,
         "fail handshake: self-signed certificate cert_failed=1"),
        ("missing intermediate", ("leaf_a",), "www.example.test", False,
         "fail handshake: unknown issuer: ojjyOS Test Intermediate A cert_failed=1"),
        ("CA:false issuer", ("leaf_noca", "int_noca"), "www.example.test", False,
         "issuer is not a certificate authority: ojjyOS Test Not A CA cert_failed=1"),
        ("weak RSA key", ("leaf_rsa1024", "int_b"), "weak.example.test", False,
         "fail handshake: the server's RSA key is too weak cert_failed=1"),
        ("-k wrong host", ("leaf_a", "int_a"), "other.example.test", True,
         "verified=0 root=- subject=www.example.test cert_error=name mismatch: other.example.test vs www.example.test"),
        ("-k self-signed", ("leaf_self",), "www.example.test", True, "verified=0 root=- subject=www.example.test "
                                                                    "cert_error=self-signed certificate"),
    ]
    failures = 0
    log = []
    for label, names, host, insecure, want in cases:
        server = serve(chain(*names), c[names[0]].key, weak=names[0] == "leaf_rsa1024")
        try:
            port = server.server_address[1]
            cmd = [probe] + (["-k"] if insecure else []) + ["127.0.0.1", str(port), host]
            got = subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout.strip()
        finally:
            server.shutdown()
            server.server_close()
        ok = want in got and (("PROBE ok" in got) == ("fail" not in want))
        if ok and "PROBE ok" in got:
            ok = "status=HTTP/1.0 200 OK" in got or "status=HTTP/1.1 200 OK" in got
        failures += not ok
        line = f"{'PASS' if ok else 'FAIL'} {label}: {got}"
        log.append(line)
        print(line)
    with open(os.path.join(out, "results.txt"), "w") as f:
        f.write("\n".join(log) + "\n")
    print(f"test_tls_local: {len(cases) - failures}/{len(cases)} cases passed (evidence {out})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
