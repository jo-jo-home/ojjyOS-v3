#!/usr/bin/env python3
"""Throwaway test certificate authorities built with the OpenSSL CLI.

Used by tests/gen_x509_vectors.py (DER vectors for tests/test_x509.c) and
tests/test_tls_local.py (live handshakes against Python's TLS 1.3 server).
Every key and certificate gets its own file name (N1 lesson: a shared
name silently mixed up vectors). Created by Jonas Lee.
"""
import calendar
import os
import subprocess

VALID_FROM = "20260101000000Z"
VALID_UNTIL = "20360101000000Z"


def openssl(*args, data=b""):
    return subprocess.run(["openssl", *args], input=data, capture_output=True, check=True).stdout


def unix(stamp):
    """YYYYMMDDHHMMSSZ -> Unix seconds (UTC)."""
    return calendar.timegm((int(stamp[0:4]), int(stamp[4:6]), int(stamp[6:8]),
                            int(stamp[8:10]), int(stamp[10:12]), int(stamp[12:14])))


KEY_OPTS = {
    "rsa1024": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:1024"],   # weak-key case only
    "rsa2048": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048"],
    "rsa3072": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072"],
    "rsa4096": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:4096"],
    "p256": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256"],
    "p384": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-384"],
}


class Cert:
    def __init__(self, name, path, key_kind, cn, sig, not_before, not_after, issuer):
        self.name, self.key_kind, self.cn, self.sig = name, key_kind, cn, sig
        self.key = f"{path}.key"
        self.pem = f"{path}.pem"
        self.der_path = f"{path}.der"
        self.not_before, self.not_after = unix(not_before), unix(not_after)
        self.issuer = issuer

    @property
    def der(self):
        with open(self.der_path, "rb") as f:
            return f.read()


class Pki:
    def __init__(self, workdir, valid_from=VALID_FROM, valid_until=VALID_UNTIL):
        self.work = workdir
        self.valid_from, self.valid_until = valid_from, valid_until
        os.makedirs(workdir, exist_ok=True)
        self.serial = 4096
        self.certs = {}

    def cert(self, name, key_kind, cn, issuer=None, ext=(), digest="sha256", pss=False,
             not_before=None, not_after=None, org="ojjyOS Test"):
        """issuer: a Cert (signs) or None (self-signed). ext: openssl.cnf lines."""
        not_before = not_before or self.valid_from
        not_after = not_after or self.valid_until
        path = os.path.join(self.work, name)
        cert = Cert(name, path, key_kind, cn, "", not_before, not_after, issuer)
        openssl("genpkey", *KEY_OPTS[key_kind], "-out", cert.key)
        openssl("req", "-new", "-key", cert.key, "-subj", f"/CN={cn}/O={org}", "-out", f"{path}.csr")
        with open(f"{path}.ext", "w") as f:
            f.write("[ext]\nsubjectKeyIdentifier = hash\n" + "".join(line + "\n" for line in ext))
        self.serial += 1
        cmd = ["x509", "-req", "-in", f"{path}.csr", "-out", cert.pem, "-set_serial", str(self.serial),
               "-not_before", not_before, "-not_after", not_after,
               "-extfile", f"{path}.ext", "-extensions", "ext", f"-{digest}"]
        cmd += ["-CA", issuer.pem, "-CAkey", issuer.key] if issuer else ["-key", cert.key]
        if pss:
            cmd += ["-sigopt", "rsa_padding_mode:pss", "-sigopt", "rsa_pss_saltlen:digest"]
        openssl(*cmd)
        openssl("x509", "-in", cert.pem, "-outform", "DER", "-out", cert.der_path)
        signer_kind = issuer.key_kind if issuer else key_kind
        if signer_kind.startswith("rsa"):
            cert.sig = f"X509_SIG_RSA_{'PSS' if pss else 'PKCS1'}_{digest.upper()}"
        else:
            cert.sig = f"X509_SIG_ECDSA_{digest.upper()}"
        self.certs[name] = cert
        return cert


CA = ["basicConstraints = critical,CA:true", "keyUsage = critical,keyCertSign,cRLSign"]
LEAF = ["basicConstraints = critical,CA:false", "keyUsage = critical,digitalSignature",
        "extendedKeyUsage = serverAuth"]


def build(workdir, valid_from=VALID_FROM, valid_until=VALID_UNTIL):
    """The PKI the C tests expect (names are referenced from test_x509.c).
    The live handshake test passes dates around today instead."""
    p = Pki(workdir, valid_from, valid_until)
    # A: RSA-4096 root -> P-384 intermediate (pathLen 0) -> P-256 leaf
    root_a = p.cert("root_a", "rsa4096", "ojjyOS Test Root A", None, CA)
    int_a = p.cert("int_a", "p384", "ojjyOS Test Intermediate A", root_a,
                   ["basicConstraints = critical,CA:true,pathlen:0", "keyUsage = critical,keyCertSign,cRLSign",
                    "extendedKeyUsage = serverAuth"], digest="sha384")
    san = "subjectAltName = DNS:www.example.test,DNS:example.test,IP:10.0.2.2,IP:127.0.0.1"
    p.cert("leaf_a", "p256", "www.example.test", int_a, LEAF + [san], digest="sha384")
    # B: P-384 root -> RSA-3072 intermediate -> RSA leaves (PSS / sha512 / PSS-384)
    root_b = p.cert("root_b", "p384", "ojjyOS Test Root B", None, CA, digest="sha384")
    int_b = p.cert("int_b", "rsa3072", "ojjyOS Test Intermediate B", root_b, CA, digest="sha384")
    p.cert("leaf_b", "rsa2048", "rsa.example.test", int_b, LEAF + ["subjectAltName = DNS:rsa.example.test"], pss=True)
    p.cert("leaf_b512", "rsa2048", "sha512.example.test", int_b,
           LEAF + ["subjectAltName = DNS:sha512.example.test"], digest="sha512")
    p.cert("leaf_b384", "rsa2048", "pss384.example.test", int_b,
           LEAF + ["subjectAltName = DNS:pss384.example.test"], digest="sha384", pss=True)
    # C: P-256 intermediate signing with SHA-384 (hash longer than the curve)
    int_c = p.cert("int_c", "p256", "ojjyOS Test Intermediate C", root_a, CA)
    p.cert("leaf_c", "p384", "p256.example.test", int_c, LEAF + ["subjectAltName = DNS:p256.example.test"],
           digest="sha384")
    # Failure cases under A
    p.cert("leaf_expired", "p256", "www.example.test", int_a, LEAF + [san], digest="sha384",
           not_before="20200101000000Z", not_after="20210101000000Z")
    p.cert("leaf_future", "p256", "www.example.test", int_a, LEAF + [san], digest="sha384",
           not_before="20300101000000Z", not_after="20310101000000Z")
    p.cert("leaf_gentime", "p256", "www.example.test", int_a, LEAF + [san], digest="sha384",
           not_after="20510101000000Z")
    p.cert("leaf_wild", "p256", "*.wild.example.test", int_a,
           LEAF + ["subjectAltName = DNS:*.wild.example.test"], digest="sha384")
    p.cert("leaf_nosan", "p256", "www.example.test", int_a, LEAF, digest="sha384")
    p.cert("leaf_client", "p256", "www.example.test", int_a,
           ["basicConstraints = critical,CA:false", "keyUsage = critical,digitalSignature",
            "extendedKeyUsage = clientAuth", san], digest="sha384")
    p.cert("leaf_crit", "p256", "www.example.test", int_a, LEAF + [san, "1.2.3.4 = critical,DER:05:00"],
           digest="sha384")
    p.cert("leaf_noncrit", "p256", "www.example.test", int_a, LEAF + [san, "1.2.3.4 = DER:05:00"],
           digest="sha384")
    int_noca = p.cert("int_noca", "p256", "ojjyOS Test Not A CA", root_a,
                      ["basicConstraints = critical,CA:false", "keyUsage = critical,keyCertSign"])
    p.cert("leaf_noca", "p256", "www.example.test", int_noca, LEAF + [san])
    int_nokcs = p.cert("int_nokcs", "p256", "ojjyOS Test No CertSign", root_a,
                       ["basicConstraints = critical,CA:true", "keyUsage = critical,digitalSignature"])
    p.cert("leaf_nokcs", "p256", "www.example.test", int_nokcs, LEAF + [san])
    int_a2 = p.cert("int_a2", "p256", "ojjyOS Test Intermediate A2", int_a, CA, digest="sha384")
    p.cert("leaf_deep", "p256", "www.example.test", int_a2, LEAF + [san])
    int_nc = p.cert("int_nc", "p256", "ojjyOS Test Constrained", root_a,
                    CA + ["nameConstraints = critical,permitted;DNS:allowed.test,permitted;IP:10.0.0.0/255.0.0.0,"
                          "excluded;DNS:bad.allowed.test"])
    p.cert("leaf_nc_ok", "p256", "a.allowed.test", int_nc,
           LEAF + ["subjectAltName = DNS:a.allowed.test,IP:10.0.2.2"])
    p.cert("leaf_nc_dns", "p256", "evil.test", int_nc, LEAF + ["subjectAltName = DNS:a.allowed.test,DNS:evil.test"])
    p.cert("leaf_nc_ip", "p256", "b.allowed.test", int_nc,
           LEAF + ["subjectAltName = DNS:b.allowed.test,IP:192.168.1.1"])
    p.cert("leaf_nc_excl", "p256", "x.bad.allowed.test", int_nc, LEAF + ["subjectAltName = DNS:x.bad.allowed.test"])
    int_eku = p.cert("int_eku", "p256", "ojjyOS Test Client CA", root_a,
                     CA + ["extendedKeyUsage = clientAuth"])
    p.cert("leaf_eku", "p256", "www.example.test", int_eku, LEAF + [san])
    root_x = p.cert("root_x", "p256", "ojjyOS Untrusted Root", None, CA)
    p.cert("leaf_untrusted", "p256", "www.example.test", root_x, LEAF + [san])
    p.cert("leaf_self", "p256", "www.example.test", None, LEAF + [san])
    p.cert("leaf_rsa1024", "rsa1024", "weak.example.test", int_b, LEAF + ["subjectAltName = DNS:weak.example.test"])
    return p
