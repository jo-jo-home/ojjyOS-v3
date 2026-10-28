#!/usr/bin/env python3
"""Validate THIRD_PARTY.md (roadmap card DOC-03).

Checks that the third-party register keeps its required sections and key
entries: the fonts/ table with both shipped typefaces and their licence,
the packages/ section for future ojjypkg programs, the built-in CA roots
(NET-02), and the Apple
wallpapers policy (local only, never shipped or committed). Run from
scripts/test.sh; roadmap card QA-40 re-runs it for releases.

Created by Jonas Lee.
"""
import re
import sys
from pathlib import Path


def main() -> int:
    path = Path(__file__).resolve().parent.parent / "THIRD_PARTY.md"
    if not path.is_file():
        print(f"THIRD_PARTY: missing {path.name} (roadmap card DOC-03)")
        return 1
    text = path.read_text(encoding="utf-8")
    problems = []

    def need(pattern: str, what: str) -> None:
        if not re.search(pattern, text, re.MULTILINE):
            problems.append(what)

    need(r"^## fonts/", "no '## fonts/' section")
    need(r"^## packages/", "no '## packages/' section (future ojjypkg register)")
    need(r"Inter.*OFL|OFL.*Inter", "fonts/ table: Inter entry with its OFL licence missing")
    need(r"JetBrains Mono", "fonts/ table: JetBrains Mono entry missing")
    need(r"JetBrainsMono-Regular\.ttf", "fonts/ table: JetBrains Mono file name missing")
    need(r"Inter-OFL-LICENSE\.txt", "fonts/ table: Inter licence file name missing")
    need(r"wallpapers/local", "wallpapers policy: git-ignored wallpapers/local/ folder not named")
    need(r"never committed", "wallpapers policy: 'never committed' promise missing")
    need(r"never.*pushed|never.*ship", "wallpapers policy: never shipped/pushed promise missing")
    need(r"/System/Legal/THIRD_PARTY\.txt", "in-image copy /System/Legal/THIRD_PARTY.txt not named")
    need(r"^## CA root certificates", "no '## CA root certificates' section (NET-02 trust store)")
    need(r"roots_data\.c", "CA roots: kernel/src/crypto/roots_data.c not named")
    need(r"tools/mkroots\.py", "CA roots: generator tools/mkroots.py not named")

    if problems:
        for problem in problems:
            print(f"THIRD_PARTY: {problem}")
        return 1
    print(f"THIRD_PARTY: register OK ({len(text.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
