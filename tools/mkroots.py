#!/usr/bin/env python3
"""Generate kernel/src/crypto/roots_data.c: the built-in TLS trust anchors.

ojjyOS keeps a curated subset of the public web roots (the CAs behind
nearly every HTTPS site) and stores only what verification needs: each
root's subject Name DER and SubjectPublicKeyInfo DER. Root certificates
are public data published by the CAs themselves; the source used here is
the macOS system root store, or any PEM bundle given with --pem (for
example Mozilla's CCADB-derived cacert.pem on Linux).

    python3 -I tools/mkroots.py > kernel/src/crypto/roots_data.c
    python3 -I tools/mkroots.py --pem /etc/ssl/certs/ca-certificates.crt > ...

A wanted root that is missing or expired is reported on stderr and left
out. Created by Jonas Lee.
"""
import argparse
import base64
import calendar
import datetime
import hashlib
import re
import subprocess
import sys
import time

MACOS_ROOTS = "/System/Library/Keychains/SystemRootCertificates.keychain"

# Label = subject CN, or the OU for the GlobalSign roots that all share
# CN "GlobalSign" (and for roots without a CN).
WANTED = [
    "ISRG Root X1", "ISRG Root X2",                                       # Let's Encrypt
    "DigiCert Global Root CA", "DigiCert Global Root G2", "DigiCert Global Root G3",
    "DigiCert High Assurance EV Root CA", "DigiCert Assured ID Root CA",
    "DigiCert TLS RSA4096 Root G5", "DigiCert TLS ECC P384 Root G5",
    "QuoVadis Root CA 2 G3",
    "GlobalSign Root CA", "GlobalSign Root CA - R3", "GlobalSign Root CA - R6",
    "GlobalSign ECC Root CA - R4", "GlobalSign ECC Root CA - R5",
    "GlobalSign Root R46", "GlobalSign Root E46",
    "GTS Root R1", "GTS Root R2", "GTS Root R3", "GTS Root R4",            # Google Trust Services
    "Amazon Root CA 1", "Amazon Root CA 2", "Amazon Root CA 3", "Amazon Root CA 4",
    "Starfield Services Root Certificate Authority - G2",
    "Starfield Root Certificate Authority - G2", "Go Daddy Root Certificate Authority - G2",
    "USERTrust RSA Certification Authority", "USERTrust ECC Certification Authority",   # Sectigo
    "Sectigo Public Server Authentication Root R46", "Sectigo Public Server Authentication Root E46",
    "AAA Certificate Services", "COMODO RSA Certification Authority", "COMODO ECC Certification Authority",
    "Microsoft RSA Root Certificate Authority 2017", "Microsoft ECC Root Certificate Authority 2017",
    "SSL.com Root Certification Authority RSA", "SSL.com Root Certification Authority ECC",
    "SSL.com TLS RSA Root CA 2022", "SSL.com TLS ECC Root CA 2022",
    "SSL.com EV Root Certification Authority RSA R2",
    "Certum Trusted Network CA", "Certum Trusted Network CA 2",
    "Actalis Authentication Root CA", "HARICA TLS RSA Root CA 2021", "HARICA TLS ECC Root CA 2021",
    "Buypass Class 2 Root CA", "IdenTrust Commercial Root CA 1",
    "Certainly Root R1", "Certainly Root E1",                             # Fastly
]


def tlv(data, i):
    """(tag, value_start, value_end, next) of the DER element at i."""
    tag, length, j = data[i], data[i + 1], i + 2
    if length & 0x80:
        n = length & 0x7F
        length = int.from_bytes(data[j:j + n], "big")
        j += n
    return tag, j, j + length


def children(data, start, end):
    i = start
    while i < end:
        tag, vs, ve = tlv(data, i)
        yield tag, i, vs, ve
        i = ve


def attrs(name_der):
    """{oid_last_byte: text} for id-at attributes of a Name."""
    out = {}
    _, vs, ve = tlv(name_der, 0)
    for _, _, svs, sve in children(name_der, vs, ve):              # SET
        for _, _, avs, ave in children(name_der, svs, sve):        # AttributeTypeAndValue
            items = list(children(name_der, avs, ave))
            oid = name_der[items[0][2]:items[0][3]]
            value = name_der[items[1][2]:items[1][3]]
            if len(oid) == 3 and oid[:2] == b"\x55\x04" and oid[2] not in out:
                out[oid[2]] = value.decode("utf-8", "replace")
    return out


def parse_time(tag, text):
    text = text.decode()
    if tag == 0x17:
        year = int(text[:2])
        text = str(year + (1900 if year >= 50 else 2000)) + text[2:]
    return calendar.timegm(time.strptime(text, "%Y%m%d%H%M%SZ"))


def parse_cert(der):
    _, vs, _ = tlv(der, 0)
    _, tvs, tve = tlv(der, vs)
    fields = list(children(der, tvs, tve))
    if fields[0][0] == 0xA0:
        fields = fields[1:]
    # serial, signature, issuer, validity, subject, spki
    _, _, vvs, vve = fields[3]
    times = list(children(der, vvs, vve))
    not_after = parse_time(times[1][0], der[times[1][2]:times[1][3]])
    subject = der[fields[4][1]:fields[4][3]]
    spki = der[fields[5][1]:fields[5][3]]
    a = attrs(subject)
    cn, ou = a.get(3), a.get(11)
    label = ou if (cn in (None, "GlobalSign") and ou) else (cn or ou or a.get(10, "?"))
    return {"label": label, "subject": subject, "spki": spki, "not_after": not_after,
            "sha256": hashlib.sha256(der).hexdigest()}


def c_array(name, data):
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return f"static const uint8_t {name}[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"


def main():
    ap = argparse.ArgumentParser(description="Generate kernel/src/crypto/roots_data.c")
    ap.add_argument("--pem", help="PEM bundle instead of the macOS system roots")
    args = ap.parse_args()
    if args.pem:
        source = args.pem
        pem = open(args.pem, encoding="ascii", errors="replace").read()
    else:
        source = "macOS " + MACOS_ROOTS
        pem = subprocess.run(["security", "find-certificate", "-a", "-p", MACOS_ROOTS],
                             capture_output=True, check=True, text=True).stdout
    certs = []
    for block in re.findall(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", pem, re.S):
        try:
            certs.append(parse_cert(base64.b64decode("".join(block.split()))))
        except (IndexError, ValueError):
            print("mkroots: skipped an unparseable certificate", file=sys.stderr)
    now = time.time()
    chosen, seen = [], set()
    for label in WANTED:
        found = [c for c in certs if c["label"] == label]
        if not found:
            print(f"mkroots: wanted root not in source: {label}", file=sys.stderr)
        for c in found:
            if c["not_after"] < now:
                print(f"mkroots: expired, left out: {label}", file=sys.stderr)
            elif c["spki"] + c["subject"] not in seen:
                seen.add(c["spki"] + c["subject"])
                chosen.append(c)
    today = datetime.date.today().isoformat()
    out = ["/*\n * ojjyOS v3 - Built-in TLS trust anchors (generated - do not edit)\n *\n"
           f" * {len(chosen)} public web roots, subject DER + SubjectPublicKeyInfo only.\n"
           f" * Generated {today} by tools/mkroots.py from {source}.\n"
           " * Regenerate: python3 -I tools/mkroots.py > kernel/src/crypto/roots_data.c\n"
           " * Root certificates are public data published by their CAs (THIRD_PARTY.md).\n *\n"
           " * ojjyOS - Created by Jonas Lee\n */\n", '#include "crypto/x509.h"\n\n']
    table = []
    for i, c in enumerate(chosen):
        expires = datetime.datetime.fromtimestamp(c["not_after"], datetime.timezone.utc).date().isoformat()
        out.append(f"/* {c['label']} (expires {expires})\n * SHA-256 {c['sha256']} */\n")
        out.append(c_array(f"root{i}_subject", c["subject"]))
        out.append(c_array(f"root{i}_spki", c["spki"]))
        label = c["label"].replace("\\", "\\\\").replace('"', '\\"')
        table.append(f"    {{root{i}_subject, sizeof(root{i}_subject), root{i}_spki, sizeof(root{i}_spki), \"{label}\"}},\n")
    out.append("\nconst X509Root x509_builtin_roots[] = {\n" + "".join(table) + "};\n")
    out.append("const size_t x509_builtin_root_count = sizeof(x509_builtin_roots) / sizeof(x509_builtin_roots[0]);\n")
    sys.stdout.write("".join(out))
    print(f"mkroots: {len(chosen)} roots", file=sys.stderr)


if __name__ == "__main__":
    main()
