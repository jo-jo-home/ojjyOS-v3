# ojjyOS bug tracker

A lightweight in-repo tracker until Jonas moves to GitHub issues
(roadmap card QA-30). One line per bug, nothing else — no commentary,
no status churn: a bug is either in `OPEN.md` or it is fixed and the
line moved out (with its fix noted in the linked card).

Created by Jonas Lee.

## Format

One line per bug in `OPEN.md`, pipe-separated:

    BUG-NNNN | <symptom, one sentence> | <how to reproduce> | <area> | <severity: low/med/high> | <roadmap card or -->

- `ID` — `BUG-` plus the next free four-digit number (1000 up).
- `symptom` — what a user sees, not a stack trace.
- `steps` — the shortest click path or command that shows it.
- `area` — the roadmap area prefix (NET, SHL, APP-FINDER, TIM, ...).
- `severity` — low / med / high (high = data loss, boot failure, or a
  security hole).
- `card` — the roadmap card that will fix it, `--` when none exists
  yet (finding a session should add the card, or say why not).

## Rules for AI sessions

- Found a bug you do not fix in the same session? Add one line here
  before wrapping up, then mention the file in your AGENTS.md LATEST
  block. Do not file TODO-style wishes here — those go to Jonas's
  ideas inbox (`/idea`); this file is for things that are broken.
- Fixed a bug listed here? Remove its line in the same commit (say so
  in the commit message), with the evidence in the card or bugfix box.
- Never delete someone else's line except by fixing it.
