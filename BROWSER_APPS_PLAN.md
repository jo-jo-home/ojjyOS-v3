# Working browser + better apps — plan and live log

Created by Jonas Lee. Request (2026-10-17, after the cursor/UI pass):
"keep working on improvements, then make a list of ur own for new stuff to
add. also improve the whole os like all the apps better and also focus on
making a working browser". Work autonomously; record context here as it
progresses; commit locally only, never push.

## Starting point (measured, not assumed)

- No networking exists at all: no NIC driver, no IP stack, no sockets.
- `Web.app` renders bundled `/System/Web/*.page` files in a tiny line markup
  (`# heading`, `>id link`). No URLs, no HTML, no address-bar editing.
- ARM64 (QEMU virt, the platform Jonas runs) uses virtio-mmio. The transport
  (`arch/aarch64/virtio.c`) only initialises and notifies **queue 0** and
  assumes one in-flight chain; virtio-net needs RX (queue 0) and TX (queue 1)
  with many buffers in flight.
- Intel has no PCI enumeration, so no NIC path there yet.
- Kernel heap is 512 KB; big browser buffers must be static/global (one
  browser instance), never per-window state (AppWindowState is copied on
  every raise) and never on the stack.
- No floating point in the kernel (`-mgeneral-regs-only`, x86 `-mno-sse`):
  all layout/crypto must be integer.
- Single-threaded main loop that wakes every 1 ms tick -> the network stack
  and fetches are **polled state machines**; the UI must never block on I/O.

## Architecture

```
kernel/src/net/        arch-neutral stack (host-testable)
  netif.h/.c           device hook (send/poll), MAC/IP config, stats
  eth_arp.c            Ethernet II + ARP cache/requests
  ip.c                 IPv4 (no fragment reassembly; DF), ICMP echo
  udp.c dhcp.c dns.c   UDP sockets, DHCP client, DNS A-record resolver
  tcp.c                client TCP: handshake, in-order RX buffer,
                       retransmit/backoff, FIN/RST, MSS 1460
  http.c               HTTP/1.1 GET: status, headers, Content-Length,
                       chunked, redirects (<=5), size cap
  tls.c + crypto/*     (phase 5) TLS 1.3 client
kernel/src/web/        browser engine (host-testable, no UI deps)
  url.c                parse/resolve/normalise URLs (http, https, ojjy:)
  html.c               tokenizer + entity decode + layout -> display list
kernel/src/arch/aarch64/virtio_net.c, virtio_rng.c   devices
kernel/src/ui/compositor.c   Web window draws the display list, input
```

QEMU: `-netdev user,id=net0 -device virtio-net-device,netdev=net0` and
`-device virtio-rng-device`. Slirp gives DHCP (10.0.2.15), gateway 10.0.2.2
(also = host loopback), DNS 10.0.2.3. Live tests serve pages from a local
Python HTTP server reached at `http://10.0.2.2:<port>/` so verification needs
no internet; real-site checks are a bonus, not a gate.

## Phases (each: host tests, both builds, live test, docs, local commit)

1. **Network foundation (ARM).** Multi-queue virtio transport; virtio-net
   (MAC feature, 12-byte header, RX buffer pool); virtio-rng; Ethernet/ARP/
   IPv4/ICMP/UDP; DHCP; DNS. Terminal: `ifconfig`, `ping`, `nslookup`.
2. **TCP + HTTP.** Polled TCP client and HTTP fetch state machine; Terminal
   `curl <url>`. Host tests drive TCP against a simulated peer (loss,
   reordering, RST, FIN) and HTTP parsing (chunked, redirects, truncation).
3. **HTML engine.** Tokenizer (tags, attrs, comments, entities, skip
   script/style), block/inline flow with word wrap in the 8x16 font:
   headings, p, br, div, ul/ol/li, a, pre/code, blockquote, hr, tables as
   rows, img alt placeholders, forms (text inputs + GET submit). Title.
4. **Browser UI.** Editable address bar, back/forward/reload/stop, home,
   status bar with progress/hover URL, clickable links, scroll (wheel, keys),
   window title = page title, start page with HTTP-friendly sites
   (frogfind.com search, 68k.news, info.cern.ch, example.com), offline
   `ojjy://` pages kept, error pages (DNS fail, refused, timeout, https).
5. **HTTPS.** TLS 1.3 (X25519, AES-128-GCM-SHA256, ChaCha20-Poly1305) with
   RFC test vectors on host. Honest UI: certificate *verification* is a
   separate milestone; until then pages show "Not verified" and are never
   described as secure.
6. **Apps pass.** Every built-in app reviewed in a live tour; fix bugs and
   add missing essentials (see list below).
7. **Intel networking (stretch).** PCI config scan + e1000 or virtio-pci.

## Wi-Fi / Bluetooth request (Jonas, mid-session: "add wifi bluetooth all that good stuff")

Reality check: QEMU on macOS exposes no Wi-Fi or Bluetooth hardware to the
guest (no emulated 802.11 chip; host Bluetooth cannot be passed through on
macOS). Plan, staying honest (no fabricated SSIDs/devices):
- Control Center "Wi-Fi" tile becomes real: it brings the actual network
  interface (virtio-net) up/down, shows connected/IP, and the menu bar shows
  a connection indicator. Settings gets a Network page (interface, MAC, IP,
  gateway, DNS, link state, traffic counters, "Renew DHCP").
- The interface is labelled "Network (virtual Ethernet)" where precision
  matters; the tile keeps the familiar Wi-Fi look.
- Bluetooth tile: shows "No Bluetooth hardware" and cannot be enabled.
  A future virtio/USB-passthrough path is a roadmap item, not claimed.

## App improvement targets (phase 6, refined during the tour)

- Finder: Trash instead of instant delete; new folder; folder copy.
- TextEdit/Notes: clearer save state, longer documents, word wrap.
- Terminal: `curl`, `ping`, `ifconfig`, `nslookup`, scrollback, tab-complete.
- Calculator: keyboard input, decimal display.
- Activity Monitor: network counters.
- Settings: Network page (IP, DNS, MAC, status).
- Calendar/Preview/About: review in tour.

## Jonas-facing roadmap ideas (my own list)

See `ROADMAP.md` (kept current).

## Live log

(appended as work proceeds)

### 2026-10-17 — Phase 1 progress
- virtio transport is multi-queue (`virtio_queue_init_index`, `virtio_notify`
  use the queue's own index; +3 host asserts in test_virtqueue).
- New `kernel/src/net/{net,tcp}.c`, `random.c`, `arch/aarch64/virtio_{net,rng}.c`.
  QEMU runner + smoke VM add `-netdev user` + `virtio-net-device` + `virtio-rng-device`.
- `tests/test_net.c` (45 asserts, also under ASan/UBSan): simulated LAN with
  DHCP/ARP/DNS (CNAME + compression, NXDOMAIN, timeout) and a scriptable TCP
  peer (200 KB through the 64 KB ring, OOO drop + dup ACK, RTO retransmit,
  FIN sequencing, refused, SYN timeout, network-off), 30k fuzzed frames.
  Lesson: slirp answers ARP for every on-link address (gateway AND DNS
  10.0.2.3); a fake LAN must too, or DNS appears "not sent".
- Live boot (`build/net-boot-1`): `[NET] DHCP lease 10.0.2.15 gateway
  10.0.2.2 dns 10.0.2.3` on the first try.
- Next: URL + HTTP client, Terminal `ifconfig/ping/nslookup/curl`.

### 2026-10-17 — Phase 2 progress (TCP/HTTP/Terminal)
- `web/url.c` (parse/resolve/dot-segments/encode), `web/http.c` (polled GET,
  Content-Length, chunked with resumable completion scan, <=5 redirects,
  20 s idle timeout, truncation flag, https -> honest "not supported yet").
- `ui/netcmd.c`: Terminal `ifconfig`, `net on|off|renew`, `ping`, `nslookup`,
  `curl [-I]` as background jobs (Ctrl+C cancels); serial receipts `[NET] ...`.
- `tests/web_server.py` (local server; guest uses http://10.0.2.2:PORT) and
  `tests/qemu_net.py`: PASS `build/net-3` — DHCP, ping gateway 3/3, curl
  index (exact length), redirect (1 hop), chunked (exact decoded length),
  300 KB truncated at 256 KiB cap, 404, refused port, net off refusal, net
  on -> new lease -> fetch. Bonus real internet: example.com DNS + HTTP 200.
- Smoke --protected both arches PASS with net devices (`build/smoke-net-*`).
- Known: ~80 KB/s download under TCG (ACK per segment, 1 ms loop). Tune later.
- Next: HTML engine (display list) + browser UI.

### 2026-10-17 — Phase 3+4 DONE: HTML engine + browser
- `web/html.c`: tokenizer -> flow layout -> display list (runs/boxes/links/
  fields/forms/anchors); headings 2x, bold, links, lists, pre, quotes, hr,
  tables, img alt, inputs/buttons/checkbox/select/textarea, GET forms,
  entities + UTF-8 transliteration, script/style skipped. Bug found by tests:
  block() must flush the pending word first (last word fell to next line).
- `ui/browser.c` (+ `ui/draw.h` exports): toolbar (back/fwd/reload-stop/home),
  editable address bar (words -> Wiby search), progress, status/hover URL,
  links, #anchors, forms (Tab/Enter), history with async replay + scroll
  restore, window title = page title, honest HTTPS page. Built-in pages are
  real HTML (`/System/Web/*.html`, generated by scripts/mkfs.sh).
- framebuffer: clip rectangle honoured by all primitives; scaled/bold text.
- IMPORTANT (boot): kernel image hit firmware boot-services memory at
  0x44000000 once BSS passed ~48 MB ("ConvertPages ... covers multiple
  entries"). Fixed by `pmm_alloc_contiguous()` and moving wallpaper caches
  (17.6 MB) + browser/curl buffers (3.2 MB) to runtime allocation; image now
  ends at ~0x42D40000. PMM also reserves the framebuffer range explicitly.
  Keep large buffers OUT of BSS.
- FrogFind (old default) is down from the host too; default search is Wiby
  (http://wiby.me/?q=) which still serves plain HTTP. Google needs JS.
- New windows are clamped on screen.
- Tests: test_web.c 52 asserts (+3000 fuzz docs), test_fs updated for .html;
  host 1184 C asserts + 9 Python, normal + ASan/UBSan. Live
  `tests/qemu_browser.py` PASS `build/web-5`: start page, local index (exact
  bytes), link click at laid-out position, Back replay, Tab+type+Enter form
  -> /search?q=ojjy+os, redirect, chunked text/plain, https notice without
  fetching, ojjy:about; real internet: CERN, 68k.news (96 KB in 0.4 s), Wiby
  search. Smoke --protected both arches PASS (`build/smoke-web-*`).
- Next: TLS 1.3 (HTTPS), then Wi-Fi tile/Network settings, then apps pass.

### Phase 5 plan: HTTPS (TLS 1.3) — design notes
- `kernel/src/crypto/`: SHA-256 + HMAC + HKDF(-Expand-Label), X25519
  (TweetNaCl-style, left shifts of negatives avoided for UBSan), AES-128 +
  GCM (table AES: not constant-time — documented), ChaCha20 + Poly1305 (26-bit
  limbs). random.c becomes a ChaCha20 fast-key-erasure DRBG seeded by
  virtio-rng + timer jitter.
- `kernel/src/net/tls.c`: TLS 1.3 client only (RFC 8446): x25519 key share,
  TLS_AES_128_GCM_SHA256 + TLS_CHACHA20_POLY1305_SHA256, SNI, ALPN http/1.1,
  middlebox-compat session id + CCS, server Finished verified, tickets and
  CCS ignored, close_notify = EOF. Transport-agnostic (send/recv callbacks)
  so a hosted probe (`tests/tls_probe.c`) can talk to `openssl s_server` and
  real sites from macOS before any QEMU run.
- Certificate chain is NOT verified yet (needs X.509 + RSA/ECDSA + roots):
  UI must say "encrypted, identity not verified", never "secure".
- Ground truth: python hashlib/hmac, RFC 7748/McGrew-Viega vectors, openssl
  CLI (chacha20, poly1305 mac), openssl s_server -tls1_3.

### 2026-10-17 — Phase 5 DONE: HTTPS (TLS 1.3)
- `kernel/src/crypto/{sha256,x25519,aes_gcm,chacha_poly}.c` + `crypto.h`;
  `tests/test_crypto.c` 43 asserts vs RFC 4231/5869/7748/8439, FIPS-197,
  McGrew-Viega GCM, and `tests/gen_crypto_vectors.py` (python hashlib/hmac +
  openssl CLI) -> `tests/crypto_vectors.h`. All passed first run.
- random.c is now a ChaCha20 fast-key-erasure DRBG (SHA-256 entropy mixing;
  virtio-rng, MAC, input-event timing).
- `kernel/src/net/tls.c`: TLS 1.3 client, x25519, ChaCha20-Poly1305 +
  AES-128-GCM, SNI, ALPN, middlebox compat, Finished verified, KeyUpdate,
  alerts mapped to readable errors. Certificates NOT verified (UI says
  "Encrypted (TLS 1.3 ...), site identity not verified").
- `tests/tls_probe.c` (hosted, real sockets): OK vs `openssl s_server -tls1_3`
  for both suites, and live sites example.com, wikipedia, github (578 KB),
  google, cloudflare (1 MB), apple, HN, duckduckgo, bbc, rust-lang, npr.
- http.c: `HTTP_SECURING` phase, pooled TlsConn (pmm), stream_* transport.
- Browser: https enabled; start page favorites add Wikipedia/NPR/HN.
- html.c: honour `hidden`, aria-hidden, inline display:none; block elements
  inside table cells flow inline (HN rows). test_web 54 asserts.
- Live `qemu_browser.py` PASS `build/web-7`: local HTTPS server (TLS 1.3,
  self-signed) exact bytes; real Wikipedia article 675 KB/0.6 s, NPR, HN.
- Smoke --protected both arches PASS (`build/smoke-tls-*`).
- Next: Wi-Fi tile + Network settings page; apps pass; cert verification later.

### 2026-10-17 — Wi-Fi / Bluetooth request DONE (honest version)
- Control Center redesigned as tiles (Wi-Fi, Bluetooth, Appearance,
  Transparency) + address line + Restart/Shut Down. Wi-Fi tile calls
  `net_set_enabled()` and persists `wifi=` (applied at UI start).
  Bluetooth tile: "No hardware" + message; cannot be enabled.
- Menu bar Wi-Fi glyph (slashed when off; click opens Control Center).
- Settings > Network page: status + switch, adapter/MAC, address/mask,
  router/DNS, traffic counters, Renew Lease, Bluetooth "No hardware", note
  that the VM link is virtual Ethernet. Repaints when network state changes.
- `[UI] window '<title>' at x,y wxh` serial receipt for UI tests.
- Live `tests/qemu_network_ui.py` PASS (`build/netui-2`): tile off ->
  `[NET] Network off`, Bluetooth message, tile on -> new DHCP lease,
  Settings > Network shows the lease. Smoke both arches PASS.
- Next: apps pass (all built-in apps), then cert verification / images.
