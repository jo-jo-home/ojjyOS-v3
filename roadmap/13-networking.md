# 13 — Networking

Today (`02-current-state.md` §5): an in-kernel stack in `net/net.c`
(Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS) and `net/tcp.c` (8 TCP
connections), a TLS 1.3 client in `net/tls.c`, an HTTP/1.1 client in
`web/http.c`, used only by kernel-hosted apps. Track N owns X.509,
certificate verification, SNTP and TLS 1.2 in P0.

Target: a full dual-stack TCP/IP stack in the kernel (D-001) exposed through
BSD sockets; user-space libraries for TLS, HTTP and protocols; services for
configuration (`netconfigd`), Wi-Fi (`wifid`), Bluetooth (`bluetoothd`),
discovery (`mdnsd`).

Rules: every protocol parser has fuzz tests; every network-facing service
runs sandboxed (SEC-*); TLS certificate verification is never optional
outside explicit developer flags.

---

## P0 items owned by Track N

### NET-02 — HTTPS certificate verification (X.509 + chain + CertificateVerify)
- Phase: P0 · Size: L · Status: doing
- Depends: —
- Files: NET_WEB_PLAN.md N2–N3 (Track N), kernel/src/net/tls.c, kernel/src/crypto/x509*.c/h (new), kernel/src/crypto/roots_data.c (new), tools/mkroots.py (new), kernel/src/web/http.c/h, kernel/src/ui/browser*.c/h, kernel/src/ui/netcmd.c, tests/test_x509.c (new), tests/test_tls_verify.c (new), tests/qemu_https.py (new)

**Context.** TLS encrypts but does not verify server identity. Track N has
done N1 (SHA-384/512, RSA, ECDSA). This card mirrors N2 (ASN.1/X.509) and
N3 (chain building, root store, hostname checks, CertificateVerify).
(2026-10-27, board 27c8ad, worktree `ojjyOS-v3-net-02` / `net-02`.) The
card said `doing` since the roadmap was written, but nothing for N2/N3
existed: `tls.c` keeps only the leaf certificate and skips
CertificateVerify. N4 (TIM-04 SNTP clock) is done, so validity can be
checked against `clock_now_unix()`. No Track D session is active, so this
card also edits the browser files Track D used to own.
**Goal.** As specified in `NET_WEB_PLAN.md` N2–N3.
**Steps.**
- [ ] 1. Strict DER reader + X.509 parser in `kernel/src/crypto/x509.c/h` (new): tbs bytes, version, serial, signature algorithm (sha256/384/512WithRSA, RSASSA-PSS sha256/384/512, ecdsa-with-SHA256/384), issuer/subject raw DER + CN, validity (UTCTime/GeneralizedTime -> Unix), SPKI (RSA n/e, EC P-256/P-384 point), basicConstraints, keyUsage, extKeyUsage, SAN dNSName/iPAddress, nameConstraints, unknown critical extension flag; `x509_verify_signed_by(child, issuer_key)`. Files: x509.c/h, kernel/Makefile, scripts/test.sh, tests/gen_x509_vectors.py (new, OpenSSL CLI), tests/x509_vectors.h (generated), tests/test_x509.c (new). Test: field checks + signature checks for every algorithm + full truncation sweep + 4000 random corruptions, normal and ASan/UBSan. Trap: OIDs/constants from generated data or the RFC text, never memory; a separate file name for every generated key.
- [ ] 2. Chain verification in `kernel/src/crypto/x509_chain.c` (new): host-name match (RFC 6125: exact or one left-most `*` label, IP hosts against iPAddress SANs), validity with the clock (±48 h tolerance only while NTP has not synced, logged), path building through the sent certificates in any order to a trusted root (issuer/subject + signature), CA flag, pathLen, keyCertSign, serverAuth EKU, dNSName name constraints, depth limit; clear reasons ("certificate expired", "name mismatch: a vs b", "unknown issuer", "bad signature", …). Test: tests/test_x509.c chain cases (good, out-of-order, expired, not yet valid, wrong SAN, wildcard rules, CA:false intermediate, pathLen exceeded, no keyCertSign, clientAuth-only EKU, unknown critical extension, unknown issuer, name constraints). Trap: a leaf that is itself self-signed is "unknown issuer", never trusted.
- [ ] 3. Root store: `tools/mkroots.py` (new) builds `kernel/src/crypto/roots_data.c` (subject DER + SubjectPublicKeyInfo only) from a curated list of the macOS system roots; THIRD_PARTY.md entry; regeneration documented. Test: tests/test_x509.c parses every built-in root key and finds ISRG Root X1/X2 and the other must-have roots; `tools/check_third_party.py`. Trap: watch kernel image size (`llvm-size`) — rodata only, no BSS.
- [ ] 4. TLS: `tls.c` checks the whole Certificate message (bounded count/size) at arrival, keeps the leaf key, verifies CertificateVerify over `64×0x20 || "TLS 1.3, server CertificateVerify" || 0 || transcript hash` (rsa_pss_rsae_sha256/384/512, ecdsa_secp256r1_sha256, ecdsa_secp384r1_sha384; PKCS#1 v1.5 rejected), stops advertising ed25519; per-connection "allow untrusted" option keeps the handshake (and CertificateVerify) but records the certificate problem; `verified`, `cert_error`, subject/issuer CN exposed. Files: tls.c/h, tests/tls_probe.c, tests/test_tls_verify.c (new), tests/test_tls_local.py (new). Test: CertificateVerify vectors from OpenSSL (valid, flipped, wrong transcript, PKCS#1 refused) + local handshakes against Python's TLS 1.3 server with generated chains (RSA leaf, ECDSA P-256 and P-384 leaves, expired, wrong host, unknown issuer, allow-untrusted). Trap: TlsConn lives in the page-allocated pool — no new big static buffers.
- [ ] 5. Performance: log `[TLS] verify N ms` (chain + CertificateVerify); measure on host and QEMU TCG/HVF. Test: numbers recorded in the card log; if one handshake's verification is > ~300 ms under TCG, split it across tls_poll turns. Trap: never quote TCG time as user speed.
- [ ] 6. HTTP + browser: `http.c` exposes verified/cert-error and asks a browser hook for per-host exceptions; the browser shows a blue Liquid Glass "This connection is not private" page with the reason, Go Back and a small "Visit anyway" link (per-host, per-session exception), a lock for verified HTTPS, a crossed lock for exceptions, honest page-info and Settings text. Files: web/http.c/h, ui/browser.c, ui/browser_pages.c, ui/browser_ui.c, ui/browser_int.h, tests/test_web.c stubs. Test: both builds + live in step 8.
- [ ] 7. Terminal: `curl https://…` prints the certificate reason and refuses; `curl -k` connects and warns. Files: ui/netcmd.c. Test: live in step 8.
- [ ] 8. Live: new `tests/qemu_https.py` (local self-signed server -> warning page with "unknown issuer", Visit anyway -> page + crossed lock, curl refuse / curl -k; with --internet: example.com, en.wikipedia.org, www.apple.com, letsencrypt.org load verified; expired/wrong.host/self-signed/untrusted-root.badssl.com show the right reason); `tests/qemu_browser.py` HTTPS step updated; smoke TCG + HVF; screenshots looked at. Evidence `build/net-02-*`.
- [ ] 9. Docs + delivery: NET_WEB_PLAN.md N2/N3 log, tls.h header, About/Settings/README/APPS.md "identity not verified" texts, What's New, AGENTS.md LATEST, card Log; fast-forward main; installed image rebuild + ready-to-test note.
**Done when.**
- [ ] The DER/X.509 parser is host-tested with real OpenSSL-generated certificates and survives the truncation sweep + 4000 corruptions under ASan/UBSan.
- [ ] Chain verification rejects expired, not-yet-valid, wrong-name, unknown-issuer, CA:false, pathLen and bad-signature chains with clear reasons (host tests).
- [ ] TLS verifies CertificateVerify with the leaf key and refuses PKCS#1 v1.5 there (host tests + local handshakes).
- [ ] The browser shows a real lock for verified HTTPS, and a blue "This connection is not private" page with the reason, Go Back and Visit anyway (per-host, per-session; crossed lock) for bad certificates (live).
- [ ] Terminal `curl https://…` prints the reason and refuses unless `curl -k` (live).
- [ ] Real sites load with a lock and the badssl.com cases show the right reason under HVF (or the lack of internet is recorded and host tests cover it).
- [ ] Verification time is logged and stays under ~300 ms per handshake under TCG (or is split across poll turns).
- [ ] Definition of Done: both dev builds 0 warnings, scripts/test.sh + sanitizer pass, smoke TCG + HVF pass, evidence in `build/net-02-*`.
**Tests.** As specified there: tests/test_x509.c, tests/test_tls_verify.c,
tests/test_tls_local.py (host), tests/qemu_https.py + qemu_browser.py (live).
**Pitfalls.** —
**Refs.** RFC 5280, RFC 8446 §4.4.3, RFC 6125.

### NET-03 — TLS 1.2 client (sites that do not speak TLS 1.3)
- Phase: P0 · Size: M · Status: todo
- Depends: NET-02
- Files: kernel/src/net/tls.c (Track N)

**Context.** craigslist.org closes the handshake; it needs TLS 1.2 or a
HelloRetryRequest path.
**Goal.** HelloRetryRequest handling in 1.3, and a TLS 1.2 client with
ECDHE-ECDSA/RSA + AES-GCM/ChaCha20 suites only (no CBC, no RSA key
exchange), extended master secret, secure renegotiation refusal.
**Done when.** craigslist.org and the 32-site probe (`tests/tls_probe.c`)
all connect with verified certificates.
**Tests.** `tls_probe.c`; host tests with RFC test vectors for the PRF.
**Pitfalls.** Downgrade protection sentinel values.
**Refs.** RFC 5246, RFC 7627, RFC 8446 §4.1.3.

---

## Stack foundations

### NET-01 — Network stack architecture v2
- Phase: P2 · Size: L · Status: todo
- Depends: SCH-05, THR-03, MEM-06
- Files: kernel/src/net/net.c → kernel/net/

**Context.** One global interface, packet handling inline in the main loop
(moved to a thread by SCH-05), fixed buffers.
**Goal.** Interface objects (`netif`: Ethernet, loopback, Wi-Fi, VPN,
USB tethering) with statistics; packet buffers (`pkbuf`) with headroom
and scatter-gather; a routing table with default routes per interface and
metrics; ARP cache with timeouts and gratuitous ARP; a loopback interface
(127.0.0.1, ::1); clear layering (link → IP → transport) and locks for SMP.
**Steps.**
1. Introduce `pkbuf` and `netif`; port virtio-net.
2. Routing table + loopback.
3. Move DHCP client out to `netconfigd` (NET-11) once sockets exist.
**Done when.** Two NICs (virtio + e1000 in QEMU) both get leases; traffic
routes by metric; loopback works.
**Tests.** `test_net.c` extended (routing, ARP aging); QEMU two-NIC test.
**Pitfalls.** Don't regress DHCP under VZ (MTU feature negotiation).
**Refs.** RFC 1122, RFC 826.

### NET-04 — DNS resolver service
- Phase: P5 · Size: M · Status: todo
- Depends: NET-10, SVC-01
- Files: services/resolverd/, lib/libc/netdb.c

**Context.** DNS lookups are done by the kernel stack for the browser.
**Goal.** `resolverd` with a cache honouring TTLs, A + AAAA + CNAME +
HTTPS/SVCB records, multiple servers with failover, search domains,
`/etc/hosts`, negative caching; `getaddrinfo` in libc talks to it.
**Done when.** Page loads use cached lookups (logged hit rate);
`nslookup` shows TTLs.
**Tests.** Host tests of the message parser (fuzzed); live test.
**Pitfalls.** Response validation (ID, question match) against spoofing.
**Refs.** RFC 1035, RFC 3596, RFC 9460.

**Log.** 2026-10-27 pre-service bugfix (board 6e210c): kernel dns_poll
previously marked a lookup failed immediately after sending query four.
It now preserves that query's reply window within the same four-query /
ten-second budget. `tests/test_net.c` proves delayed fourth-query success,
bounded timeout and expired-answer rejection; 52 checks normal/sanitized
plus existing 30000-frame fuzz pass (`build/bug-1006/`). BUG-1006's earlier
six external DNS failures did not reproduce on the unchanged image:
all six HTTP 200, DNS packets captured. This is not completion of NET-04's
future userspace service; Status remains todo and BUG-1006 stays open.

### NET-05 — TCP v2
- Phase: P5 · Size: L · Status: todo
- Depends: NET-01
- Files: kernel/src/net/tcp.c → kernel/net/tcp/

**Goal.** Complete state machine incl. listen/accept (server sockets),
simultaneous open/close, TIME_WAIT handling, window scaling, SACK,
timestamps, RTO estimation (RFC 6298), NewReno then CUBIC congestion
control, delayed ACKs, Nagle with `TCP_NODELAY`, keepalive, MSS/PMTU
discovery, thousands of connections (hash tables), SYN cookies.
**Done when.** Benchmark ≥ 2 Gbit/s to the host under VZ; 1,000
concurrent connections test passes; packet-loss test (QEMU netem) keeps
throughput reasonable.
**Tests.** Host tests driving the state machine with scripted packets
(loss, reordering, RST); live benchmark.
**Pitfalls.** Sequence number wraparound; RST validation (RFC 5961).
**Refs.** RFC 9293, 7323, 2018, 6298, 5681, 9438.

### NET-06 — UDP v2, broadcast and multicast
- Phase: P5 · Size: S · Status: todo
- Depends: NET-01
- Files: kernel/net/udp.c

**Goal.** Port tables, checksums, broadcast, multicast join/leave (IGMPv3
/ MLDv2), per-socket options.
**Done when.** mDNS (NET-13) works over it.
**Tests.** Host + live tests.
**Pitfalls.** —
**Refs.** RFC 768, RFC 3376.

### NET-07 — IPv6
- Phase: P5 · Size: L · Status: todo
- Depends: NET-01
- Files: kernel/net/ipv6/

**Goal.** Addresses (link-local, SLAAC with privacy extensions, DHCPv6
in netconfigd), NDP (neighbor/router discovery), ICMPv6, extension
headers (safe subset), fragmentation, dual-stack sockets, Happy
Eyeballs in the connect helper (NET-17).
**Done when.** Under QEMU with IPv6 user networking, `ping6` and an
IPv6-only website load.
**Tests.** Host tests of NDP and header parsing (fuzzed); QEMU test.
**Pitfalls.** Extension header loops/DoS.
**Refs.** RFC 8200, 4861, 4862, 8981, 8415, 8305.

### NET-08 — IPv4 completeness
- Phase: P5 · Size: S · Status: todo
- Depends: NET-01
- Files: kernel/net/ipv4.c

**Goal.** Fragment reassembly with limits, ICMP errors (port unreachable,
TTL exceeded) for traceroute, path MTU discovery, IP options rejected.
**Done when.** `traceroute` works (NET-80).
**Tests.** Host tests (overlapping fragments rejected).
**Pitfalls.** Fragment memory exhaustion.
**Refs.** RFC 791, 1191, 5722.

### NET-10 — BSD sockets API
- Phase: P2 · Size: M · Status: todo
- Depends: SYS-07, SYS-08, NET-01
- Files: kernel/net/socket.c, lib/libc/socket.c

**Goal.** `socket/bind/connect/listen/accept/send/recv/sendto/recvfrom/
shutdown/getsockopt/setsockopt/getsockname/getpeername` for AF_INET,
AF_INET6, AF_UNIX, non-blocking mode, `poll/select` via SYS-08.
**Done when.** A user-space `curl`-like program fetches a page; the
browser's network code moves to sockets.
**Tests.** Live tests with echo servers on the host.
**Pitfalls.** Kernel-hosted callers must keep working during migration.
**Refs.** POSIX sockets.

### NET-11 — Network configuration service (`netconfigd`)
- Phase: P5 · Size: M · Status: todo
- Depends: NET-10, SVC-01
- Files: services/netconfigd/

**Goal.** Owns interface configuration: DHCPv4 client (moved from the
kernel), DHCPv6/SLAAC, static IP/DNS, per-network profiles ("locations"),
proxies, link up/down events → notifications; IPC API for Settings >
Network and the menu bar.
**Done when.** Settings can switch DHCP ↔ static and changes apply
without reboot.
**Tests.** Live test.
**Pitfalls.** Keep the network up during the move (kernel DHCP fallback
until verified).
**Refs.** RFC 2131.

### NET-12 — Captive portal detection
- Phase: P6 · Size: S · Status: todo
- Depends: NET-11, NET-17
- Files: services/netconfigd/captive.c

**Goal.** Probe a known URL after joining a network; if redirected, open a
mini ojjyWeb window for the login page.
**Done when.** Simulated captive portal (local server) triggers the sheet.
**Tests.** Live test with `tests/web_server.py`.
**Pitfalls.** Use our own probe endpoint (configurable), not Apple's.
**Refs.** RFC 8910.

### NET-13 — mDNS / DNS-SD discovery ("Bonjour"-like)
- Phase: P6 · Size: M · Status: todo
- Depends: NET-06, SVC-01
- Files: services/mdnsd/

**Goal.** Multicast DNS responder (`<hostname>.local`) and service
browser (printers `_ipp._tcp`, shares `_smb._tcp`, other ojjyOS machines
`_ojjy._tcp`), used by printing, Finder's Network sidebar and AirDrop-like
sharing (SHL-40).
**Done when.** Two ojjyOS VMs see each other in Finder > Network.
**Tests.** Host tests of packet building/parsing (fuzzed); two-VM test.
**Pitfalls.** Name conflicts and probing rules.
**Refs.** RFC 6762, RFC 6763.

### NET-14 — Firewall
- Phase: P5 · Size: M · Status: todo
- Depends: NET-05, SEC-10
- Files: kernel/net/filter.c, services/firewalld

**Goal.** Stateful packet filter (connection tracking) with default-deny
inbound, application-based allow rules ("Allow incoming connections for
X?" prompt), stealth mode, rule list in Settings > Network > Firewall.
**Done when.** Inbound connections to a test server are blocked until the
user allows the app.
**Tests.** Live test with host-side connection attempts.
**Pitfalls.** Don't break DHCP/mDNS replies.
**Refs.** —

---

## User-space protocol libraries

### NET-15 — User-space TLS library (`libtls-ojjy`)
- Phase: P5 · Size: L · Status: todo
- Depends: NET-03, NET-10, USR-01
- Files: lib/libtls/ (from kernel/src/net/tls.c + crypto/)

**Goal.** TLS 1.2/1.3 client **and server** as a library: certificate
verification with the system trust store (NET-16), ALPN (h2, http/1.1),
SNI, session resumption/0-RTT off by default, OCSP stapling check,
client certificates; constant-time crypto shared with the kernel copy.
**Done when.** ojjyWeb, Mail, `curl` and `ojssh`-less services use it; a
local HTTPS test server built on it passes a TLS checker.
**Tests.** Host tests with RFC 8448 test vectors; interop against the
host's OpenSSL `s_server`/`s_client` in tests (tools only).
**Pitfalls.** Keep one crypto implementation (shared source).
**Refs.** RFC 8446, RFC 8448, RFC 7301.

### NET-16 — Trust store and certificate management
- Phase: P5 · Size: M · Status: todo
- Depends: NET-15, SEC-40
- Files: /System/Library/Security/roots.pem, services/keychaind

**Goal.** System root store (from a public CA list, licence recorded),
updates via packages, user-added roots (with admin auth), certificate
viewer in ojjyWeb and Keychain Access, revocation via OCSP/CRLite-like
lists later.
**Done when.** Adding a self-signed root lets a local HTTPS site load.
**Tests.** Live test.
**Pitfalls.** Distrust events (remove compromised CAs quickly).
**Refs.** CA/Browser Forum baseline requirements.

### NET-17 — HTTP client library (`libhttp`)
- Phase: P5 · Size: L · Status: todo
- Depends: NET-15, NET-04
- Files: lib/libhttp/ (from kernel/src/web/http.c)

**Goal.** HTTP/1.1 with keep-alive connection pools, pipelining off,
redirects, cookies (shared jar service), caching (RFC 9111) to disk,
compression (gzip, deflate, brotli later), proxies, Happy Eyeballs,
timeouts, progress callbacks, range requests (resume downloads).
**Done when.** ojjyWeb fetches through it; page loads reuse connections
(logged).
**Tests.** Host tests with a scripted fake server; live test vs
`tests/web_server.py`.
**Pitfalls.** Request smuggling: strict parsing of responses.
**Refs.** RFC 9110, 9111, 9112.

### NET-18 — HTTP/2
- Phase: P6 · Size: M · Status: todo
- Depends: NET-17
- Files: lib/libhttp/h2/

**Goal.** HPACK, streams, flow control, priorities (simple), server push
refused, ALPN negotiation.
**Done when.** Major sites load over h2 (logged protocol per request).
**Tests.** Host tests with RFC 7541 vectors; interop with public servers.
**Pitfalls.** HPACK table size attacks.
**Refs.** RFC 9113, RFC 7541.

### NET-19 — QUIC and HTTP/3
- Phase: P9 · Size: L · Status: todo
- Depends: NET-18, NET-06
- Files: lib/libquic/

**Goal.** QUIC v1 client (TLS 1.3 handshake inside QUIC, loss recovery,
congestion control), HTTP/3 with QPACK.
**Done when.** Sites advertising h3 load over QUIC.
**Tests.** Host tests with RFC 9001 vectors; interop.
**Pitfalls.** Large spec; split into cards before starting (size L+).
**Refs.** RFC 9000, 9001, 9002, 9114, 9204.

### NET-20 — WebSocket
- Phase: P6 · Size: S · Status: todo
- Depends: NET-17
- Files: lib/libhttp/websocket.c

**Goal.** RFC 6455 client (and server for local tools) with
permessage-deflate; needed by ojjyWeb JavaScript (WEB-*).
**Done when.** An echo test against a local server passes.
**Tests.** Host tests with frame vectors; fuzz.
**Pitfalls.** Masking rules.
**Refs.** RFC 6455, RFC 7692.

### NET-21 — Encrypted DNS (DoH/DoT)
- Phase: P6 · Size: S · Status: todo
- Depends: NET-04, NET-15
- Files: services/resolverd/doh.c

**Goal.** Optional DNS over HTTPS/TLS with selectable providers in
Settings > Network > DNS.
**Done when.** Lookups go over DoH when enabled (packet capture shows no
plaintext DNS).
**Tests.** Live test.
**Pitfalls.** Bootstrap the DoH server address.
**Refs.** RFC 8484, RFC 7858.

### NET-22 — Proxies
- Phase: P6 · Size: S · Status: todo
- Depends: NET-17
- Files: lib/libhttp/proxy.c

**Goal.** HTTP CONNECT and SOCKS5 proxies, PAC files (needs the JS engine
— WEB-20; until then manual proxy settings only).
**Done when.** Browsing through a local test proxy works.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** RFC 1928.

---

## Remote access and sharing

### NET-30 — SSH client and server (`ojssh`, `ojsshd`)
- Phase: P6 · Size: L · Status: todo
- Depends: NET-10, USR-01, SEC-41
- Files: apps-cli/ojssh/, services/ojsshd/

**Goal.** SSH-2 transport (curve25519-sha256, ed25519 host/user keys,
chacha20-poly1305@openssh-style AEAD and aes-gcm), user auth (public key,
password), channels (shell over PTY, exec, port forwarding), SFTP
subsystem, `known_hosts`, `authorized_keys`, Remote Login toggle in
Settings > Sharing.
**Done when.** `ssh` from the Mac into ojjyOS gives a shell; `ojssh` to
the Mac works; `scp`/SFTP copies files.
**Tests.** Interop tests against the host's OpenSSH (tool only).
**Pitfalls.** Original implementation only; strict key exchange
(Terrapin mitigation, RFC "strict kex").
**Refs.** RFC 4251–4254, RFC 8709, RFC 8731.

### NET-31 — Screen sharing (VNC/RFB server and client)
- Phase: P9 · Size: M · Status: todo
- Depends: NET-10, WS-20
- Files: services/screensharingd/, apps/ScreenSharing

**Goal.** RFB protocol server (view/control with password and TLS) and a
viewer app.
**Done when.** The Mac's Screen Sharing app connects and controls
ojjyOS.
**Tests.** Live test with a scripted RFB client.
**Pitfalls.** Security: off by default, require a strong password.
**Refs.** RFC 6143.

### NET-70 — SMB client (Windows/Mac file sharing)
- Phase: P6 · Size: L · Status: todo
- Depends: NET-10, NET-13, FS-30
- Files: lib/libsmb/, services/smbfsd/

**Goal.** SMB 2.1/3.x client with NTLMv2 and Kerberos-less auth, signing
and encryption, mounted as a user-space filesystem in Finder (Network
sidebar, "Connect to Server" sheet).
**Done when.** A macOS "File Sharing" folder mounts and files copy both
ways.
**Tests.** Interop with the Mac host's SMB server.
**Pitfalls.** Follow Microsoft's published open specifications only.
**Refs.** [MS-SMB2], [MS-NLMP].

### NET-71 — File sharing server (SMB)
- Phase: P9 · Size: M · Status: todo
- Depends: NET-70, NET-14
- Files: services/smbd/

**Goal.** Share folders from ojjyOS to Macs/PCs (Settings > Sharing).
**Done when.** Finder on the Mac mounts an ojjyOS share.
**Tests.** Interop test.
**Pitfalls.** Off by default; per-share permissions.
**Refs.** [MS-SMB2].

### NET-75 — Mail protocols library (IMAP, SMTP, POP3)
- Phase: P6 · Size: M · Status: todo
- Depends: NET-15
- Files: lib/libmail/

**Goal.** IMAP4rev2 (IDLE, UID, CONDSTORE), SMTP submission with AUTH
(PLAIN/LOGIN, OAuth2 later), POP3, MIME parsing/building (multipart,
encodings, charsets), for the Mail app (APP-MAIL-01).
**Done when.** Reads and sends mail with a test IMAP/SMTP server.
**Tests.** Host tests with recorded sessions; MIME fuzzing.
**Pitfalls.** MIME is a parser-bug magnet: fuzz heavily.
**Refs.** RFC 9051, 6409, 1939, 2045–2049.

### NET-76 — CalDAV and CardDAV
- Phase: P6 · Size: M · Status: todo
- Depends: NET-17
- Files: lib/libdav/

**Goal.** Sync calendars and contacts with standard servers (iCloud,
Fastmail, Nextcloud), iCalendar and vCard parsing.
**Done when.** Calendar shows events from a CalDAV test server.
**Tests.** Host tests (iCalendar/vCard fuzzing).
**Pitfalls.** Recurrence rules (RRULE) complexity.
**Refs.** RFC 4791, 6352, 5545, 6350.

---

## Wireless

### NET-40 — Wi-Fi management (`wifid`, supplicant, UI)
- Phase: P9 · Size: L · Status: todo
- Depends: NET-11, SVC-01
- Files: services/wifid/

**Goal.** Driver-independent 802.11 management: scanning, network list
with signal, join (open, WPA2-PSK, WPA3-SAE), 4-way handshake, roaming,
known networks with priorities stored in the keychain, enterprise
(EAP-PEAP) later; Wi-Fi menu in the menu bar and Settings pane.
**Done when.** With DRV-NET-03 or DRV-NET-04, joining a WPA2/WPA3 network
works and reconnects after sleep.
**Tests.** Host tests of the handshake with IEEE test vectors.
**Pitfalls.** SAE (dragonfly) side channels — constant-time.
**Refs.** IEEE 802.11-2020 §12.

### NET-60 — Bluetooth host stack: L2CAP, SMP, GATT (`bluetoothd`)
- Phase: P9 · Size: L · Status: todo
- Depends: DRV-BT-01, SVC-01
- Files: services/bluetoothd/

**Goal.** L2CAP (BR/EDR + LE), Security Manager (LE Secure Connections
pairing), GAP (scanning, connecting), ATT/GATT client, bonding keys in
the keychain, Settings > Bluetooth pane.
**Done when.** Pairing with a BLE device lists its GATT services.
**Tests.** Host tests with recorded traces; SMP test vectors.
**Pitfalls.** Pairing UI must show numeric comparison codes.
**Refs.** Bluetooth Core 5.4 Vol 3.

### NET-61 — Bluetooth keyboards and mice (HID)
- Phase: P9 · Size: M · Status: todo
- Depends: NET-60, DRV-USB-04
- Files: services/bluetoothd/hid.c

**Goal.** HID over GATT (BLE) and classic HID profile, reconnect on boot.
**Done when.** A Bluetooth keyboard and mouse work (record).
**Tests.** Hardware record.
**Pitfalls.** —
**Refs.** HOGP spec.

### NET-62 — Bluetooth audio (A2DP, AVRCP, HFP)
- Phase: P9 · Size: L · Status: todo
- Depends: NET-60, AUD-01, MED-10
- Files: services/bluetoothd/audio.c

**Goal.** A2DP sink/source with SBC (original codec implementation),
AVRCP media keys, HFP for headsets' microphones (later).
**Done when.** Music plays on Bluetooth headphones (record).
**Tests.** Host tests of SBC encoder vs reference output.
**Pitfalls.** Timing/latency.
**Refs.** A2DP, AVRCP specs.

### NET-50 — VPN (WireGuard protocol)
- Phase: P6 · Size: M · Status: todo
- Depends: NET-10, NET-11
- Files: kernel/net/wg/ or services/vpnd/

**Goal.** Original WireGuard-protocol implementation (Noise IK, ChaCha20-
Poly1305, Curve25519, BLAKE2s), tunnel interface, config import (QR/text),
VPN menu item.
**Done when.** Connects to a WireGuard server on the host; traffic routes
through it.
**Tests.** Host tests with protocol test vectors; interop with
`wireguard-go` on the host (tool only).
**Pitfalls.** Timers/handshake rules exactly per the whitepaper.
**Refs.** WireGuard whitepaper (Donenfeld), Noise Protocol Framework.

---

## Tools and quality

### NET-80 — Network tools and Network Utility app
- Phase: P5 · Size: M · Status: todo
- Depends: NET-10, NET-08
- Files: apps-cli/{ping,traceroute,netstat,ifconfig,ip,dig,nc,netcap}, apps/NetworkUtility

**Goal.** Command-line tools as real programs (moving `ui/netcmd.c`
features out of the kernel), `netcap` writing pcap files for Wireshark on
the Mac, Network Utility app with info/ping/lookup/traceroute tabs.
**Done when.** All tools work from Terminal; pcap opens in Wireshark.
**Tests.** Live tests.
**Pitfalls.** Raw sockets require admin.
**Refs.** pcap file format.

### NET-90 — Network benchmark suite
- Phase: P3 · Size: S · Status: todo
- Depends: NET-01
- Files: tests/qemu_netbench.py, programs/netbench

**Goal.** Throughput/latency/connection-rate benchmarks against a host
server, tracked over time with thresholds.
**Done when.** Bench in the perf suite.
**Tests.** The bench.
**Pitfalls.** Quiet host only.
**Refs.** —

### NET-91 — Network parser fuzzing harness
- Phase: P1 · Size: S · Status: todo
- Depends: —
- Files: tests/test_net.c, tests/fuzz/

**Goal.** One harness that feeds random/mutated packets to every parser
(Ethernet, ARP, IPv4, UDP, TCP, DHCP, DNS, TLS records, HTTP heads) for
N iterations in `scripts/test.sh` and longer in a nightly mode.
**Done when.** Harness runs under ASan in the normal suite.
**Tests.** Itself.
**Pitfalls.** Keep iteration counts small for the normal run.
**Refs.** —
