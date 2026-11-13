#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
"""Check the test counts README.md and CLAUDE.md state against the suites.

The counts come from where the suites are defined: the TESTS_* lists of
the test Makefiles, the files in tests/irl/scotch/props/ and PIN_TESTS of
the Scotch pin `current`.  Each statement below is a regular expression
over one line of a document; its groups are the numbers that line states.
A statement that no line matches is an error too, so rewording a sentence
cannot silently drop it from the check: update the expression here.

Usage:
    scripts/check_doc_counts.py     # exit 1 on a wrong or missing statement
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def make_lists(path: str) -> dict[str, int]:
    """Word counts of the `NAME = a b \\` lists of a Makefile (literal words
    only: a list built from $(...) is skipped)."""
    text = (REPO_ROOT / path).read_text().replace("\\\n", " ")
    counts = {}
    for m in re.finditer(r"^([A-Z_]+)[ \t]*=[ \t]*(.*)$", text, re.MULTILINE):
        if "$" not in m.group(2):
            counts[m.group(1)] = len(m.group(2).split())
    return counts


def shell_list(path: str, name: str) -> int:
    """Word count of NAME="a b \\ c" in a shell fragment (a pin file)."""
    text = (REPO_ROOT / path).read_text().replace("\\\n", " ")
    m = re.search(rf'^{name}="([^"]*)"', text, re.MULTILINE)
    if not m:
        sys.exit(f"check_doc_counts: no {name}=\"...\" in {path}")
    return len(m.group(1).split())


def suites() -> dict[str, int]:
    st = make_lists("tests/selftest/Makefile")
    hr = make_lists("tests/from-hegel-rust/Makefile")
    mpi = make_lists("tests/mpi/Makefile")
    sc = make_lists("tests/irl/scotch/Makefile")
    n = {
        "st_pass": st["TESTS_PASS"], "st_fail": st["TESTS_FAIL"],
        "st_crash": st["TESTS_CRASH"], "st_health": st["TESTS_HEALTH"],
        "st_shrink": st["TESTS_SHRINK"],
        "hr_pass": hr["TESTS_PASS"], "hr_shrink": hr["TESTS_SHRINK"],
        "mpi_mpiexec": mpi["TESTS_MPIEXEC"],
        "mpi_spawn": mpi["TESTS_SPAWN"] + mpi["TESTS_SPAWN_SHRINK"],
        "sc_seq": sc["TESTS_PASS"], "sc_shrink": sc["TESTS_SHRINK"], "sc_mpi": sc["TESTS_MPI"],
        "sc_props": len(list((REPO_ROOT / "tests/irl/scotch/props").glob("test_*.c"))),
        "sc_green": shell_list("tests/irl/scotch/pins/current.conf", "PIN_TESTS"),
    }
    n["st"] = sum(n[k] for k in ("st_pass", "st_fail", "st_crash", "st_health", "st_shrink"))
    n["hr"] = n["hr_pass"] + n["hr_shrink"]
    n["mpi"] = n["mpi_mpiexec"] + n["mpi_spawn"]
    n["sc"] = n["sc_seq"] + n["sc_shrink"] + n["sc_mpi"]
    n["sc_open"] = n["sc_props"] - n["sc_green"]
    return n


# (document, expression, what each group must equal)
STATEMENTS = [
    ("README.md",
     r"^\| selftest \| (\d+) \((\d+) PASS, (\d+) FAIL, (\d+) CRASH, (\d+) HEALTH, (\d+) SHRINK\)",
     ("st", "st_pass", "st_fail", "st_crash", "st_health", "st_shrink")),
    ("README.md", r"^\| from-hegel-rust \| (\d+) \((\d+) PASS, (\d+) SHRINK\)",
     ("hr", "hr_pass", "hr_shrink")),
    ("README.md", r"^\| MPI \| (\d+) \((\d+) mpiexec, (\d+) spawn\)",
     ("mpi", "mpi_mpiexec", "mpi_spawn")),
    ("README.md",
     r"^\| Scotch IRL \| (\d+) \((\d+) sequential, (\d+) reducer demo, (\d+) PT-Scotch MPI\)"
     r" \+ a (\d+)-test property suite",
     ("sc", "sc_seq", "sc_shrink", "sc_mpi", "sc_props")),
    ("CLAUDE.md", r"^- `tests/selftest/` — (\d+) self-tests", ("st",)),
    ("CLAUDE.md", r"^- `tests/from-hegel-rust/` — (\d+) tests .*\((\d+) PASS, (\d+) SHRINK\)",
     ("hr", "hr_pass", "hr_shrink")),
    ("CLAUDE.md", r"^- `tests/mpi/` — (\d+) MPI tests", ("mpi",)),
    ("CLAUDE.md",
     r"^- `tests/irl/scotch/` — (\d+) Scotch integration tests .*, a (\d+)-test property suite"
     r" \(`props/`: (\d+) green .*, (\d+) witnesses of open bugs",
     ("sc", "sc_props", "sc_green", "sc_open")),
    ("CLAUDE.md", r"^make selftest-test +# (\d+) tests", ("st",)),
    ("CLAUDE.md", r"^make from-hegel-rust-test +# (\d+) tests \((\d+) PASS, (\d+) SHRINK\)",
     ("hr", "hr_pass", "hr_shrink")),
    ("CLAUDE.md", r"^make mpi-test +# (\d+) tests", ("mpi",)),
]


def main() -> int:
    n = suites()
    errors = []
    for doc, expr, keys in STATEMENTS:
        lines = (REPO_ROOT / doc).read_text().splitlines()
        hits = [(i, m) for i, line in enumerate(lines, 1) if (m := re.search(expr, line))]
        if not hits:
            errors.append(f"{doc}: no line matches /{expr}/ (reworded? update {Path(__file__).name})")
        for i, m in hits:
            stated = [int(g) for g in m.groups()]
            actual = [n[k] for k in keys]
            if stated != actual:
                errors.append(f"{doc}:{i}: states {stated}, the suites have {actual} ({', '.join(keys)})")
    for e in errors:
        print(f"check_doc_counts: {e}", file=sys.stderr)
    if not errors:
        print(f"Test counts in README.md and CLAUDE.md match the suites ({len(STATEMENTS)} statements).")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
