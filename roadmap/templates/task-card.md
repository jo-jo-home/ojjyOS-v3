# Card template

Copy the block below into the right area file, pick the next free number
for the prefix (`python3 roadmap/tools/roadmap.py stats` lists prefixes),
fill every section, run `python3 roadmap/tools/roadmap.py check`.

Writing guidance:
- **Context**: what exists today (files, behaviour, limits) — a new AI
  session must understand the starting point without reading the history.
- **Goal**: the end state in plain words.
- **Steps**: concrete, ordered; each step should be a natural commit point.
- **Done when**: measurable, testable statements (numbers, observable
  behaviour). Avoid "works well".
- **Tests**: name existing test files to extend or new ones to create.
- **Pitfalls**: traps you know about (specs, platform quirks, past bugs).
- **Refs**: specifications, project documents, decision IDs.
- Sizes: S (1 session), M (2–3), L (4–8), XL (must split before work).

```markdown
### AREA-NN — Short imperative title
- Phase: Pn · Size: M · Status: todo
- Depends: OTHER-01, OTHER-02
- Files: path/one.c, path/two/ (new)

**Context.** …
**Goal.** …
**Steps.**
1. …
2. …
**Done when.**
- …
**Tests.** …
**Pitfalls.** …
**Refs.** …
```

When the card is finished, add a log line at its end:

```markdown
**Log.** 2027-03-14 done in `abc1234`; evidence `build/sch-03-*`; limits: … 
```
