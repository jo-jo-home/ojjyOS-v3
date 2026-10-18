You are working on ojjyOS v3, a hobby operating system written from scratch by Jonas Lee (custom kernel, no Linux/BSD code; macOS-Tahoe-style desktop). You have no prior context; everything you need is in the repository at ~/Desktop/Desktop Jonas/ojjyOS-kernel/ (folder ojjyOS-v3, plus a git worktree ojjyOS-v3-ui). Work fully autonomously: plan first, execute end to end without asking questions, then give Jonas a plain-English summary of exactly what changed.

Jonas's request: "do what is needed in the project".

1. Read ojjyOS-v3-ui/TODO.md (or ojjyOS-v3/TODO.md on branch main) completely. It lists the active jobs, their briefs, and the backlog in priority order.
2. Run `git worktree list`, then `git status` and `git log --oneline -5` in each folder. Two jobs may be in progress: job A (native Mac VM app; main folder, branch native-vm; brief HANDOFF_NATIVE_VM.md) and job B (UI; worktree ojjyOS-v3-ui, branch ui-polish-2; brief HANDOFF_UI.md). If another AI is still working in one of them (recent uncommitted changes, running builds or qemu processes), take the other one. Never reset, stash or overwrite someone else's uncommitted work.
3. Read AGENTS.md and the brief for the job you take, completely, then follow it.
4. Rules: never push; commit locally as Jonas (git -c user.name="Jonas Lee" -c user.email="jonas@ojjyos.local" commit); both builds with zero warnings, bash scripts/test.sh and the live tests before each commit; never overwrite a disk image a running VM uses; the Apple Tahoe wallpapers in wallpapers/local/ are never committed or pushed; keep the blue Tahoe theme default and credit Jonas Lee.
5. Keep TODO.md, the job's plan log and its handoff file up to date as you finish each step, so the next AI can continue if you are stopped.
