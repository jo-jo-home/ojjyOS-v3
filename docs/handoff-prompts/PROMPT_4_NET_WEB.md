You are working on ojjyOS v3, a hobby operating system written entirely from scratch by Jonas Lee: a custom kernel (no Linux/BSD/macOS code) for ARM64 and x86-64 with a macOS-Tahoe-style desktop, its own network stack, TLS 1.3 and a web browser. It runs as a native Mac app (Apple Virtualization.framework) and in QEMU. You have no prior context; everything you need is in the repository. Work fully autonomously: plan, then execute end to end without asking questions, then give Jonas a plain-English summary of exactly what changed.

ANOTHER AI IS WORKING AT THE SAME TIME on the desktop UI in a different folder. You must stay inside your own folder, branch and files.

WHERE YOU WORK
- Your git worktree: ~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3-net  (branch net-web, already created from main). cd there before every command.
- Do NOT edit, build in, or switch branches in ~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3 (Jonas's folder; he runs the OS from it) or ~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3-ui (the other AI's folder, branch desktop-polish).
- Machine: Apple M2 Pro, macOS 26, QEMU 11.1.2 (Homebrew), Homebrew LLVM at /opt/homebrew/opt/llvm, Python 3 with Pillow.

READ FIRST, COMPLETELY, IN THIS ORDER
1. PARALLEL_PLAN.md — how the two AIs share the repo: file ownership (section 3), interfaces between the tracks, build/test/commit/publish rules (section 4). Follow it exactly.
2. AGENTS.md — project rules and history (read the LATEST blocks at the top and sections 0, 6, 7).
3. NET_WEB_PLAN.md — YOUR task, step by step (N1-N8 + Finish), with the current state of the code, file names, test requirements and acceptance checks.
4. BROWSER_APPS_PLAN.md — how the network stack and browser were built.

YOUR TASK (Track N, details in NET_WEB_PLAN.md)
Make the browser trustworthy and complete:
- N1 crypto primitives: SHA-384/512, big integers, RSA (PKCS#1 v1.5 + PSS) and ECDSA (P-256, P-384) signature verification, host-tested with official/generated vectors.
- N2 strict ASN.1 DER + X.509 certificate parser, fuzzed.
- N3 THE SECURITY FIX: today kernel/src/net/tls.c never validates the server certificate and skips the CertificateVerify signature, so any network attacker can impersonate any HTTPS site. Add a curated root store (tools/mkroots.py from the macOS system roots), chain validation, hostname (SAN) matching, validity dates, and CertificateVerify checking; a clear "This connection is not private" page in the browser and refusal in Terminal curl.
- N4 a correct clock: clock.c + SNTP (time.apple.com) after DHCP, without changing rtc_read_time()'s signature.
- N5 images: DEFLATE/zlib + PNG decoder, progressive JPEG in kernel/src/image/jpeg.c (keep baseline working; the wallpapers depend on it), and the shared kernel/src/image/image.h API defined in PARALLEL_PLAN.md.
- N6 <img> on web pages (layout boxes, sequential fetching, memory limits, alpha blending).
- N7 downloads to ~/Downloads with a notification via notifications_push().
- N8 Web app polish inside kernel/src/ui/browser.c (glass toolbar, lock icon, start page tiles, error pages).

RULES (non-negotiable)
- Edit only the files PARALLEL_PLAN.md section 3 gives to Track N. Shared files (kernel/Makefile, scripts/test.sh, TODO.md, AGENTS.md, README.md, ROADMAP.md) are append-only, in small separate commits. Never edit kernel/src/ui/compositor.c, services.c, gfx.c, main.c, arch/ (except pl031.c), fs/, users.c, scripts/mkfs.sh or scripts/mkimg.sh; if you truly need a one-line hook elsewhere, make it a separate commit whose message starts with "hook:" and record it in your plan log.
- Before every commit: both dev builds with zero warnings (OJJY_BUILD_DIR="$PWD/build/dev-aarch64" ARCH=aarch64 bash scripts/build.sh, same for x86_64), bash scripts/test.sh, the sanitizer run (HOST_CC=/opt/homebrew/opt/llvm/bin/clang OJJY_SANITIZE=1 OJJY_TEST_DIR="$PWD/build/tests-san" bash scripts/test.sh), and the relevant live QEMU tests with a NEW output directory each run. New parsers get truncation + random-corruption fuzz tests under ASan/UBSan.
- Commit locally as Jonas: git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit, message ending with a "Co-Authored-By: <your model name> <noreply@...>" line. Stage files by name (never git add -A). Never commit ojjyos-v3-bugs-macos-features-custom-kernel.json, wallpapers/local/, build output, or iCloud duplicate files named like "net 2.o".
- Publish each green step to main from your worktree: git fetch -q . net-web:main ; if that fails because main moved, git rebase main (or merge main), rebuild, rerun the tests, then fast-forward again. Never force-update main.
- NEVER push to any remote. Never reset/stash/clean anything you did not create. Never rebuild Jonas's installed images (build/aarch64, build/x86_64, build/ojjyOS.app in the main folder) — that happens once at the very end of the round.
- Keep the blue Tahoe theme, credit Jonas Lee in user-visible places, no AI names in the product, no large buffers in BSS or on the kernel stack (pmm_alloc_bytes cannot free: reuse buffers).
- Performance numbers are only valid when the host is quiet; the other AI may be running QEMU too — say so in your log if measurements might be affected.
- Keep NET_WEB_PLAN.md's Log, TODO.md and your AGENTS.md LATEST block updated after every step, so a new AI could continue if you are stopped.
