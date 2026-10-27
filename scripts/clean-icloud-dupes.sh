#!/bin/bash
#
# clean-icloud-dupes.sh — list (and with --yes, delete) the
# "name 2.ext" conflict copies macOS/iCloud sync leaves behind when a
# build product changes on two machines at once.
#
# SAFETY (roadmap card QA-22): without --yes nothing is deleted. With
# --yes, only files are deleted that are BOTH (a) named like a conflict
# copy of a BUILD OUTPUT extension (.o .d .obj .bin .elf .efi .a .lib
# .img) and (b) not tracked by git. Sources, documents and tracked files
# are never touched — they are only listed.
#
# Created by Jonas Lee.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ASSUME_YES=0
for arg in "$@"; do
    case "$arg" in
        --yes) ASSUME_YES=1 ;;
        *) echo "usage: $0 [--yes]   (without --yes: list only)" >&2; exit 2 ;;
    esac
done

found=0
listed=0
deleted=0
while IFS= read -r -d '' path; do
    base="$(basename "$path")"
    ext="${base##*.}"
    stem="${base%.*}"
    # Conflict-copy shape: "<something> <digits>.<build-output ext>"
    case "$ext" in
        o|d|obj|bin|elf|efi|a|lib|img) ;;
        *) continue ;;
    esac
    case "$stem" in
        *" "[0-9]*[!0-9]*) continue ;;   # digits must be the whole tail: "name 2"
        *" "[0-9]*) ;;
        *) continue ;;
    esac

    found=$((found + 1))
    rel="${path#"$ROOT"/}"
    if [ -n "$(git -C "$ROOT" ls-files -- "$rel")" ]; then
        echo "TRACKED, left alone: $rel"
        continue
    fi
    if [ "$ASSUME_YES" = 1 ]; then
        rm -- "$path"
        echo "deleted: $rel"
        deleted=$((deleted + 1))
    else
        echo "conflict copy: $rel"
        listed=$((listed + 1))
    fi
done < <(find "$ROOT" -name ".git" -prune -o -type f -print0)

if [ "$ASSUME_YES" = 1 ]; then
    echo "found $found conflict copies of build outputs; deleted $deleted"
else
    echo "found $found conflict copies of build outputs (listed $listed); nothing deleted — rerun with --yes to remove them"
fi
