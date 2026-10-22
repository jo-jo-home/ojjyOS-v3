#!/usr/bin/env python3
"""ojjyOS roadmap tool (created by Jonas Lee's project; see roadmap/README.md).

Parses every card (`### ID — Title` + field lines) in roadmap/*.md and:

  check            validate cards: fields, sections, unknown deps, cycles,
                   dependencies scheduled in a later phase (warning)
  status           regenerate roadmap/STATUS.md
  next [N]         list ready cards (todo, all deps done), lowest phase first
  show ID          print one card
  deps ID          print the dependency tree of a card
  blocks ID        list cards that depend on ID (directly or transitively)
  stats            counts per phase / area / size / status
  critical [ID]    longest dependency chain weighted by size (to ID or overall)

Exit code 1 on validation errors (so it can run in scripts/test.sh).
"""
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import List, Optional

ROOT = Path(__file__).resolve().parent.parent
HEADER = re.compile(r'^### ([A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+) — (.+?)\s*$')
META = re.compile(r'^- Phase: P(\d+) · Size: (S|M|L|XL) · Status: (todo|doing|done|blocked)\s*$')
DEPS = re.compile(r'^- Depends: (.+?)\s*$')
FILES = re.compile(r'^- Files: (.+?)\s*$')
ID_RE = re.compile(r'[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+')
REQUIRED = ('**Goal.**', '**Done when.**', '**Tests.**')
SIZE_ORDER = {'S': 1, 'M': 2, 'L': 3, 'XL': 4}


class Card:
    def __init__(self, cid, title, path, line):
        self.id, self.title, self.path, self.line = cid, title, path, line
        self.phase: Optional[int] = None
        self.size: Optional[str] = None
        self.status: Optional[str] = None
        self.deps: List[str] = []
        self.files = ''
        self.body: List[str] = []

    @property
    def area(self):
        return self.id.rsplit('-', 1)[0]

    def where(self):
        return f'{self.path.name}:{self.line}'


def parse():
    cards, errors = {}, []
    for path in sorted(ROOT.glob('*.md')):
        if path.name == 'STATUS.md':
            continue
        cur = None
        fenced = False
        for n, raw in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
            if raw.startswith('```'):
                fenced = not fenced
                continue
            if fenced:
                continue
            m = HEADER.match(raw)
            if m:
                cur = Card(m.group(1), m.group(2), path, n)
                if cur.id in cards:
                    errors.append(f'{cur.where()}: duplicate id {cur.id} (first at {cards[cur.id].where()})')
                cards[cur.id] = cur
                continue
            if raw.startswith('## ') or raw.startswith('# '):
                cur = None
                continue
            if cur is None:
                continue
            cur.body.append(raw)
            if (m := META.match(raw)):
                cur.phase, cur.size, cur.status = int(m.group(1)), m.group(2), m.group(3)
            elif (m := DEPS.match(raw)):
                txt = m.group(1).strip()
                cur.deps = [] if txt in ('—', '-', 'none') else ID_RE.findall(txt)
            elif (m := FILES.match(raw)):
                cur.files = m.group(1)
    return cards, errors


def validate(cards, errors):
    warnings = []
    for c in cards.values():
        if c.phase is None:
            errors.append(f'{c.where()}: {c.id} missing/malformed "- Phase: Pn · Size: X · Status: s" line')
        text = '\n'.join(c.body)
        for sec in REQUIRED:
            if sec not in text:
                errors.append(f'{c.where()}: {c.id} missing section {sec}')
        if not any(DEPS.match(l) for l in c.body):
            errors.append(f'{c.where()}: {c.id} missing "- Depends:" line')
        for d in c.deps:
            if d not in cards:
                errors.append(f'{c.where()}: {c.id} depends on unknown card {d}')
            elif d == c.id:
                errors.append(f'{c.where()}: {c.id} depends on itself')
            elif cards[d].phase is not None and c.phase is not None and cards[d].phase > c.phase:
                warnings.append(f'{c.where()}: {c.id} (P{c.phase}) depends on {d} (P{cards[d].phase}) scheduled later')
        if c.status == 'done' and any(cards[d].status != 'done' for d in c.deps if d in cards):
            warnings.append(f'{c.where()}: {c.id} is done but a dependency is not')
        if c.size == 'XL' and c.status == 'doing':
            errors.append(f'{c.where()}: {c.id} is XL and must be split before work starts')
    # cycles
    state = {}

    def visit(cid, stack):
        state[cid] = 1
        for d in cards[cid].deps:
            if d not in cards:
                continue
            if state.get(d) == 1:
                cyc = stack[stack.index(d):] + [d] if d in stack else [cid, d]
                errors.append('dependency cycle: ' + ' -> '.join(cyc))
            elif not state.get(d):
                visit(d, stack + [d])
        state[cid] = 2

    for cid in cards:
        if not state.get(cid):
            visit(cid, [cid])
    return errors, warnings


def ready(cards):
    out = [c for c in cards.values()
           if c.status == 'todo' and all(cards[d].status == 'done' for d in c.deps if d in cards)]
    return sorted(out, key=lambda c: (c.phase if c.phase is not None else 99, SIZE_ORDER.get(c.size or "M", 9), c.id))


def write_status(cards):
    by_phase = defaultdict(list)
    for c in cards.values():
        by_phase[c.phase].append(c)
    total = len(cards)
    done = sum(c.status == 'done' for c in cards.values())
    lines = ['# STATUS — generated by `python3 roadmap/tools/roadmap.py status`', '',
             'Do not edit by hand: change `Status:` inside the card and regenerate.', '',
             f'**{done} / {total} cards done.** Ready to start now: '
             + ', '.join(c.id for c in ready(cards)[:25]) + (' …' if len(ready(cards)) > 25 else ''), '']
    lines += ['| Phase | Cards | todo | doing | done | blocked |', '|---|---|---|---|---|---|']
    for p in sorted(k for k in by_phase if k is not None):
        cs = by_phase[p]
        cnt = lambda s: sum(c.status == s for c in cs)
        lines.append(f'| P{p} | {len(cs)} | {cnt("todo")} | {cnt("doing")} | {cnt("done")} | {cnt("blocked")} |')
    lines.append('')
    for p in sorted(k for k in by_phase if k is not None):
        lines += [f'## P{p}', '', '| ID | Title | Size | Status | Depends | File |', '|---|---|---|---|---|---|']
        for c in sorted(by_phase[p], key=lambda c: c.id):
            deps = ', '.join(c.deps) if c.deps else '—'
            lines.append(f'| {c.id} | {c.title} | {c.size} | {c.status} | {deps} | {c.path.name} |')
        lines.append('')
    (ROOT / 'STATUS.md').write_text('\n'.join(lines), encoding='utf-8')
    return total, done


def main(argv):
    cmd = argv[1] if len(argv) > 1 else 'check'
    cards, errors = parse()
    errors, warnings = validate(cards, errors)
    if cmd == 'check':
        for w in warnings:
            print('warning:', w)
        for e in errors:
            print('error:', e)
        print(f'{len(cards)} cards, {len(errors)} errors, {len(warnings)} warnings')
        return 1 if errors else 0
    if errors:
        print(f'{len(errors)} validation errors; run "check" first', file=sys.stderr)
    if cmd == 'status':
        total, done = write_status(cards)
        print(f'STATUS.md written: {done}/{total} done')
    elif cmd == 'next':
        n = int(argv[2]) if len(argv) > 2 else 20
        for c in ready(cards)[:n]:
            print(f'{c.id:14} P{c.phase} {c.size:2} {c.title}  ({c.path.name})')
    elif cmd == 'show':
        c = cards.get(argv[2])
        if not c:
            print('unknown card'); return 1
        print(f'### {c.id} — {c.title}  [{c.where()}]')
        print('\n'.join(c.body).rstrip())
    elif cmd == 'deps':
        def tree(cid, depth, seen):
            c = cards.get(cid)
            mark = c.status if c else 'UNKNOWN'
            print('  ' * depth + f'{cid} [{mark}] {c.title if c else ""}')
            if cid in seen:
                return
            seen.add(cid)
            for d in (c.deps if c else []):
                tree(d, depth + 1, seen)
        tree(argv[2], 0, set())
    elif cmd == 'blocks':
        target = argv[2]
        rev = defaultdict(set)
        for c in cards.values():
            for d in c.deps:
                rev[d].add(c.id)
        seen, todo = set(), [target]
        while todo:
            for x in rev[todo.pop()]:
                if x not in seen:
                    seen.add(x); todo.append(x)
        for x in sorted(seen, key=lambda i: (cards[i].phase, i)):
            print(f'{x:14} P{cards[x].phase} {cards[x].title}')
    elif cmd == 'critical':
        weight = {'S': 1.0, 'M': 2.5, 'L': 6.0, 'XL': 12.0}
        memo = {}

        def longest(cid):
            if cid in memo:
                return memo[cid]
            c = cards[cid]
            best, chain = 0.0, []
            for d in c.deps:
                if d in cards and cards[d].status != 'done':
                    w, ch = longest(d)
                    if w > best:
                        best, chain = w, ch
            own = 0.0 if c.status == 'done' else weight.get(c.size or 'M', 2.5)
            memo[cid] = (best + own, chain + [cid])
            return memo[cid]

        if len(argv) > 2:
            total, chain = longest(argv[2])
        else:
            total, chain = max((longest(cid) for cid in cards), key=lambda t: t[0])
        print(f'chain weight {total:.1f} sessions (S=1, M=2.5, L=6, XL=12), {len(chain)} cards:')
        for cid in chain:
            c = cards[cid]
            print(f'  {cid:14} P{c.phase} {c.size:2} {c.title}')
    elif cmd == 'stats':
        for key in ('phase', 'area', 'size', 'status'):
            counts = defaultdict(int)
            for c in cards.values():
                counts[getattr(c, key)] += 1
            print(key + ':', ', '.join(f'{k}={v}' for k, v in sorted(counts.items(), key=lambda kv: str(kv[0]))))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
