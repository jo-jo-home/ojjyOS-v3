You are continuing work on ojjyOS v3, a hobby operating system written from scratch by Jonas Lee (custom kernel, no Linux/BSD code; macOS-Tahoe-style desktop). You have no prior context; everything you need is in the repository. Work fully autonomously (do not ask questions), plan first, then finish the task end to end, then give Jonas a plain-English summary of exactly what changed.

WHERE
- Work ONLY in the git worktree ~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3-ui on branch ui-polish-2.
- The main folder ~/Desktop/Desktop Jonas/ojjyOS-kernel/ojjyOS-v3 is checked out on branch native-vm by another AI job (native Mac VM app). Do not edit, build in, commit to, reset or stash anything there, and do not switch its branch.
- Machine: Apple M2 Pro, macOS 26, QEMU 11.1.2 (Homebrew), Homebrew LLVM at /opt/homebrew/opt/llvm, Python 3 with Pillow.

READ FIRST, COMPLETELY, IN THIS ORDER
1. AGENTS.md (project rules and history; its START HERE block describes the other job).
2. TODO.md (the project-wide list; your job is section 2).
3. HANDOFF_UI.md (your brief: what is done, exact files/functions, how to build and test, and the remaining steps with acceptance checks).
4. LIQUID_GLASS_APPS_PLAN.md (the plan; append to its ## Log after every step).

THE TASK
Finish job B from TODO.md in the order given in HANDOFF_UI.md section 5: C (Settings > Appearance customisation: Light/Dark/Auto, accent colour with Blue as default, Liquid Glass Clear/Tinted, menu bar Glass/Solid, window corners, Terminal theme row - applied live and saved), LG3 (Liquid Glass window rims), LG4 (glass controls), S1 (notifications), S2 (window snapping), S3 (live resize), S4 (Terminal themes), A (improve every app per the Phase A table), the listed leftovers, then the Finish steps.

RULES (non-negotiable)
- Never push to any remote. Commit locally after each step with: git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit (message ending with a Co-Authored-By trailer line for the AI), stage files by name (never git add -A; never commit ojjyos-v3-bugs-macos-features-custom-kernel.json), then fast-forward main from inside the worktree: git fetch -q . ui-polish-2:main
- Before every commit: both dev builds with zero warnings (OJJY_BUILD_DIR=build/dev-aarch64 and build/dev-x86_64), bash scripts/test.sh, the sanitizer run, and the live tests listed in HANDOFF_UI.md section 4 (new output directory each run). Look at screenshots in light AND dark mode at 2x yourself before committing.
- Never build into or overwrite build/aarch64 or build/x86_64 while a VM may use them; Jonas may have his VM running.
- The macOS Tahoe wallpapers are Apple's: they stay in git-ignored wallpapers/local/ and in locally built disk images only. Never commit or push them.
- Keep the blue Tahoe theme as the default, credit Jonas Lee in user-visible places, no AI names in the product, no big buffers in BSS or on the kernel stack (use pmm_alloc_bytes and reuse buffers).
- Keep writing context into the repo as you go (plan log, HANDOFF_UI.md status, TODO.md checkboxes) so the next AI can continue if you are stopped.
