#!/usr/bin/env python3
"""Say who wrote each file, from the git history rather than from habit.

THE PROBLEM

These trees credit AppleWOA (now NT-for-ASi) in files AppleWOA never had, and
credit nobody in hundreds of files Aurora Silicon wrote outright. Both happened
the same way: a header travels with a copy-paste, and a new file starts from an
old one. Neither is a claim anyone made on purpose, which is exactly why it has
to be fixed from evidence instead of from memory.

WHAT DECIDES IT

The fork point. Every file is one of three things:

  upstream   present in the upstream tree at the fork point. Their copyright
             stands. Aurora's is added only where we have actually changed it.
  derived    we added it, but its content matches an upstream file -- a copy,
             a move, or a template for a new SoC. Both copyrights, theirs
             first, because that is the order the work happened in.
  ours       we added it and no upstream file resembles it. Aurora alone. If
             it carries an AppleWOA line, that line is a travelled header and
             is removed.

"Resembles" is measured, not guessed: difflib ratio against the best-matching
upstream file with the same basename, threshold 0.5.

WHAT IT WILL NOT DO

It never removes a copyright from a file classed upstream or derived, and it
never adds Aurora's to a file we have not touched. Attribution is only worth
fixing if the fix is itself accurate.
"""

from __future__ import annotations

import argparse
import difflib
import os
import re
import subprocess
import sys
from pathlib import Path

AURORA = "Copyright (c) 2026 Aurora Silicon"
SIMILARITY = 0.5

# Comment syntax by extension. EDK2 sources use C block comments even for ASL.
BLOCK = {".c", ".h", ".aslc", ".asl", ".rs", ".java"}
HASH = {".inf", ".dec", ".dsc", ".inc", ".py", ".sh", ".fdf", ".yml", ".yaml"}

APPLEWOA = re.compile(r"^\s*(?:[/*#\s]*)Copyright\s*\(c\).*(?:amarioguy|AppleWOA).*$",
                      re.IGNORECASE)
HAS_AURORA = re.compile(r"Aurora Silicon", re.IGNORECASE)
SPDX = re.compile(r"SPDX-License-Identifier")


def git(repo, *args):
    r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def classify(repo: Path, fork: str, tracked: list[str]) -> dict[str, str]:
    upstream = set((git(repo, "ls-tree", "-r", "--name-only", fork) or "").split())
    by_base: dict[str, list[str]] = {}
    for p in upstream:
        by_base.setdefault(os.path.basename(p), []).append(p)

    out = {}
    for f in tracked:
        if f in upstream:
            out[f] = "upstream"
            continue
        try:
            mine = (repo / f).read_text(errors="replace")
        except (OSError, UnicodeDecodeError):
            continue
        best = 0.0
        for cand in by_base.get(os.path.basename(f), []):
            other = git(repo, "show", f"{fork}:{cand}")
            if other:
                best = max(best, difflib.SequenceMatcher(None, mine, other).quick_ratio())
        out[f] = "derived" if best >= SIMILARITY else "ours"
    return out


def touched_since(repo: Path, fork: str) -> set[str]:
    diff = git(repo, "diff", "--name-only", f"{fork}..HEAD") or ""
    return set(diff.split())


def comment(path: Path, text: str, like: str | None = None) -> str | None:
    """The copyright line, in the same comment form as the line it joins.

    Form matters, not just prefix. A line that opens and closes its own block
    comment -- `/* SPDX-License-Identifier: MIT */` -- has to be mirrored whole;
    copying only its prefix yields `/* Copyright ...` and swallows the rest of
    the file into an unterminated comment.
    """
    if like is not None:
        stripped = like.strip()
        if stripped.startswith("/*") and stripped.endswith("*/"):
            indent = like[:len(like) - len(like.lstrip())]
            return f"{indent}/* {text} */"
        prefix = re.match(r"^[\s*#]*", like).group(0)
        if prefix.strip():
            return prefix + text
    ext = path.suffix
    if ext in BLOCK:
        return f" * {text}"
    if ext in HASH:
        return f"# {text}"
    return None


def _below_shebang(lines: list[str], at: int) -> int:
    """Never insert above a #! line -- that is what makes the file executable."""
    if at == 0 and lines and lines[0].startswith("#!"):
        return 1
    return at


def apply_to(path: Path, kind: str, modified: bool) -> str | None:
    """Rewrite one file's attribution. Returns a description, or None if unchanged."""
    try:
        text = path.read_text()
    except (OSError, UnicodeDecodeError):
        return None
    lines = text.splitlines(keepends=True)
    line = comment(path, AURORA)
    if line is None:
        return None

    if kind == "ours":
        kept = [l for l in lines if not APPLEWOA.match(l)]
        removed = len(lines) - len(kept)
        lines = kept
        if HAS_AURORA.search("".join(lines)):
            if removed:
                path.write_text("".join(lines))
                return f"removed {removed} travelled AppleWOA line(s)"
            return None
    else:
        if HAS_AURORA.search(text) or (kind == "upstream" and not modified):
            return None
        removed = 0

    # Convention is Copyright first, then SPDX-License-Identifier. Put the line
    # immediately above the SPDX tag, wearing that line's own comment prefix so
    # it matches whatever indentation the file already uses.
    at = None
    for i, l in enumerate(lines[:40]):
        if SPDX.search(l):
            at = i
            line = comment(path, AURORA, like=l) or line
            break
    if at is None:
        for i, l in enumerate(lines[:6]):
            if l.strip() in ("/**", "/*"):
                at = i + 1
                break
    if at is None:
        at = 0
        prefix = "/*\n" + line + "\n */\n\n" if path.suffix in BLOCK else line + "\n\n"
        lines.insert(_below_shebang(lines, 0), prefix)
    else:
        lines.insert(_below_shebang(lines, at), line + "\n")

    path.write_text("".join(lines))
    note = f"added Aurora ({kind})"
    return note + f", removed {removed} AppleWOA line(s)" if removed else note


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("repo", type=Path)
    ap.add_argument("--fork", required=True, help="fork-point commit")
    ap.add_argument("--ext", default=".c,.h,.aslc,.asl,.inf,.dec,.dsc,.inc,.py,.sh,.rs")
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    exts = set(args.ext.split(","))
    tracked = [f for f in (git(args.repo, "ls-files") or "").split()
               if Path(f).suffix in exts]
    kinds = classify(args.repo, args.fork, tracked)
    modified = touched_since(args.repo, args.fork)

    counts: dict[str, int] = {}
    changes = 0
    for f, kind in sorted(kinds.items()):
        counts[kind] = counts.get(kind, 0) + 1
        if not args.apply:
            continue
        if apply_to(args.repo / f, kind, f in modified):
            changes += 1

    for k in ("upstream", "derived", "ours"):
        print(f"  {k:<10} {counts.get(k, 0)}")
    print(f"  {'changed' if args.apply else 'would change'}: {changes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
