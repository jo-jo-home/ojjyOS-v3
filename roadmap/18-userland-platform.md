# 18 — Userland, packages (ojjypkg) and the developer platform

Everything a process needs above the kernel: the C library, the shell and
command-line tools, core utility libraries, the package manager (the Arch
side of ojjyOS), the SDK and, eventually, self-hosting.

**Originality policy for user space (decision D-023, see
`03-target-architecture.md`):** the **base system** (libc, shell, core
tools, services, frameworks, built-in apps) is original code. **Ported
third-party open-source software** (git, Python, clang, vim, games…) is
allowed **only as optional packages** installed through ojjypkg — never
compiled into the base image — with licences recorded in the package and
in `THIRD_PARTY.md`. This is how every OS lets users install software; it
keeps Jonas's "all original" rule for ojjyOS itself.

---

## C library and core libraries

### USR-01 — C library (`libc`)
- Phase: P2 · Size: L · Status: todo
- Depends: SYS-05, SYS-06, SYS-07, THR-04
- Files: lib/libc/ (new), lib/libc/include/

**Goal.** Original C17 library for native programs: `stdio` (buffered,
`printf` family with full format support incl. floating point), `stdlib`
(`malloc` with size classes + thread caches on vm syscalls, `qsort`,
`strtod` correctly rounded, environment), `string`, `ctype` (UTF-8
aware wide functions), `time` (`strftime`, tz via TIM-03), `errno`,
`setjmp`, `signal` (PROC-06), `pthread` (THR-05 futexes), POSIX wrappers
(files, dirs, processes, `posix_spawn`, pipes, sockets, `poll`),
`dlfcn` (ELF-05), `getopt`, `locale` (C and UTF-8), static and shared
builds, startup code (`crt1`, init/fini).
**Steps.**
1. Freestanding subset + `printf`/`malloc` for the first programs.
2. POSIX I/O and processes; threads.
3. Shared library build; ABI checks (ELF-06).
**Done when.** USR-04 conformance suite passes; coreutils build against it.
**Tests.** USR-04 suite; host tests for format/parse functions vs the
host libc results.
**Pitfalls.** `printf("%f")` and `strtod` exactness (use well-known
algorithms, write them ourselves); `malloc` fragmentation.
**Refs.** ISO C17, POSIX.1-2017.

### USR-02 — Math library (`libm`)
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libm/

**Goal.** C17 math functions in double/float with documented accuracy
(≤ 1 ULP for common functions), correct handling of special values,
`fenv`, vectorizable versions of hot functions (`expf`, `sinf`) for
graphics.
**Done when.** Accuracy tests against high-precision references pass.
**Tests.** Host tests comparing against the host libm and reference
tables at many points.
**Pitfalls.** Range reduction for large trig arguments.
**Refs.** IEEE 754-2019.

### USR-03 — C++ support for ported software
- Phase: P7 · Size: M · Status: todo
- Depends: USR-01, PKG-01
- Files: packages/llvm-libcxx/ (OJBUILD)

**Goal.** Package LLVM's libc++/libc++abi/libunwind (permissive licence)
for ports that need C++ (D-023: packages only, base stays C).
**Done when.** A C++ program with exceptions and STL runs.
**Tests.** Small C++ test programs.
**Pitfalls.** Unwinding needs `.eh_frame` support in the dynamic linker.
**Refs.** Itanium C++ ABI.

### USR-04 — POSIX conformance test suite (original)
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: tests/posix/

**Goal.** Our own test programs for libc + syscalls: stdio, string,
memory, time, files, directories, processes, signals, threads, sockets,
locale; runs in QEMU and reports per-area pass counts.
**Done when.** Runs in the live suite with recorded baseline.
**Tests.** Itself.
**Pitfalls.** —
**Refs.** POSIX.

### USR-40 — ojjyDB embedded database
- Phase: P3 · Size: L · Status: todo
- Depends: USR-01, FS-13
- Files: lib/libojjydb/

**Goal.** Original embedded database for app data (Photos library,
Mail index, Music library, search index, package database): B+tree
storage in a single file, ACID transactions with a write-ahead log,
indexes, a small typed query API (no full SQL initially), crash safety.
**Done when.** Photos and searchd store millions of records reliably;
crash test (kill during commit) loses no committed data.
**Tests.** Host tests incl. crash injection and fuzzing of file images.
**Pitfalls.** fsync discipline; page checksums.
**Refs.** Database textbooks (WAL, B+trees).

### USR-41 — Configuration format library (TOML-like)
- Phase: P2 · Size: S · Status: todo
- Depends: USR-01
- Files: lib/libconf/

**Goal.** Parser/writer for the TOML subset used by bundles, services,
packages and settings (preserving comments on rewrite).
**Done when.** Used by APP-00, SVC-01, PKG-01.
**Tests.** Host tests with the toml-test style fixtures (own copies) and
fuzzing.
**Pitfalls.** —
**Refs.** TOML 1.0 spec.

### USR-42 — JSON and XML libraries
- Phase: P2 · Size: S · Status: todo
- Depends: USR-01
- Files: lib/libjson/, lib/libxml_ojjy/

**Goal.** Fast JSON parse/serialise (with streaming), safe XML parser
(no external entities, depth limits) for feeds, SVG, office formats,
CalDAV.
**Done when.** Used by WEB, NET-76, APP-NEWS-01.
**Tests.** JSONTestSuite-style fixtures (own copies) + fuzzing.
**Pitfalls.** Billion-laughs protection.
**Refs.** RFC 8259, XML 1.0.

### USR-43 — Compression libraries
- Phase: P3 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libcompress/ (from kernel/src/image/inflate.c)

**Goal.** Deflate/zlib/gzip decode+encode, zstd decode+encode (original
implementation from RFC 8878), xz/LZMA decode, LZ4-class fast codec
(shared with FS-16/MEM-15), streaming APIs.
**Done when.** ojjypkg uses zstd; Archive Utility uses all.
**Tests.** Round-trip and interop with host `zstd`/`xz`/`gzip` outputs;
fuzzing.
**Pitfalls.** Decompression bombs.
**Refs.** RFC 1951, RFC 8878.

### USR-44 — Crypto library for user space
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libcrypto_ojjy/ (shared with kernel/src/crypto)

**Goal.** One source tree for kernel and user crypto: SHA-2, SHA-3,
BLAKE2/3, HMAC, HKDF, PBKDF2, Argon2id (password hashing, upgrade from
PBKDF2 — SEC-41), AES-GCM, ChaCha20-Poly1305, X25519, Ed25519 (sign +
verify), ECDSA P-256/384 (sign + verify), RSA verify/sign, constant-time
utilities, secure random.
**Done when.** Ed25519 signing used by ojjypkg; Argon2id by accounts.
**Tests.** RFC/NIST vectors (extend `test_crypto.c`, `test_pubkey.c`).
**Pitfalls.** Side channels: constant-time review checklist.
**Refs.** RFC 8032, RFC 9106, FIPS 180-4/202.

### USR-46 — `libojjy` foundation library
- Phase: P2 · Size: L · Status: todo
- Depends: USR-01, IPC-07
- Files: lib/libojjy/

**Goal.** The native application framework below ojjyKit: refcounted
objects, UTF-8 strings with Unicode helpers (TXT-13), arrays/maps,
URLs, paths, dates/calendars, number formatting (with I18N), run loop
(ports/timers), task queues and thread pools, notifications between
objects, file coordination, preferences access (SVC-10), process launch,
logging (`ojlog`), error objects.
**Done when.** ojjyKit and services build on it; API documented.
**Tests.** Host unit tests for every module.
**Pitfalls.** Keep it C and small; no hidden global state.
**Refs.** —

---

## Shell and command-line tools

### USR-20 — `ojsh` shell
- Phase: P2 · Size: L · Status: todo
- Depends: USR-01, PROC-05, DRV-TTY-01
- Files: apps-cli/ojsh/

**Goal.** POSIX `sh`-compatible scripting (variables, quoting, expansions,
here-docs, functions, `if/case/for/while`, pipelines, redirections,
subshells, traps) plus a friendly interactive mode: syntax highlighting,
autosuggestions from history, tab completion (commands, paths, options
from completion files), coloured prompt themes (default shows user, host,
path in accent colours like today's Terminal), `cd` history, job control.
**Done when.** Runs common POSIX scripts unmodified; interactive features
work in Terminal.
**Tests.** Script test suite with expected outputs.
**Pitfalls.** Word splitting and quoting rules must be exact.
**Refs.** POSIX Shell Command Language.

### USR-21 — Line editing library
- Phase: P2 · Size: S · Status: todo
- Depends: USR-01, DRV-TTY-01
- Files: lib/liblineedit/

**Goal.** Readline-like editing (emacs and vi modes, history search,
completion hooks, multi-line, UTF-8 width aware) for ojsh, REPLs, tools.
**Done when.** ojsh uses it.
**Tests.** Host tests with simulated terminals.
**Pitfalls.** —
**Refs.** —

### USR-10 — File utilities
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: apps-cli/coreutils/

**Goal.** `ls` (colours, `-la`, human sizes), `cp` (incl. `-r`, clones on
ojjyFS), `mv`, `rm` (`-i` default for root?), `mkdir -p`, `rmdir`, `ln`,
`chmod`, `chown`, `touch`, `stat`, `df`, `du`, `find`, `file` (magic
detection), `realpath`, `basename`, `dirname`, `mktemp`, `install`.
**Done when.** Each tool passes its test script; `--help` for all.
**Tests.** Script tests in QEMU.
**Pitfalls.** Option compatibility: GNU-style long options + BSD short.
**Refs.** POSIX utilities.

### USR-11 — Text utilities
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01, USR-13
- Files: apps-cli/coreutils/

**Goal.** `cat`, `echo`, `printf`, `head`, `tail -f`, `wc`, `sort`, `uniq`,
`cut`, `paste`, `tr`, `tee`, `grep` (colour), `sed` (POSIX), `awk`
(POSIX awk, own implementation), `xargs`, `diff`/`patch`, `base64`,
`sha256sum`, `seq`, `yes`, `nl`, `fold`, `column`.
**Done when.** Test scripts pass.
**Tests.** Script tests.
**Pitfalls.** awk is a language: separate sub-step.
**Refs.** POSIX utilities.

### USR-12 — System utilities
- Phase: P2 · Size: S · Status: todo
- Depends: USR-01, PROC-09
- Files: apps-cli/sysutils/

**Goal.** `ps`, `top` (curses-like UI), `kill`, `killall`, `uptime`,
`uname`, `whoami`, `id`, `hostname`, `date`, `sleep`, `env`, `which`,
`sudo` (admin auth via authd), `su`, `shutdown`, `reboot`, `mount`,
`umount`, `dmesg`, `sysctl`-like `ojctl`, `open` (appregd), `defaults`
(settings store CLI), `log` (logd query).
**Done when.** Old kernel console/Terminal commands all have program
equivalents.
**Tests.** Script tests.
**Pitfalls.** `sudo` must use the same authorization rules as Settings.
**Refs.** —

### USR-13 — Regular expression library
- Phase: P2 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libregex/

**Goal.** POSIX BRE/ERE and a Perl-compatible subset (classes,
lookahead, lazy quantifiers, Unicode properties) with a backtracking-safe
engine (Thompson NFA/DFA for the non-backtracking subset, bounded
backtracking otherwise); used by grep/sed/editors/ojjyWeb JS later.
**Done when.** Tools and ojjyCode find/replace use it.
**Tests.** Host regex test corpus; ReDoS timing tests.
**Pitfalls.** Catastrophic backtracking (seen in this project's own tooling).
**Refs.** POSIX regex; ECMAScript RegExp spec for the JS flavour later.

### USR-14 — Pager and terminal editor
- Phase: P2 · Size: M · Status: todo
- Depends: USR-21, DRV-TTY-01
- Files: apps-cli/less/, apps-cli/ojedit/

**Goal.** `less`-like pager (search, follow mode, colours) and `ojedit`,
a nano-like friendly terminal editor (shortcut bar, syntax highlighting,
mouse support), default `$EDITOR`.
**Done when.** `git commit` (after USR-31) opens ojedit.
**Tests.** Terminal fixture tests.
**Pitfalls.** —
**Refs.** —

### USR-15 — `ojjyfetch` and fun terminal extras
- Phase: P2 · Size: S · Status: todo
- Depends: USR-12
- Files: apps-cli/ojjyfetch/

**Goal.** neofetch-style system info with the ojjyOS ASCII art Jonas
likes (credit "Created by Jonas Lee"), `cowsay`-like `ojsay`, `fortune`
with own quotes, `sl`-like train, colour test.
**Done when.** `ojjyfetch` prints art + system info.
**Tests.** Golden output test.
**Pitfalls.** —
**Refs.** —

### USR-16 — Man pages and `help`
- Phase: P3 · Size: S · Status: todo
- Depends: USR-14
- Files: docs/man/, apps-cli/man

**Goal.** Manual pages for every tool/syscall/library function in a
simple markup rendered by `man`, searchable (`apropos`), also shown in the
Help viewer.
**Done when.** `man ls` works; coverage check in tests.
**Tests.** Coverage script (every tool has a page).
**Pitfalls.** —
**Refs.** —

---

## Ports (packages, D-023)

### USR-30 — Toolchain port (clang, lld, make)
- Phase: P7 · Size: L · Status: todo
- Depends: PKG-04, USR-03, SDK-02
- Files: packages/llvm/, packages/make/

**Goal.** LLVM clang/lld and GNU-compatible make running on ojjyOS,
targeting ojjyOS — the step before self-hosting.
**Done when.** Hello world compiles and runs on ojjyOS.
**Tests.** Build tests in QEMU.
**Pitfalls.** Memory/time: build packages on the Mac (cross), run on ojjyOS.
**Refs.** —

### USR-31 — git port
- Phase: P7 · Size: M · Status: todo
- Depends: PKG-04, NET-15, USR-20
- Files: packages/git/

**Goal.** git with HTTPS (our TLS through a curl-compatible shim or
git's own transport against libhttp) and SSH (ojssh).
**Done when.** Clone a public repository and commit locally.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### USR-32 — Python 3 port
- Phase: P7 · Size: M · Status: todo
- Depends: PKG-04, USR-02
- Files: packages/python/

**Goal.** CPython with standard library modules (ssl via our TLS
shim or a ported library as a package), `pip` working against PyPI for
pure-Python packages.
**Done when.** `python3 -m pip install requests` and a script fetching a
page work.
**Tests.** Subset of CPython test suite.
**Pitfalls.** —
**Refs.** —

### USR-33 — Popular CLI tools and runtimes
- Phase: P7 · Size: M · Status: todo
- Depends: PKG-04
- Files: packages/*

**Goal.** Package Lua, vim, nano, htop-like, tmux, ripgrep (needs Rust,
SDK-09), jq, ffmpeg-free alternatives as available; Node.js later (needs
JIT entitlement SEC-21).
**Done when.** 30 popular packages in `extra`.
**Tests.** Smoke test per package (`OJBUILD` check function).
**Pitfalls.** Each port its own small card when non-trivial.
**Refs.** —

---

## Packages: ojjypkg (the Arch-inspired package manager, D-015)

### PKG-01 — ojjypkg format and CLI
- Phase: P5 · Size: L · Status: todo
- Depends: USR-43, USR-41, FS-13
- Files: apps-cli/ojjypkg/, docs/pkg/format.md

**Goal.** `.ojpkg` = tar + zstd with `.PKGINFO` (name, version, release,
arch, deps, optdeps, provides, conflicts, replaces, licence, install
size), file list with hashes, install scripts (restricted hooks);
local database; CLI modelled on pacman with friendlier aliases:
`ojjypkg -S/install`, `-R/remove`, `-Syu/upgrade`, `-Q/list`, `-Qo/owns`,
`-Ss/search`, `-Si/info`, `-Fl/files`; dependency solver (SAT-lite with
clear errors), transactions with file-conflict checks, progress bars,
parallel downloads.
**Done when.** Install/remove/upgrade of a dozen packages from a local
repo is reliable.
**Tests.** Host tests of the solver and DB; live tests.
**Pitfalls.** Partial upgrades: the tool refuses inconsistent states
(rolling-release discipline like Arch).
**Refs.** pacman documentation (concepts only).

### PKG-02 — Signing and keyring
- Phase: P5 · Size: M · Status: todo
- Depends: PKG-01, USR-44, SEC-20
- Files: apps-cli/ojjypkg/sign.c, /System/Library/Keys/packages/

**Goal.** Ed25519 signatures on packages and repository databases,
trusted keyring shipped with the OS (Jonas's master key offline, signing
subkeys), key rotation and revocation, `ojjypkg-key` tool.
**Done when.** Tampered packages are rejected with a clear message.
**Tests.** Host tests with tampered fixtures.
**Pitfalls.** Key management documented (DOC).
**Refs.** RFC 8032.

### PKG-03 — Repository tools and hosting
- Phase: P5 · Size: S · Status: todo
- Depends: PKG-02
- Files: tools/repo-add.py, docs/pkg/repos.md

**Goal.** Build repository databases from a folder of packages, mirrors
list, hosting as static files (local server for tests; a public host later
when Jonas decides).
**Done when.** `ojjypkg -Syu` works against a repo served from the Mac.
**Tests.** Live test with `tests/web_server.py`.
**Pitfalls.** —
**Refs.** —

### PKG-04 — OJBUILD scripts and `ojmakepkg`
- Phase: P5 · Size: M · Status: todo
- Depends: PKG-01, SDK-02
- Files: tools/ojmakepkg, packages/

**Goal.** PKGBUILD-like build scripts (`OJBUILD`: metadata, sources with
checksums, `prepare/build/check/package` functions) built in a clean
sandbox (cross on the Mac first, native later), producing signed
packages; template and linting.
**Done when.** 10 packages build reproducibly from OJBUILDs.
**Tests.** Lint + build tests.
**Pitfalls.** Reproducibility (timestamps, paths).
**Refs.** —

### PKG-05 — OUR: ojjy User Repository
- Phase: P7 · Size: M · Status: todo
- Depends: PKG-04
- Files: docs/pkg/our.md, apps-cli/ojour/

**Goal.** AUR-like community repository of OJBUILD scripts (a git repo
or web service later), helper `ojour` that fetches, shows the script for
review, builds and installs; clear "unofficial" warnings; Store shows OUR
apps in a separate section when enabled.
**Done when.** Installing a community package via `ojour` works.
**Tests.** Live test with a local OUR mirror.
**Pitfalls.** Security: always show scripts before building.
**Refs.** —

### PKG-06 — Snapshot-protected transactions and rollback
- Phase: P5 · Size: S · Status: todo
- Depends: PKG-01, FS-15
- Files: apps-cli/ojjypkg/snapshot.c

**Goal.** Take a snapshot before each transaction; `ojjypkg rollback`
returns to it (and boot menu can boot the pre-update snapshot).
**Done when.** A bad package upgrade can be rolled back.
**Tests.** Live test.
**Pitfalls.** Data vs system separation (don't roll back user files).
**Refs.** —

### PKG-20 — Package daemon (`pkgd`) for the Store
- Phase: P6 · Size: M · Status: todo
- Depends: PKG-01, SEC-12, IPC-07
- Files: services/pkgd/

**Goal.** Privileged service performing installs for the Store and
Settings with progress events, admin authorization via authd, background
updates, notifications.
**Done when.** Store installs without a terminal.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### PKG-21 — App bundles: drag-to-install, uninstall, updates
- Phase: P5 · Size: S · Status: todo
- Depends: APP-00, SEC-20, SHL-27
- Files: services/appregd/

**Goal.** Bundles copied into /Applications are verified (signature,
quarantine prompt on first open), registered, removable by dragging to
Trash (with leftover cleanup option), self-update framework for bundles
(signed appcast-like feeds).
**Done when.** A downloaded `.ojdmg` app installs by drag and updates
itself.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

---

## SDK and developer platform

### SDK-01 — Build orchestrator `ojjy`
- Phase: P1 · Size: M · Status: todo
- Depends: —
- Files: tools/ojjy (new), scripts/*.sh

**Goal.** One command for everything: `ojjy build kernel|libs|services|
apps|image|all --arch aarch64`, `ojjy test host|live|vz`, `ojjy run vz|qemu`,
`ojjy package <name>`; wraps today's scripts; parallel builds; clear
output; used by AI sessions.
**Done when.** Docs and AGENTS.md use `ojjy` commands.
**Tests.** CI script runs `ojjy build all` and `ojjy test host`.
**Pitfalls.** Keep the existing scripts working underneath.
**Refs.** D-019.

### SDK-02 — Target triples and sysroot
- Phase: P2 · Size: S · Status: todo
- Depends: USR-01, ELF-05
- Files: sdk/toolchain/, tools/ojjy

**Goal.** `aarch64-unknown-ojjy` and `x86_64-unknown-ojjy` clang
configuration files (sysroot, crt objects, default libs, PIE), generated
sysroot from built libraries.
**Done when.** `ojjy-clang hello.c` produces a running program.
**Tests.** Build test.
**Pitfalls.** —
**Refs.** —

### SDK-03 — macOS SDK package and app templates
- Phase: P3 · Size: S · Status: todo
- Depends: SDK-02, UIK-01
- Files: sdk/

**Goal.** Downloadable SDK (headers, libraries, tools) for building ojjyOS
apps on a Mac, `ojjy new app MyApp` template (ojjyKit window, menus,
bundle), docs "Your first ojjyOS app".
**Done when.** A new app builds and runs in the VM in < 5 minutes.
**Tests.** Template build test.
**Pitfalls.** —
**Refs.** —

### SDK-04 — API documentation
- Phase: P3 · Size: M · Status: todo
- Depends: SDK-03
- Files: docs/api/, tools/gendocs.py

**Goal.** Reference docs generated from header comments (libojjy,
ojjyKit, ojjyDraw, ojjyGPU, syscalls), guides, sample code; browsable in
the Help viewer and as static HTML.
**Done when.** Every public function documented (coverage check).
**Tests.** Coverage script.
**Pitfalls.** —
**Refs.** —

### SDK-05 — User-space debugger support
- Phase: P3 · Size: M · Status: todo
- Depends: PROC-08, SYS-02
- Files: kernel/core/debug.c, apps-cli/ojdebugserver

**Goal.** Debug API (attach via exception ports, read/write memory and
registers, breakpoints, single-step, thread list) and a GDB-remote-
protocol server so lldb/gdb on the Mac (or a ported one) can debug ojjyOS
processes.
**Done when.** lldb sets a breakpoint in a running ojjyOS app.
**Tests.** Scripted debugger session test.
**Pitfalls.** Permission: same user or admin, plus debugging entitlement.
**Refs.** GDB Remote Serial Protocol.

### SDK-06 — App profiler (Instruments-like)
- Phase: P6 · Size: M · Status: todo
- Depends: KDB-05, SDK-05
- Files: apps/Profiler/

**Goal.** Time profiler (sampling call trees), allocations, file/network
activity, frame timeline, using the kernel tracing infrastructure.
**Done when.** Finding a slow function in a sample app is easy.
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

### SDK-09 — Rust support
- Phase: P10 · Size: M · Status: todo
- Depends: USR-30
- Files: packages/rust/

**Goal.** Rust target spec for ojjyOS (std port on our libc), packaged
toolchain, so Rust programs and tools can be ported.
**Done when.** A Rust hello world and ripgrep run.
**Tests.** Build tests.
**Pitfalls.** —
**Refs.** —

### SDK-40 — Self-hosting
- Phase: P10 · Size: L · Status: todo
- Depends: USR-30, USR-31, APP-CODE-02
- Files: tools/ojjy

**Goal.** Build the whole of ojjyOS (kernel, libs, apps, image) on
ojjyOS itself, with the same outputs as the Mac build (hash comparison
for reproducible parts).
**Done when.** A self-built image boots and passes the smoke suite.
**Tests.** Full build + smoke inside ojjyOS.
**Pitfalls.** Performance and memory; may need more VM RAM.
**Refs.** —
