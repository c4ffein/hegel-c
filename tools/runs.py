#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
"""View and check docs/runs.toml, docs/corpus.toml and the pins' bug references.

docs/runs.toml: each run that pointed hegel-c at bugs, with a verdict per
target, so a newer hegel-c / skill / model can be checked against an older
one.  docs/corpus.toml: the rediscovery targets those runs are scored on.

The bugs themselves are tracked in c4ffein/bugs (private), each with a uuid
that never changes.  Here a bug is named "bugs:<uuid>": a corpus target's
`bug`, a run's `found` and result targets, a pin's PIN_BUG.  lint checks the
form; with a checkout of c4ffein/bugs (HEGEL_BUGS_REPO, default ../bugs) it
also checks that each uuid exists there (data/uuids.txt), and `runs` prints
the bugs' slugs.

  tools/runs.py runs [--project P]   (targets x runs, regressions flagged)
  tools/runs.py lint

Python >= 3.11, standard library only.  Colors follow the terminal: off when
piped or with NO_COLOR, forced with FORCE_COLOR.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

if sys.version_info < (3, 11):
    sys.exit("runs.py needs Python 3.11+ (tomllib)")
import tomllib

ROOT = Path(__file__).resolve().parent.parent
RUNS = ROOT / "docs" / "runs.toml"
CORPUS = ROOT / "docs" / "corpus.toml"
PROJECTS = {"linux", "scotch"}

# ---- bugs (c4ffein/bugs)
BUG_RE = re.compile(r"^bugs:[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
BUGS_REPO = Path(os.environ.get("HEGEL_BUGS_REPO", ROOT.parent / "bugs"))


def known_bugs() -> dict[str, str] | None:
    """uuid -> slug from a c4ffein/bugs checkout, or None when there is none."""
    ids = BUGS_REPO / "data" / "uuids.txt"
    if not ids.is_file():
        return None
    out: dict[str, str] = {}
    for line in ids.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if parts and re.match(r"^[0-9a-f-]{36}$", parts[0]):
            out[parts[0]] = parts[1] if len(parts) > 1 else ""
    return out


def check_bug(bad, ref: str, what: str, bugs: dict[str, str] | None) -> None:
    if not BUG_RE.match(ref):
        bad(f"{what} {ref!r} must be bugs:<uuid>")
    elif bugs is not None and ref[5:] not in bugs:
        bad(f"{what} {ref}: no such uuid in {BUGS_REPO}/data/uuids.txt")


def bug_name(ref: str, bugs: dict[str, str] | None) -> str:
    slug = (bugs or {}).get(ref[5:]) if BUG_RE.match(ref) else None
    return slug or ref[:13]


# ---- corpus (docs/corpus.toml)
TGT_REQUIRED = ["title", "subsystem", "classes", "provenance", "tier", "status"]
TGT_OPTIONAL = ["fixed_by", "bug", "fixed", "author", "source", "harness_dir", "pin",
                "effort", "mechanism", "harness", "property", "why", "notes"]
TGT_CLASSES = {"leak", "uaf", "double-free", "oob", "overflow", "off-by-one", "uninit",
               "null-deref", "hang", "wrong-result", "error-path"}
TGT_STATUSES = {"candidate", "rediscovered", "harness-only", "missed", "found-novel",
                "found-and-submitted"}
TGT_EFFORT = {"ready", "small", "medium", "large"}
TGT_TIERS = {"A", "B", "C"}
FOUND = {"found-novel", "found-and-submitted"}       # our own finds (bug = ...)

# ---- runs (docs/runs.toml)
RUN_KINDS = {"blind-recall", "harness", "pins", "hunt"}
RUN_REQUIRED = ["id", "date", "kind", "project", "summary"]
RUN_OPTIONAL = ["blind", "baseline", "hegel_c", "libhegel", "model", "skill", "dir", "found",
                "notes", "result"]
RUN_BLIND = {"clean", "leaked"}
RUNS_DIR = "docs/runs"
RESULT_KEYS = {"target", "verdict", "blind_credit", "note"}
HUNT_VERDICTS = ["hit", "partial", "miss", "false-positive"]
PIN_VERDICTS = ["as-expected", "unexpected"]
GOOD_VERDICTS = {"hit", "as-expected"}

DATE_RE = re.compile(r"^\d{4}-\d{2}(-\d{2})?$")
ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)+$")
SHA_RE = re.compile(r"^[0-9a-f]{7,40}$")
PINS_GLOB = "tests/irl/*/pins/*.conf"



def load_runs() -> list[dict]:
    if not RUNS.exists():
        return []
    with open(RUNS, "rb") as f:
        return tomllib.load(f).get("run", [])


def pin_vars(ref: str) -> dict[str, str]:
    """KEY="value" assignments of a pin file (tests/irl/<p>/pins/<pin>.conf)."""
    out = {}
    try:
        text = (ROOT / ref).read_text()
    except OSError:
        return out
    for line in text.splitlines():
        m = re.match(r'^([A-Z_]+)="([^"]*)"', line.strip())
        if m:
            out[m.group(1)] = m.group(2)
    return out


def load_corpus() -> list[dict]:
    try:
        with open(CORPUS, "rb") as f:
            return tomllib.load(f).get("target", [])
    except OSError:
        return []


def corpus_shas() -> dict[str, dict]:
    """docs/corpus.toml targets by 12-hex fix SHA."""
    return {t["fixed_by"][:12]: t for t in load_corpus() if t.get("fixed_by")}


def lint_corpus(bugs: dict[str, str] | None, tracked: set[str] | None) -> list[str]:
    errors: list[str] = []
    seen: set[str] = set()
    for i, t in enumerate(load_corpus()):
        key = t.get("fixed_by") or t.get("bug") or f"#{i + 1}"

        def bad(msg: str, key=key) -> None:
            errors.append(f"corpus.toml: {key}: {msg}")

        for k in TGT_REQUIRED:
            if not t.get(k):
                bad(f"missing {k}")
        for k in t:
            if k not in TGT_REQUIRED + TGT_OPTIONAL:
                bad(f"unknown field {k}")
        if bool(t.get("fixed_by")) == bool(t.get("bug")):
            bad("needs exactly one of fixed_by and bug")
        if t.get("fixed_by") and not re.match(r"^[0-9a-f]{12,40}$", t["fixed_by"]):
            bad("fixed_by must be 12 to 40 hex digits")
        k12 = (t.get("fixed_by") or "")[:12] or t.get("bug")
        if k12 in seen:
            bad("listed twice")
        seen.add(k12)
        for k, allowed in (("tier", TGT_TIERS), ("status", TGT_STATUSES),
                           ("effort", TGT_EFFORT), ("provenance", {"curated", "found"})):
            if t.get(k) and t[k] not in allowed:
                bad(f"{k} must be one of {', '.join(sorted(allowed))}")
        for c in t.get("classes", []):
            if c not in TGT_CLASSES:
                bad(f"class {c} not in {', '.join(sorted(TGT_CLASSES))}")
        if t.get("fixed") and not re.match(r"^\d{4}-\d{2}-\d{2}$", t["fixed"]):
            bad("fixed must be YYYY-MM-DD")
        if t.get("bug"):
            check_bug(bad, t["bug"], "bug", bugs)
            if t.get("provenance") != "found":
                bad("our own find's target has provenance found")
            if t.get("status") not in FOUND:
                bad("our own find's target has status found-novel or found-and-submitted")
        elif t.get("status") in FOUND:
            bad(f"status {t['status']} is for our own finds (bug = ...)")
        if t.get("status") in ("rediscovered", "harness-only", "missed") and not t.get("harness_dir"):
            bad(f"status {t['status']} needs a harness_dir")
        for k in ("harness_dir", "pin"):
            p = t.get(k)
            if not p:
                continue
            if not (ROOT / p).exists():
                bad(f"{k} {p} does not exist")
            elif tracked is not None and not is_tracked(p, tracked):
                bad(f"{k} {p} is not in git")
        pin = t.get("pin")
        if pin and (ROOT / pin).is_file() and t.get("fixed_by"):
            if t["fixed_by"][:12] not in (ROOT / pin).read_text(encoding="utf-8"):
                bad(f"pin {pin} doesn't name fix {t['fixed_by']}")
        if t.get("harness_dir") and not pin:
            bad("a target with a harness_dir names its pin")
    return errors


def run_verdicts(kind: str) -> list[str]:
    return PIN_VERDICTS if kind == "pins" else HUNT_VERDICTS


def lint_runs(runs: list[dict], bugs: dict[str, str] | None,
              tracked: set[str] | None) -> list[str]:
    errors: list[str] = []
    shas = corpus_shas()
    rids = [r.get("id") for r in runs]
    seen: set[str] = set()
    for i, r in enumerate(runs):
        rid = r.get("id") or f"#{i + 1}"

        def bad(msg: str, rid=rid) -> None:
            errors.append(f"runs.toml: run {rid}: {msg}")

        for k in RUN_REQUIRED:
            if not r.get(k):
                bad(f"missing {k}")
        for k in r:
            if k not in RUN_REQUIRED + RUN_OPTIONAL:
                bad(f"unknown field {k}")
        if r.get("id"):
            if r["id"] in seen:
                bad("duplicate id")
            seen.add(r["id"])
            if not ID_RE.match(r["id"]) or not r["id"].startswith(str(r.get("date", "")) + "-"):
                bad("id must be <date>-<what>, kebab-case")
        if r.get("date") and not re.match(r"^\d{4}-\d{2}-\d{2}$", r["date"]):
            bad("date must be YYYY-MM-DD")
        kind = r.get("kind")
        if kind and kind not in RUN_KINDS:
            bad(f"kind must be one of {', '.join(sorted(RUN_KINDS))}")
        if r.get("project") and r["project"] not in PROJECTS:
            bad(f"project must be one of {', '.join(sorted(PROJECTS))}")
        if kind == "blind-recall" and r.get("blind") not in RUN_BLIND:
            bad("blind-recall needs blind = clean | leaked")
        if kind != "blind-recall" and "blind" in r:
            bad("blind only applies to blind-recall runs")
        if r.get("baseline") and r["baseline"] not in rids:
            bad(f"baseline {r['baseline']} is not a run")
        if r.get("hegel_c") and not SHA_RE.match(r["hegel_c"]):
            bad("hegel_c must be a commit SHA")
        for ref in r.get("found", []):
            check_bug(bad, ref, "found", bugs)
        if r.get("dir"):
            d = r["dir"].rstrip("/")
            if d != f"{RUNS_DIR}/{r.get('id')}":
                bad(f"dir should be {RUNS_DIR}/{r.get('id')}")
            elif not (ROOT / d).is_dir():
                bad(f"dir {d} does not exist")
            elif tracked is not None and not is_tracked(d, tracked):
                bad(f"dir {d} is not in git")
        results = r.get("result", [])
        if kind == "hunt" and results:
            bad("a hunt has no targets: put what it turned up in found")
        if kind and kind != "hunt" and not results:
            bad("no results")
        targets: set[str] = set()
        for res in results:
            t = res.get("target", "")
            for k in res:
                if k not in RESULT_KEYS:
                    bad(f"result {t}: unknown field {k}")
            if not t:
                bad("result without target")
            elif t in targets:
                bad(f"result {t}: target listed twice")
            targets.add(t)
            if t.startswith("tests/irl/"):
                if not t.endswith(".conf") or "/pins/" not in t or not (ROOT / t).is_file():
                    bad(f"result {t}: not a pin file")
            elif SHA_RE.match(t):
                if t[:12] not in shas:
                    bad(f"result {t}: no target with fixed_by {t} in docs/corpus.toml")
            elif t.startswith("bugs:"):
                check_bug(bad, t, "result", bugs)
            else:
                bad(f"result {t}: not a corpus SHA, a bugs:<uuid> or a pin file")
            if kind and res.get("verdict") not in run_verdicts(kind):
                bad(f"result {t}: verdict must be {' | '.join(run_verdicts(kind))}")
            if "blind_credit" in res:
                if kind != "blind-recall":
                    bad(f"result {t}: blind_credit only applies to blind-recall runs")
                elif not isinstance(res["blind_credit"], bool):
                    bad(f"result {t}: blind_credit must be true or false")
                elif res["blind_credit"] and res.get("verdict") != "hit":
                    bad(f"result {t}: blind credit needs a hit")
    linked = {r["dir"].rstrip("/").split("/")[-1] for r in runs if r.get("dir")}
    if (ROOT / RUNS_DIR).is_dir():
        for p in sorted((ROOT / RUNS_DIR).iterdir()):
            if p.name not in linked:
                errors.append(f"runs.toml: {RUNS_DIR}/{p.name} belongs to no run: add its run "
                              f"or move it")
    return errors


def target_name(t: str, shas: dict[str, dict], bugs: dict[str, str] | None) -> str:
    if t.startswith("bugs:"):
        return "bug " + bug_name(t, bugs)
    e = shas.get(t[:12]) if SHA_RE.match(t) else None
    if e and e.get("harness_dir"):
        return f"{Path(e['harness_dir']).name} ({t[:12]})"
    if t.startswith("tests/irl/"):
        return "pin " + Path(t).stem
    return t


def regressions(runs: list[dict]) -> list[tuple[str, str, str]]:
    """(target, "hit in <run>", "<verdict> in <later run>"), per kind, in date order."""
    out = []
    by_kind: dict[str, list[dict]] = {}
    for r in sorted(runs, key=lambda r: (r.get("date", ""), r.get("id", ""))):
        by_kind.setdefault(r.get("kind", ""), []).append(r)
    for rs in by_kind.values():
        last_good: dict[str, str] = {}
        for r in rs:
            for res in r.get("result", []):
                t = res.get("target")
                if res.get("verdict") in GOOD_VERDICTS:
                    last_good[t] = (r["id"], res["verdict"])
                elif t in last_good:
                    good, verdict = last_good.pop(t)
                    out.append((t, f"{verdict} in {good}", f"{res.get('verdict')} in {r['id']}"))
    return out


VERDICT_STYLE = {"hit": ("fixed-upstream", "hit"), "partial": ("suspected", "part"),
                 "miss": ("confirmed", "miss"), "false-positive": ("confirmed", "f+"),
                 "as-expected": ("fixed-upstream", "ok"), "unexpected": ("confirmed", "BAD")}


def cmd_runs(args) -> int:
    paint = Paint(use_color())
    shas = corpus_shas()
    bugs = known_bugs()
    runs = sorted((r for r in load_runs() if not args.project or r.get("project") == args.project),
                  key=lambda r: (r.get("date", ""), r.get("id", "")))
    print(paint("bold", f"hegel-c runs — {len(runs)}"))
    for n, r in enumerate(runs, 1):
        extra = []
        if r.get("blind"):
            extra.append(f"blind: {r['blind']}")
        for k in ("hegel_c", "libhegel", "model"):
            if r.get(k):
                extra.append(f"{k} {r[k]}")
        if r.get("found"):
            extra.append(f"found {len(r['found'])}")
        print(f" {n:>2}  {r['id']:<26} {r['kind']:<13} {paint('dim', r['summary'])}")
        if extra:
            print(" " * 31 + paint("dim", " · ".join(extra)))
    cols = [(n, r) for n, r in enumerate(runs, 1) if r.get("result")]
    targets: list[str] = []
    for _, r in cols:
        for res in r["result"]:
            if res["target"] not in targets:
                targets.append(res["target"])
    if targets:
        names = {t: target_name(t, shas, bugs) for t in targets}
        w = max(len(v) for v in names.values())
        print()
        print(paint("head", " " * w + "".join(f"{n:>6}" for n, _ in cols)))
        for t in sorted(targets, key=lambda t: names[t]):
            cells = ""
            for _, r in cols:
                res = next((x for x in r["result"] if x["target"] == t), None)
                if res is None:
                    cells += paint("dim", f"{'·':>6}")
                    continue
                style, label = VERDICT_STYLE.get(res.get("verdict"), ("dim", "?"))
                if res.get("blind_credit"):
                    label += "*"
                cells += paint(style, f"{label:>6}")
            print(f"{names[t]:<{w}}{cells}")
        print(paint("dim", "* blind credit   f+ false positive   · not a target of that run"))
    regs = regressions(runs)
    if regs:
        print()
        print(paint("head", "regressions"))
        for t, good, later in regs:
            print(f"  {target_name(t, shas, bugs)}: {paint('fixed-upstream', good)}, then "
                  f"{paint('confirmed', later)}")
    return 0


def git_tracked() -> set[str] | None:
    try:
        out = subprocess.run(["git", "-C", str(ROOT), "ls-files"], capture_output=True,
                             text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return set(out.splitlines())


def is_tracked(p: str, tracked: set[str]) -> bool:
    p = p.rstrip("/")
    return p in tracked or any(t.startswith(p + "/") for t in tracked)


# ------------------------------------------------------------ terminal ----

def lint_pins(bugs: dict[str, str] | None) -> list[str]:
    """PIN_BUG, when a pin has one, names a bug in c4ffein/bugs."""
    errors: list[str] = []
    for p in sorted(ROOT.glob(PINS_GLOB)):
        ref = pin_vars(str(p.relative_to(ROOT))).get("PIN_BUG")
        if ref:
            rel = p.relative_to(ROOT)
            check_bug(lambda m, rel=rel: errors.append(f"{rel}: {m}"), ref, "PIN_BUG", bugs)
    return errors


def cmd_lint(args) -> int:
    tracked = git_tracked()
    bugs = known_bugs()
    try:
        runs = load_runs()
    except tomllib.TOMLDecodeError as e:
        print(f"docs/runs.toml: {e}", file=sys.stderr)
        return 1
    errors = lint_runs(runs, bugs, tracked) + lint_corpus(bugs, tracked) + lint_pins(bugs)
    for e in errors:
        print(e)
    where = (f"uuids checked against {BUGS_REPO}" if bugs is not None
             else f"uuids checked for form only (no c4ffein/bugs at {BUGS_REPO})")
    if errors:
        print(f"{len(errors)} problem(s)")
        return 1
    print(f"runs.toml: {len(runs)} runs, ok; corpus.toml: {len(load_corpus())} targets, ok; "
          f"pins ok; {where}")
    return 0


def use_color() -> bool:
    if os.environ.get("NO_COLOR"):
        return False
    return bool(os.environ.get("FORCE_COLOR")) or sys.stdout.isatty()


ANSI = {
    "suspected": "38;5;179", "confirmed": "38;5;203", "fixed-upstream": "38;5;114",
    "dim": "2", "bold": "1", "head": "1;4",
}


class Paint:
    def __init__(self, on: bool):
        self.on = on

    def __call__(self, style: str, s: str) -> str:
        return f"\033[{ANSI[style]}m{s}\033[0m" if self.on and style in ANSI else s



def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("runs", help="docs/runs.toml: each run's verdict per target")
    p.add_argument("--project", choices=sorted(PROJECTS))
    sub.add_parser("lint", help="check docs/runs.toml, docs/corpus.toml and the pins' PIN_BUG")
    args = ap.parse_args()
    return {"runs": cmd_runs, "lint": cmd_lint}[args.cmd](args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        sys.exit(0)
