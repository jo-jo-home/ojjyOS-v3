# Prompt for giving one roadmap card to an AI session

Copy everything between the lines, replace `<CARD-ID>` (and optionally
the extra instructions), paste it into a new Claude Code / OpenCode
session started in the project folder.

---

You are working on ojjyOS, Jonas Lee's from-scratch operating system.
Your task is roadmap card **<CARD-ID>**.

1. Read `AGENTS.md` (all of it, LATEST blocks first), then
   `roadmap/README.md`, `roadmap/00-how-to-work.md`,
   `roadmap/02-current-state.md` and `roadmap/03-target-architecture.md`.
2. Run `python3 roadmap/tools/roadmap.py show <CARD-ID>` and
   `python3 roadmap/tools/roadmap.py deps <CARD-ID>`. Read the card, every
   dependency card and the files listed under `Files`.
3. Check git state in all worktrees (`git worktree list`); work in the
   right worktree/branch per `roadmap/00-how-to-work.md` §2; never touch
   other sessions' uncommitted changes.
4. Make a short written plan, then execute it end to end without asking
   questions (Jonas's working style). If the card is size XL, split it into
   new cards first.
5. Meet every "Done when" bullet and the Definition of Done in
   `00-how-to-work.md` §4: both builds 0 warnings, `scripts/test.sh` and the
   sanitizer run pass, new tests added, live tests for anything visible.
6. Mark the card done with a Log line, regenerate `roadmap/STATUS.md`
   (`python3 roadmap/tools/roadmap.py status`), commit locally as Jonas Lee
   with the co-author trailer, fast-forward `main`. **Never push.**
7. Finish with a plain-English summary for Jonas: what changed, what works,
   what doesn't yet, how to try it.

Extra instructions from Jonas (optional): <…>

---

## Variants

- **"Do the next thing"**: replace the first paragraph's card with: "Run
  `python3 roadmap/tools/roadmap.py next` and take the first ready card of
  the lowest phase that no other session is doing."
- **Several cards**: "Work through cards <A>, <B>, <C> in order; commit after
  each."
- **Planning only**: "Do not write code. Improve the cards in
  `roadmap/<file>.md` for area <X>: add missing detail, split XL cards,
  check dependencies; run `roadmap.py check`."
