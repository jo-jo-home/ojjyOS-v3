# NET_WEB_PLAN.md — Track N: secure HTTPS, clock, web images, downloads

Track N of `PARALLEL_PLAN.md` (read that first: worktree, branch, file
ownership, publishing rules). Worktree `ojjyOS-kernel/ojjyOS-v3-net`,
branch `net-web`. Another AI works on the desktop (Track D) at the same
time in `ojjyOS-v3-ui` — never edit its files.

Goal in one sentence: make the browser trustworthy and complete — HTTPS
that actually proves the server's identity, a correct clock, pictures on web
pages, file downloads, and a nicer Web app.

Order: N1 -> N2 -> N3 -> N4 -> N5 -> N6 -> N7 -> N8. Commit and publish to
`main` after each green step. Log each step at the bottom of this file.

---

## Current state (verified 2026-10-18 by reading the code)

- `kernel/src/net/tls.c` (520 lines): TLS 1.3 client. ClientHello offers
  suites AES-128-GCM-SHA256 and ChaCha20-Poly1305-SHA256, X25519 key share,
  signature_algorithms `0x0403` (ecdsa_secp256r1_sha256), `0x0804`
  (rsa_pss_rsae_sha256), `0x0401` (rsa_pkcs1_sha256) — line ~197.
  `handle_certificate()` (~284) keeps only the first (leaf) certificate in
  `c->cert` if it fits, and **never validates it**. In the record loop
  (~383) `HS_CERT_VERIFY` just sets `c->stage = 4` — **the signature is
  never checked**. The Finished MAC is checked (that only proves the peer
  knows the handshake keys, which a man-in-the-middle also does).
- `kernel/src/crypto/`: `sha256.c`, `x25519.c`, `aes_gcm.c`,
  `chacha_poly.c`, `crypto.h`. No SHA-384/512, no big integers, no RSA, no
  P-256/P-384, no ASN.1.
- `kernel/src/web/http.c` (HTTP/1.1, chunked, redirects), `url.c`,
  `html.c` (one-pass layout; `<img>` renders `[alt]` text at ~691).
- `kernel/src/ui/browser.c` (880 lines): toolbar, address bar, page view,
  history, forms, dark mode; Inter text via `WebMetrics`.
- `kernel/src/image/jpeg.c`: baseline JPEG only (progressive rejected),
  output 0x00RRGGBB.
- Clock: `rtc_read_time()` (`drivers/rtc.c` on x86, `arch/aarch64/pl031.c`
  on QEMU ARM, EFI boot time + counter on Apple VZ). No NTP.
- Network: `net/net.h` has UDP (`net_udp_bind/send`), DNS resolver, DHCP,
  ICMP; `net/tcp.c` TCP.
- Tests: `tests/test_crypto.c` (+ `gen_crypto_vectors.py`,
  `crypto_vectors.h`), `test_net.c`, `test_web.c`, `test_jpeg.c`,
  `tls_probe.c`, `web_server.py`, live `qemu_net.py`, `qemu_browser.py`,
  `qemu_network_ui.py`.
- The About/limits text and docs currently say HTTPS is "encrypted, identity
  not verified" — keep saying that until N3 lands, then update it.

---

## N1 — Crypto primitives (host-tested first, no kernel wiring yet)

New files in `kernel/src/crypto/` (declare in `crypto.h`):

1. `sha512.c`: SHA-512 and SHA-384 (needed for P-384 and sha384WithRSA
   certificates, which Let's Encrypt and many CAs use).
2. `bignum.c`: fixed-size unsigned big integers (up to 4096-bit, plus room
   for products) with: compare, add/sub, Montgomery multiplication, modular
   exponentiation (for RSA, public exponents are small — square-and-
   multiply is fine), modular inverse (for ECDSA). No heap: fixed arrays on
   a static scratch or caller buffers (kernel stack is small — keep big
   temporaries static). Constant time is not required for VERIFICATION
   (public data), but never read out of bounds.
3. `rsa.c`: `rsa_verify_pkcs1(n, e, sig, hash_alg, digest)` (PKCS#1 v1.5,
   SHA-256/384/512 DigestInfo) and `rsa_verify_pss(n, e, sig, hash_alg,
   digest, salt_len)` (MGF1). Modulus 2048–4096 bits.
4. `ecdsa.c`: verification on P-256 (SHA-256) and P-384 (SHA-384):
   affine/Jacobian point arithmetic over the field primes, point-on-curve
   check for the public key, r/s range checks, `u1*G + u2*Q`.

Tests (`tests/test_crypto.c` or a new `tests/test_pubkey.c`, add to
`scripts/test.sh`): generate vectors with Python (`cryptography` package if
installed — check with `python3 -c "import cryptography"`; otherwise use
the official NIST/Wycheproof vectors saved as a header) for: SHA-384/512
known answers; RSA-2048/3072/4096 PKCS#1 and PSS valid + one-bit-flipped
signature + wrong key; ECDSA P-256/P-384 valid + flipped r/s + r=0 + s>=n +
point not on curve. Run under ASan/UBSan.

## N2 — ASN.1 DER + X.509 parser

`kernel/src/crypto/x509.c/h`:
- Strict DER reader (tag, definite length, nesting depth limit, never past
  the buffer).
- Parse: tbsCertificate bytes (for signature input), version, serial,
  signature algorithm OID, issuer and subject (raw DER for chaining + CN for
  display), validity (UTCTime + GeneralizedTime -> Unix seconds),
  subjectPublicKeyInfo (rsaEncryption n/e; id-ecPublicKey with
  prime256v1/secp384r1 point), extensions: basicConstraints (CA flag,
  pathLen), keyUsage (keyCertSign), extendedKeyUsage (serverAuth),
  subjectAltName dNSName list; reject unknown CRITICAL extensions.
- Supported signature algorithms: sha256/384/512WithRSAEncryption,
  RSASSA-PSS (sha256/384), ecdsa-with-SHA256/SHA384.

Tests `tests/test_x509.c`: real certificates as test vectors (generate a
small CA hierarchy with Python `cryptography` or `openssl` into
`tests/x509_vectors.h`: RSA root -> ECDSA intermediate -> leaf with SAN,
plus expired, not-yet-valid, wrong SAN, wildcard SAN, CA:false
intermediate, pathLen exceeded). Plus every truncation and 4000 random
corruptions of a real certificate (must never crash; ASan clean).

## N3 — Certificate chain + CertificateVerify in TLS (the security fix)

1. Root store: `tools/mkroots.py` generates `kernel/src/crypto/roots_data.c`
   from the macOS system roots (`security find-certificate -a -p
   /System/Library/Keychains/SystemRootCertificates.keychain`) — keep a
   curated subset (≈30–60 roots that cover the web: ISRG Root X1/X2,
   DigiCert Global Root G2/G3/CA, GlobalSign R3/R6/ECC, Amazon Root CA 1–4,
   Google Trust Services GTS R1–R4, Sectigo/USERTrust RSA+ECC, Baltimore
   CyberTrust if still present, Microsoft RSA/ECC 2017, Starfield/GoDaddy G2,
   Entrust G2, Certum, etc.) to keep the kernel small; store only subject
   DER + public key. Commit the generator AND the generated file (roots are
   public data). Document how to regenerate.
2. `tls.c`: keep ALL certificates of the Certificate message (bounded: e.g.
   up to 6 certificates / 16 KiB each in a static per-connection area —
   watch BSS: `TlsConn` instances are few; measure with `llvm-size`).
3. Verify: leaf SAN matches the host (exact, or one left-most `*` label;
   no IP SANs needed unless the URL is an IP), validity against the clock
   (N4 makes it accurate; until then allow ±48 h skew and log it), chain to
   a root by issuer/subject + signature, CA flags/pathLen/keyUsage/EKU.
4. CertificateVerify: check the server's signature over
   `64 x 0x20 || "TLS 1.3, server CertificateVerify" || 0x00 ||
   transcript_hash(up to Certificate)` with the leaf key, for the offered
   schemes (add `0x0503` ecdsa_secp384r1_sha384 and `0x0805`
   rsa_pss_rsae_sha384 to signature_algorithms). Reject PKCS#1 v1.5 in
   CertificateVerify (TLS 1.3 forbids it; it is fine inside certificates).
5. Failure handling: the connection fails with a clear reason
   ("certificate expired", "name mismatch: example.com vs other.org",
   "unknown issuer", "bad signature"). The browser shows a full-page
   warning (Tahoe style, no scary red; blue theme) "This connection is not
   private" with the reason, a "Go Back" button and a small "Visit anyway"
   link that sets a per-host, per-session exception. Address bar shows a
   lock icon for verified HTTPS, a crossed lock for exceptions/HTTP.
   Terminal `curl https://...` prints the reason and refuses unless
   `curl -k`.
6. Performance: RSA-2048 verify and P-256 verify under TCG must not freeze
   the UI for long — measure (log `[TLS] verify N ms`). If > ~300 ms under
   TCG, verify incrementally across main-loop turns (the TLS state machine
   is already incremental).

Tests: extend `test_net.c`/new `test_tls_verify.c` with recorded handshakes
or unit tests of the verify function with the N2 vectors; live
`tests/qemu_https.py` (new): real sites via the VM's NAT network —
https://example.com, https://en.wikipedia.org, https://www.apple.com,
https://letsencrypt.org must load with a lock; https://expired.badssl.com,
https://wrong.host.badssl.com, https://self-signed.badssl.com,
https://untrusted-root.badssl.com must show the warning with the right
reason. (If the host has no internet, record that and rely on host tests +
`web_server.py` with a locally generated CA.)

## N4 — Wall clock + NTP

- New `kernel/src/clock.c/h`: `clock_now_unix()` = firmware/RTC time at
  boot + monotonic timer + NTP correction offset. Route
  `rtc_read_time()` through it on every platform without changing its
  signature (so the menu bar clock and Auto appearance just work).
- SNTP client over `net_udp_*`: query `time.apple.com` (fallback
  `pool.ntp.org`) after DHCP succeeds and every 6 hours; validate mode,
  stratum, origin timestamp, and reject absurd jumps (> 10 years) and
  replies to a different transaction. Log `[CLOCK] NTP offset +X ms from
  <server>`.
- Needs one tiny hook to start after DHCP: prefer to do it inside `net.c`
  (your file) when the lease is acquired, so `main.c` is not touched.
- Terminal `date` shows the corrected time; `ntp` command forces a sync.
- Tests: `tests/test_clock.c` (packet parse/validate, offset math,
  2036 era rollover), live check in `qemu_net.py`.

## N5 — Images: PNG and progressive JPEG behind one API

- `kernel/src/image/inflate.c` (zlib/DEFLATE: stored, fixed and dynamic
  Huffman; Adler-32 check; output limit), `png.c` (signature, chunks with
  CRC, IHDR, PLTE, tRNS, IDAT, IEND; colour types 0/2/3/4/6, bit depths
  1–16 (16 -> 8), all five filters, Adam7 interlace; dimension and memory
  limits).
- `jpeg.c`: add progressive (SOF2: spectral selection + successive
  approximation, DC/AC first/refine scans, coefficient buffer) — keep the
  existing baseline path and API working (wallpapers depend on it; the
  wallpaper test `tests/qemu_wallpapers.py` belongs to Track D but must
  still pass — run it).
- `kernel/src/image/image.h/c`: `image_info()` / `image_decode()` as agreed
  in `PARALLEL_PLAN.md` §3 (0xAARRGGBB output, dispatch by signature, GIF
  first frame optional).
- Tests: `tests/test_png.c` with PngSuite-style vectors generated by
  Pillow (all colour types/depths/interlace) compared pixel-exact;
  inflate vectors from Python `zlib`; progressive JPEG vectors from Pillow
  (`progressive=True`, tolerance like the baseline test); every truncation
  + 4000 corruptions each, ASan/UBSan clean.

## N6 — `<img>` on web pages

- `html.c`: `<img src width height alt>` becomes a layout box (use
  width/height attributes when present; otherwise a placeholder size, then
  re-layout when the image's real size is known); `srcset` may be ignored;
  scale down to the content width keeping aspect; inline vs block per
  context. Keep `[alt]` fallback when decoding fails.
- `browser.c`: after the page loads, fetch images sequentially (one HTTP
  connection at a time is fine) with limits: max 24 images per page,
  max 2 MiB compressed / 16 MP decoded each, total decoded budget (e.g.
  64 MiB, from `pmm_alloc_bytes` buffers reused per page — there is no
  free; design a simple arena that is reset on navigation). Draw with
  `gfx_blit_scaled`/`gfx_draw_rgba_scaled` (bilinear, alpha). Show a light
  placeholder while loading. Relative URLs resolve against the page URL.
- `test_web.c`: layout of images with/without sizes, re-layout, hit-testing
  of linked images. Live `qemu_browser.py`: a local page from
  `web_server.py` with PNG (alpha), baseline JPEG and progressive JPEG,
  screenshot-compared; plus one real site with images.

## N7 — Downloads

- When a response is not HTML/text (by Content-Type or
  Content-Disposition: attachment, or the link has the `download`
  attribute), save it to `/Users/<user>/Downloads/<name>` via VFS (create
  the folder; avoid overwriting: `name (2).ext`). Size limit = what RAMFS
  can hold (check free space first; fail clearly).
- Browser shows a small downloads popover/progress in the toolbar; when
  finished call `notifications_push("Downloaded <name>")` (Track D turns it
  into a banner).
- Terminal `curl -o <file> <url>` saves too.
- Tests: `web_server.py` serves a binary file; live test verifies the file
  exists with the right size and CRC via Terminal `ls`/a checksum command.

## N8 — Web app polish (browser.c only)

From `LIQUID_GLASS_APPS_PLAN.md` Phase A "Web": glass toolbar (use the
`ui/draw.h` helpers — if you need a new drawing helper from the compositor,
ask by adding a note in this plan's log; do NOT edit compositor.c),
lock/warning icon in the address bar, start page as icon tiles for
favourites (`ojjy:home` HTML in `scripts/mkfs.sh` belongs to Track D —
instead render the tiles natively in browser.c for `ojjy:home`, or leave a
note for Track D), loading bar polish, readable error pages, find-in-page
(Cmd+F) if time allows. Check light AND dark mode at 2x.

## Finish

- Update `BROWSER_APPS_PLAN.md`, `README.md` (HTTPS now verified; how to
  regenerate roots), `ROADMAP.md`, `TODO.md` (tick N items), `AGENTS.md`
  (your own dated LATEST block), the in-OS "What's new" text only via a
  note to Track D (it lives in `scripts/mkfs.sh`).
- Final verification on your branch rebased onto the latest `main`: both
  builds 0 warnings, host + sanitizer suites, TCG + HVF + Intel smokes
  (`tests/qemu_smoke.py --fresh-data [--protected]`), `qemu_net.py`,
  `qemu_browser.py`, `qemu_network_ui.py`, `qemu_https.py`, and the native
  app smoke if available (`scripts/test-vmapp.sh` / `tests/vz_smoke.py` —
  read them first; use private disks only).
- Plain-English summary for Jonas.

## Log

(append one entry per finished step: what, evidence directory, commit)
