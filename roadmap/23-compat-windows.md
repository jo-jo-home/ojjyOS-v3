# 23 — Windows compatibility: ojjyWin

Jonas: "maybe it should be able to run windows apps ... i want it to be
compatible with like adding windows apps". Decision D-016: **ojjyWin**, a
user-space compatibility layer: our own PE loader plus our own
implementations of the Windows DLLs (`ntdll`, `kernel32`, `user32`,
`gdi32`, …) built on the ojjyOS native API. Windows programs run as normal
ojjyOS processes with windows in WindowServer.

Architectures:
- **ARM64 Windows programs** (many apps now ship them) run natively on
  the ARM64 build — the first target.
- **x86_64 programs on the x86_64 build** run natively too.
- **x86_64 programs on ARM64** need a binary translator (WIN-80+) —
  the biggest single task in this file; done after the API layer works.

Clean-room rules (strict — this area is legally sensitive):
- Implement from **public Microsoft documentation** (Microsoft Learn API
  reference, the PE/COFF specification, open specifications like
  [MS-*]), published books and black-box behaviour tests we write.
- **Never** read or copy Windows, ReactOS or Wine source code; never use
  leaked Windows code; don't ship Microsoft DLLs, fonts or other files.
- Test programs: our own (built with clang/mingw on the Mac targeting
  Windows) and freely redistributable open-source Windows apps (e.g.
  Notepad++, 7-Zip, PuTTY) as external test inputs.
- Compatibility claims are per app (WIN-90 database), never "all
  Windows apps".

Layout: `compat/win/` — `loader/`, `dlls/<name>/`, `tools/` (regedit,
winecfg-like settings), `tests/` (Windows test programs + expected output).

---

## Foundation

### WIN-00 — ojjyWin design document
- Phase: P8 · Size: M · Status: todo
- Depends: USR-46, SYS-08
- Files: docs/compat/ojjywin.md (new), decision D-024

**Goal.** Architecture: process model (one ojjyOS process per Windows
process, `ntdll` emulating the NT object model in user space over native
handles), drive mapping (`C:` = `~/Library/ojjyWin/<prefix>/drive_c`,
`Z:` = ojjyOS `/`), registry storage, threading/TLS layout (TEB/PEB at the
offsets programs expect), exception handling (SEH/unwind on x64 and
ARM64), DLL search order, which Windows version we report, packaging of
per-app "bottles", ARM64 first then x86_64 translation, clean-room
process and test strategy.
**Done when.** Doc reviewed; decision D-024 recorded; card list below
adjusted if needed.
**Tests.** —
**Pitfalls.** Decide early how much NT kernel behaviour to emulate in
user space vs. new native syscalls.
**Refs.** Microsoft Learn; "Windows Internals" (book, concepts).

### WIN-01 — PE32+ loader
- Phase: P8 · Size: L · Status: todo
- Depends: WIN-00, ELF-05
- Files: compat/win/loader/

**Goal.** Load PE32+ images (machine ARM64/AMD64): headers, sections with
permissions, base relocations (ASLR), imports by name/ordinal, delay-load
imports, exports and forwarders, API set schema (`api-ms-win-*`
contracts → our DLLs), TLS directory and callbacks, exception tables
(`.pdata`/`.xdata`), load config (CFG flags tolerated), resources
(icons, version info, manifests), `DllMain` calls in the right order
with loader lock.
**Done when.** A console "hello" built with clang for
`aarch64-pc-windows-msvc` loads and reaches `main`.
**Tests.** Host tests parsing a corpus of PE files (fuzzed); live test.
**Pitfalls.** Malformed PE files: strict validation.
**Refs.** Microsoft PE/COFF specification.

### WIN-02 — `ntdll` core
- Phase: P8 · Size: L · Status: todo
- Depends: WIN-01
- Files: compat/win/dlls/ntdll/

**Goal.** TEB/PEB structures (fields programs read: TLS slots, last
error, stack limits, process parameters, loaded module list), Rtl heap
(RtlAllocateHeap… on native vm calls), critical sections, SRW locks,
condition variables, wait-on-address, SEH dispatch (`RtlDispatchException`,
unwinding via `.pdata` on x64/ARM64, vectored handlers,
`RtlRaiseException`, C++ exceptions transport), NTSTATUS mapping,
`Nt*`/`Zw*` APIs needed by `kernelbase`, string utilities.
**Done when.** C++ exceptions and `__try/__except` work in test programs.
**Tests.** Windows test programs with expected output.
**Pitfalls.** ARM64 Windows ABI: x18 = TEB pointer (must be preserved by
our native code paths), different unwind codes from x64.
**Refs.** Microsoft x64 and ARM64 ABI/exception handling docs.

### WIN-03 — `kernel32` / `kernelbase`
- Phase: P8 · Size: XL · Status: todo
- Depends: WIN-02
- Files: compat/win/dlls/kernelbase/

**Goal.** The core Win32 API: files and directories (CreateFile with
share modes, Read/WriteFile, overlapped I/O and I/O completion ports,
FindFirstFile, attributes, paths and drive letters, long paths),
processes (CreateProcess, command-line quoting rules, exit codes, job
objects basic), threads, synchronization objects and
`WaitForMultipleObjects`, memory (VirtualAlloc/Protect/Query, file
mapping), environment, time (FILETIME, time zones), system info, console
API, error codes/messages, modules (LoadLibrary/GetProcAddress),
locale/NLS basics. **Split into cards before work** (one per area).
**Done when.** Test programs for each area pass; 7-Zip console version
compresses and extracts.
**Tests.** Per-area Windows test programs.
**Pitfalls.** Case-insensitive paths over a case-sensitive FS (WIN-14).
**Refs.** Microsoft Learn Win32 API reference.

### WIN-04 — C runtime (UCRT) and VC runtime
- Phase: P8 · Size: L · Status: todo
- Depends: WIN-03
- Files: compat/win/dlls/ucrtbase/, vcruntime140/, msvcrt/

**Goal.** Our own `ucrtbase.dll` (stdio, stdlib, string, math, locale,
time, file descriptors on Win32 handles), `vcruntime140.dll` (memcpy,
exception helpers, `__CxxFrameHandler`), legacy `msvcrt.dll` for older
MinGW programs.
**Done when.** Programs built with MSVC-compatible toolchains and MinGW
run their CRT startup and printf correctly.
**Tests.** CRT test programs.
**Pitfalls.** C++ exception ABI interplay with WIN-02.
**Refs.** Microsoft CRT reference.

### WIN-05 — Milestone: console programs
- Phase: P8 · Size: S · Status: todo
- Depends: WIN-04, APP-PORT-08
- Files: compat/win/tools/ojjywin

**Goal.** `ojjywin run app.exe` (and double-click in Finder) runs console
programs in Terminal with correct encoding (UTF-8 console mode),
arguments, exit codes, Ctrl-C.
**Done when.** 10 console apps from the test corpus run correctly.
**Tests.** Corpus test script.
**Pitfalls.** —
**Refs.** —

### WIN-06 — Registry
- Phase: P8 · Size: M · Status: todo
- Depends: WIN-03
- Files: compat/win/dlls/advapi32/registry/, compat/win/tools/regedit

**Goal.** Hives stored in our own file format per prefix (HKLM, HKCU,
HKCR views), Reg* APIs (keys, values of all types, enumeration,
notifications), default keys programs expect (version info, fonts,
shell folders), a Registry Editor app.
**Done when.** Installers write uninstall entries readable by the app list.
**Tests.** Host tests of the hive store; Windows test programs.
**Pitfalls.** Concurrency between processes of the same prefix.
**Refs.** Microsoft registry docs.

### WIN-07 — Text encodings and NLS
- Phase: P8 · Size: S · Status: todo
- Depends: WIN-03, I18N-01
- Files: compat/win/dlls/kernelbase/nls/

**Goal.** UTF-16 throughout, `MultiByteToWideChar`/`WideCharToMultiByte`
with code pages 1252, 437, 932, 936, 949, 950, UTF-8 (tables generated
from Unicode's public mapping files), locale APIs (GetLocaleInfoEx) from
CLDR data.
**Done when.** Code page conversion tests pass.
**Tests.** Host tests.
**Pitfalls.** —
**Refs.** Unicode mapping tables.

### WIN-14 — Windows file semantics over ojjyFS
- Phase: P8 · Size: M · Status: todo
- Depends: WIN-03, FS-06
- Files: compat/win/dlls/kernelbase/files/

**Goal.** Case-insensitive lookup cache, share-mode enforcement (FS-06
mandatory locks), delete-on-close, alternate data streams (as xattrs),
reserved names (CON, NUL), timestamps precision, `\\?\` paths.
**Done when.** Installers and apps relying on these behave.
**Tests.** Windows file-semantics test programs.
**Pitfalls.** —
**Refs.** —

---

## GUI

### WIN-08 — `user32`: windows and messages
- Phase: P8 · Size: XL · Status: todo
- Depends: WIN-03, WS-02
- Files: compat/win/dlls/user32/

**Goal.** Window classes and procedures, per-thread message queues,
GetMessage/PeekMessage/DispatchMessage, posted vs sent messages
(cross-thread), WM_* message semantics (create, paint, size, input,
activation, close…), standard window styles mapped to WindowServer
windows (top-levels get ojjyOS glass chrome; child windows are in-process
regions), input (keyboard/mouse with virtual keys, IME messages), timers,
clipboard, caret, cursors, menus (mapped to UIK menus where possible),
dialogs from resources, DPI awareness. **Split into cards before work.**
**Done when.** A simple Win32 GUI app (our test "hello window" + Notepad++
later) runs.
**Tests.** Windows GUI test programs with scripted input.
**Pitfalls.** Message ordering/reentrancy is subtle — write behaviour
tests first.
**Refs.** Microsoft Learn: Windows and Messages.

### WIN-09 — `gdi32`: drawing
- Phase: P8 · Size: L · Status: todo
- Depends: WIN-08, GFX-02, TXT-04
- Files: compat/win/dlls/gdi32/

**Goal.** Device contexts, pens/brushes/fonts/bitmaps/regions, DIB
sections, BitBlt/StretchBlt/AlphaBlend, text output (ExtTextOut, font
mapping to installed fonts, metrics), paths, printing DCs later; GDI+ as
a separate later card if needed.
**Done when.** GDI test program screenshots match expectations.
**Tests.** Pixel tests of GDI calls.
**Pitfalls.** Font metrics compatibility.
**Refs.** Microsoft GDI docs.

### WIN-10 — Common controls (`comctl32` v6)
- Phase: P8 · Size: L · Status: todo
- Depends: WIN-09
- Files: compat/win/dlls/comctl32/

**Goal.** Button, edit, static, listbox, combobox, listview, treeview,
toolbar, status bar, progress, trackbar, tab, tooltips, rebar, image
lists — themed to look at home on ojjyOS (light/dark).
**Done when.** Notepad++ and 7-Zip GUI are usable.
**Tests.** Control test programs.
**Pitfalls.** —
**Refs.** Microsoft Common Controls docs.

### WIN-11 — Shell and common dialogs
- Phase: P8 · Size: M · Status: todo
- Depends: WIN-10, UIK-29
- Files: compat/win/dlls/shell32/, comdlg32/, shlwapi/

**Goal.** Open/Save dialogs mapped to ojjyOS panels, known folders mapped
to the user's folders, ShellExecute (open URLs in ojjyWeb, files with
ojjyOS default apps), icon extraction, drag and drop with ojjyOS apps.
**Done when.** Opening a file in a Windows app shows the ojjyOS panel.
**Tests.** Test programs.
**Pitfalls.** —
**Refs.** Microsoft Shell docs.

### WIN-12 — COM runtime
- Phase: P8 · Size: L · Status: todo
- Depends: WIN-06, WIN-02
- Files: compat/win/dlls/ole32/, oleaut32/

**Goal.** CoInitialize/apartments, CoCreateInstance through registry
class lookup, in-process servers, IUnknown/IDispatch, BSTR/VARIANT/
SAFEARRAY, standard marshalling basics, OLE drag and drop.
**Done when.** Apps using common COM components (e.g. file dialogs via
IFileDialog) work.
**Tests.** COM test programs.
**Pitfalls.** Large; implement on demand per tested app.
**Refs.** Microsoft COM docs.

### WIN-13 — Networking APIs
- Phase: P8 · Size: M · Status: todo
- Depends: WIN-03, NET-17
- Files: compat/win/dlls/ws2_32/, wininet/, winhttp/

**Goal.** Winsock 2 on native sockets (incl. overlapped I/O), WinINet and
WinHTTP on libhttp, DNS APIs.
**Done when.** PuTTY connects to an SSH server.
**Tests.** Test programs + PuTTY.
**Pitfalls.** —
**Refs.** Microsoft Winsock docs.

### WIN-15 — Installers and uninstallers
- Phase: P8 · Size: M · Status: todo
- Depends: WIN-11, WIN-06
- Files: compat/win/dlls/msi/, compat/win/integration/

**Goal.** Run NSIS/Inno Setup installers (they're plain programs), a basic
MSI engine for simple MSI packages, uninstall entries shown in the
ojjyWin apps list and removable from Launchpad.
**Done when.** Notepad++'s installer installs and the app launches.
**Tests.** Installer test runs.
**Pitfalls.** —
**Refs.** Windows Installer docs.

### WIN-16 — Fonts, theming and DPI
- Phase: P8 · Size: S · Status: todo
- Depends: WIN-09, TXT-08
- Files: compat/win/dlls/uxtheme/

**Goal.** Font substitutes (Segoe UI → Inter, Consolas → JetBrains Mono,
etc. via registry FontSubstitutes), system metrics, dark mode signals,
per-monitor DPI scaling with our 2× displays.
**Done when.** Windows apps render crisp at 2×.
**Tests.** Screenshots.
**Pitfalls.** Never ship Microsoft fonts.
**Refs.** —

### WIN-17 — Audio APIs
- Phase: P8 · Size: M · Status: todo
- Depends: WIN-12, AUD-03
- Files: compat/win/dlls/mmdevapi/, winmm/, dsound/

**Goal.** WASAPI (shared mode), winmm `PlaySound`/waveOut, DirectSound on
top of audiod.
**Done when.** A Windows media player plays audio.
**Tests.** Test programs.
**Pitfalls.** —
**Refs.** Microsoft Core Audio docs.

### WIN-18 — Crypto and certificates
- Phase: P8 · Size: M · Status: todo
- Depends: WIN-03, USR-44, NET-16
- Files: compat/win/dlls/bcrypt/, crypt32/, secur32/

**Goal.** CNG (bcrypt) algorithms, crypt32 certificate stores backed by
the ojjyOS trust store, Schannel-like TLS (secur32) on libtls.
**Done when.** Apps using WinHTTP over HTTPS work.
**Tests.** Test programs.
**Pitfalls.** —
**Refs.** Microsoft CNG docs.

### WIN-19 — .NET applications
- Phase: P9 · Size: M · Status: todo
- Depends: WIN-12, PKG-04
- Files: packages/dotnet/, compat/win/integration/dotnet.c

**Goal.** Run .NET apps: cross-platform .NET apps through the ported .NET
runtime (MIT, as a package — D-023) on the Linux layer or natively;
Windows-only WinForms apps through ojjyWin + the runtime's Windows build.
Decide the route in a decision record.
**Done when.** A WinForms sample app runs.
**Tests.** Sample apps.
**Pitfalls.** Large dependency; keep optional.
**Refs.** —

### WIN-20 — Desktop integration
- Phase: P8 · Size: S · Status: todo
- Depends: WIN-05, SHL-27
- Files: compat/win/integration/

**Goal.** `.exe` files open with ojjyWin; installed Windows apps appear in
Launchpad with icons from PE resources; file associations from the
registry; Windows apps in the Dock/Mission Control like native ones;
per-app settings (Windows version, DPI) in an "ojjyWin" settings app.
**Done when.** Double-clicking an installer in Downloads works end to end.
**Tests.** Live test.
**Pitfalls.** Quarantine prompts apply (SEC-22).
**Refs.** —

### WIN-60 — Direct3D 11 / DXGI translation
- Phase: P9 · Size: XL · Status: todo
- Depends: WIN-08, GPU-41
- Files: compat/win/dlls/d3d11/, dxgi/

**Goal.** Original D3D11 → ojjyGPU (Vulkan-shaped) translation including
DXBC shader translation; swapchains in WindowServer. Split before work.
**Done when.** A simple D3D11 game sample runs.
**Tests.** Rendering test programs.
**Pitfalls.** Shader translation is the hard part.
**Refs.** Microsoft Direct3D 11 docs.

### WIN-61 — Direct3D 9 and older games
- Phase: P10 · Size: L · Status: todo
- Depends: WIN-60
- Files: compat/win/dlls/d3d9/

**Goal.** D3D9 fixed-function and shaders via the same backend.
**Done when.** A classic D3D9 game runs.
**Tests.** Test programs.
**Pitfalls.** —
**Refs.** —

### WIN-62 — Direct3D 12 (stretch)
- Phase: P10 · Size: XL · Status: todo
- Depends: WIN-60
- Files: compat/win/dlls/d3d12/

**Goal.** D3D12 translation for modern games — only if hardware GPU
acceleration exists (GPU-40).
**Done when.** A D3D12 sample renders.
**Tests.** Samples.
**Pitfalls.** Stretch goal.
**Refs.** —

### WIN-63 — Game input (XInput, DirectInput)
- Phase: P9 · Size: S · Status: todo
- Depends: WIN-03, DRV-IN-06
- Files: compat/win/dlls/xinput1_4/, dinput8/

**Goal.** Controllers in Windows games through the game controller API.
**Done when.** A game reads a controller.
**Tests.** Test program.
**Pitfalls.** —
**Refs.** —

---

## x86-64 on ARM64

### WIN-80 — Translator research and design
- Phase: P8 · Size: M · Status: todo
- Depends: WIN-00
- Files: docs/compat/x86-translation.md, decision D-025

**Goal.** Design for running x86-64 code on ARM64: interpreter vs JIT vs
AOT caching, IR design, register mapping, flags handling (lazy flags),
memory ordering (x86 TSO vs ARM weak ordering — research whether a VZ
guest can use the Apple CPU TSO mode; otherwise barrier strategies),
self-modifying code detection, exceptions/precise state for SEH,
syscall/API thunks into ARM64 ojjyWin DLLs (ARM64EC-style interop),
performance targets.
**Done when.** Design reviewed; prototype interpreter plan.
**Tests.** —
**Pitfalls.** Memory ordering bugs are silent and rare — design tests.
**Refs.** Intel SDM Vol 2 (ISA); Arm ARM memory model.

### WIN-81 — x86-64 decoder and interpreter
- Phase: P9 · Size: L · Status: todo
- Depends: WIN-80
- Files: compat/x86emu/decoder/, interp/

**Goal.** Full x86-64 instruction decoder (prefixes, REX/VEX/EVEX, all
general-purpose, x87, SSE–SSE4.2, AVX/AVX2 initially; AVX-512 later),
reference interpreter for correctness, test generator comparing against
real x86 results recorded on an x86 machine/QEMU.
**Done when.** Instruction tests pass; console x64 programs run (slowly).
**Tests.** Generated instruction tests (millions of cases).
**Pitfalls.** Flags corner cases; x87 80-bit precision.
**Refs.** Intel SDM Vol 2.

### WIN-82 — JIT translator
- Phase: P9 · Size: XL · Status: todo
- Depends: WIN-81, SEC-21
- Files: compat/x86emu/jit/

**Goal.** Block translation x86-64 → IR → ARM64 with register allocation,
lazy flags, code cache with invalidation, indirect branch prediction
cache, SIMD mapping to NEON, precise exception recovery. Split before work.
**Done when.** x64 apps run at ≥ 50% native speed on benchmarks.
**Tests.** Interpreter-vs-JIT differential tests; app corpus.
**Pitfalls.** JIT needs W^X-compliant code mapping (SEC-21 entitlement).
**Refs.** —

### WIN-83 — Ahead-of-time translation cache
- Phase: P10 · Size: M · Status: todo
- Depends: WIN-82
- Files: compat/x86emu/aot/

**Goal.** Translate installed x64 binaries ahead of time (on install or
idle) and cache signed results for fast startup.
**Done when.** Second launch of an x64 app is near-native startup speed.
**Tests.** Startup benchmarks.
**Pitfalls.** Cache invalidation on file change.
**Refs.** —

### WIN-84 — Mixing x64 apps with ARM64 DLLs
- Phase: P9 · Size: L · Status: todo
- Depends: WIN-82
- Files: compat/x86emu/thunks/

**Goal.** x64 code calls our ARM64-native ojjyWin DLLs through generated
thunks (calling convention translation both ways, callbacks), so only
app code is translated.
**Done when.** x64 GUI apps call native user32/gdi32 efficiently.
**Tests.** Thunk tests.
**Pitfalls.** Callbacks re-entering translated code.
**Refs.** Microsoft ARM64EC ABI docs (concepts).

### WIN-85 — x86-64 Linux programs via the translator
- Phase: P10 · Size: S · Status: todo
- Depends: WIN-82, LNX-08
- Files: compat/x86emu/linux/

**Goal.** Reuse the translator for x86_64 Linux binaries on ARM64
(Linux environment x86 packages, games).
**Done when.** An x86_64 Linux hello world and a small game run.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### WIN-90 — Compatibility database and app corpus
- Phase: P8 · Size: S · Status: todo
- Depends: WIN-05
- Files: docs/compat/windows-apps.md, compat/win/tests/corpus.toml

**Goal.** Tracked list of test apps (free, redistributable) with status
(platinum/gold/silver/bronze/broken), per-app workarounds, automatic
corpus runs that update the table.
**Done when.** Corpus runs nightly; table generated.
**Tests.** Corpus runner.
**Pitfalls.** Don't redistribute the binaries; download from publishers
in the test script.
**Refs.** —

### WIN-91 — Milestone: everyday Windows apps
- Phase: P9 · Size: M · Status: todo
- Depends: WIN-15, WIN-20, WIN-10
- Files: docs/compat/windows-apps.md

**Goal.** A set of real apps works well enough for daily use: Notepad++,
7-Zip, PuTTY, a Windows game from itch.io (free), and one x64-only app via
the translator.
**Done when.** Each app's core workflow passes a recorded test.
**Tests.** Recorded tests + corpus.
**Pitfalls.** —
**Refs.** —
