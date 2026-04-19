#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
"""Verify or regenerate transcluded code snippets in docs/*.md.

Every C / C++ code fence in docs/ must be preceded by one of:

    <!-- /include path/to/file.c:LINE1-LINE2 -->
    ```c
    ... content matching lines LINE1..LINE2 of the source ...
    ```
    <!-- /endinclude -->

    <!-- /ignore reason-here -->
    ```c
    ... non-runnable / pedagogical / counter-example code ...
    ```

Unmarked ```c / ```cpp fences fail the check — every snippet must
either be verified against a source file or have an explicit reason
for not being verified.

Usage:
    scripts/check_doc_includes.py          # check, exit 1 on drift or unmarked fences
    scripts/check_doc_includes.py --fix    # regenerate transcluded snippets in place
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DOCS_DIR = REPO_ROOT / "docs"

# Subdirectories under docs/ that are historical archives, not
# maintained documentation — excluded from the strict fence check.
EXCLUDED_DIRS = {"saved_sessions_for_history"}

INCLUDE_RE = re.compile(
    r"(?P<header><!--\s*/include\s+(?P<src>\S+?):(?P<lo>\d+)-(?P<hi>\d+)\s*-->\n"
    r"```[a-zA-Z0-9_+-]*\n)"
    r"(?P<body>.*?)"
    r"(?P<trailer>```\n<!--\s*/endinclude\s*-->)",
    re.DOTALL,
)

IGNORE_RE = re.compile(
    r"(?P<marker><!--\s*/ignore\s+(?P<reason>.+?)\s*-->\n)"
    r"(?P<fence_open>```[a-zA-Z0-9_+-]*\n)"
    r"(?P<body>.*?)"
    r"(?P<fence_close>```)",
    re.DOTALL,
)

# Any C / C++ code fence opener.  Matches ``` followed by c, cpp, or c++
# (case-insensitive), possibly with extra text on the same line.
C_FENCE_RE = re.compile(
    r"^```(c|cpp|c\+\+)\b.*$",
    re.MULTILINE | re.IGNORECASE,
)


def read_slice(path: Path, lo: int, hi: int) -> str:
    lines = path.read_text().splitlines(keepends=True)
    if lo < 1 or hi > len(lines) or lo > hi:
        raise ValueError(
            f"bad slice {lo}-{hi} for {path} ({len(lines)} lines)"
        )
    return "".join(lines[lo - 1:hi])


def line_of(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


def process(md_path: Path, fix: bool) -> bool:
    """Return True if up-to-date, False if drift / unmarked fences / missing source."""
    text = md_path.read_text()
    ok = True

    # Collect protected ranges from /include and /ignore markers.
    protected = [
        (m.start(), m.end())
        for m in INCLUDE_RE.finditer(text)
    ]
    protected += [
        (m.start(), m.end())
        for m in IGNORE_RE.finditer(text)
    ]

    def in_protected(pos: int) -> bool:
        return any(s <= pos < e for s, e in protected)

    # Any C fence not inside a protected range = error.
    for m in C_FENCE_RE.finditer(text):
        if not in_protected(m.start()):
            print(
                f"{md_path.relative_to(REPO_ROOT)}:{line_of(text, m.start())}: "
                f"unmarked C fence '{m.group(0).strip()}' — "
                f"add <!-- /include path:lo-hi --> + <!-- /endinclude -->, "
                f"or <!-- /ignore reason --> above the fence",
                file=sys.stderr,
            )
            ok = False

    # Verify /include content (and optionally fix).
    def sub(match: re.Match) -> str:
        nonlocal ok
        src_rel = match["src"]
        lo, hi = int(match["lo"]), int(match["hi"])
        src_path = (REPO_ROOT / src_rel).resolve()
        try:
            expected = read_slice(src_path, lo, hi)
        except (FileNotFoundError, ValueError) as e:
            print(
                f"{md_path.relative_to(REPO_ROOT)}: {e}",
                file=sys.stderr,
            )
            ok = False
            return match.group(0)
        if match["body"] != expected:
            if not fix:
                print(
                    f"{md_path.relative_to(REPO_ROOT)}: drift for "
                    f"{src_rel}:{lo}-{hi}",
                    file=sys.stderr,
                )
                ok = False
            return match["header"] + expected + match["trailer"]
        return match.group(0)

    updated = INCLUDE_RE.sub(sub, text)
    if fix and updated != text:
        md_path.write_text(updated)
        print(f"{md_path.relative_to(REPO_ROOT)}: updated")

    return ok


def main() -> int:
    fix = "--fix" in sys.argv[1:]
    ok = True
    for md in sorted(DOCS_DIR.rglob("*.md")):
        if any(part in EXCLUDED_DIRS for part in md.relative_to(DOCS_DIR).parts):
            continue
        ok = process(md, fix) and ok
    if not ok and not fix:
        print(
            "docs issues detected; fixable drift can be regenerated with: "
            "scripts/check_doc_includes.py --fix",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
