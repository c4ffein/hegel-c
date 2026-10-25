#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
"""View and check docs/findings.toml: the bugs we found, and what we sent upstream.

Also docs/runs.toml: each run that pointed hegel-c at bugs, with a verdict per
target, so a newer hegel-c / skill / model can be checked against an older one.

Submissions keep their patches and notes in git, one folder per version:
submissions/<id>/v<N>[-<label>]/.  lint checks each folder against the ledger
(patch count, [PATCH vN i/n] numbering, cover letter, Signed-off-by on sent
Linux versions) and that no folder is left out of it.

  tools/findings.py [list] [--project P] [--status S] [--open]
  tools/findings.py show ID              (a finding or a submission)
  tools/findings.py html [-o build/findings.html]   (UI: tools/findings_ui.html)
  tools/findings.py runs [--project P]   (targets x runs, regressions flagged)
  tools/findings.py lint
  tools/findings.py serve [--host 0.0.0.0] [--port 8080]

Python >= 3.11, standard library only.  Colors follow the terminal: off when
piped or with NO_COLOR, forced with FORCE_COLOR.
"""
from __future__ import annotations

import argparse
import difflib
import email
import email.policy
import json
import os
import re
import shutil
import subprocess
import sys
import textwrap
from datetime import date
from pathlib import Path

if sys.version_info < (3, 11):
    sys.exit("findings.py needs Python 3.11+ (tomllib)")
import tomllib

ROOT = Path(__file__).resolve().parent.parent
LEDGER = ROOT / "docs" / "findings.toml"
RUNS = ROOT / "docs" / "runs.toml"
CORPUS = ROOT / "docs" / "our_corpus.json"

# ---- findings
STATUSES = ["suspected", "confirmed", "patch-ready", "reported", "fixed-upstream", "withdrawn"]
CLOSED = {"fixed-upstream", "withdrawn"}
KINDS = {"wrong-result", "oob", "overflow", "null-deref", "hang", "leak", "uaf", "crash",
         "contract", "api"}
LEVELS = {"high", "medium", "low"}
REACH = {"live", "latent", "unknown"}
SOURCES = {"repo", "transcript", "memory"}
FOUND_BY = {"hegel-c", "by-hand", "sashiko", "upstream"}
RELS = {"introduced-by", "masked-by", "same-root-cause", "covered-by"}
PROJECTS = {"linux", "scotch"}
REQUIRED = ["id", "project", "where", "title", "kind", "found", "how", "confidence", "reach",
            "status", "since", "source"]
OPTIONAL = ["found_by", "related", "fixed_by", "introduced", "lore", "pin", "writeup", "history",
            "checked", "notes"]
# Where each linked file may live: one home per kind, so nothing ends up stray.
FINDINGS_DIR = "docs/findings"
HOMES = {"pin": ("tests/irl/",),
         "writeup": (FINDINGS_DIR + "/", "submissions/", "tests/irl/"),
         "history": ("docs/saved_sessions_for_history/",),
         "dir": ("submissions/",)}
FILE_KEYS = ("pin", "writeup", "history")

# ---- submissions
SUB_STATES = ["draft", "sent", "changes-requested", "applied", "merged", "abandoned", "superseded"]
SUB_CLOSED = {"merged", "abandoned", "superseded"}
SUB_KINDS = {"patch", "report"}
SUB_REQUIRED = ["id", "project", "title", "kind", "fixes", "state", "since", "versions"]
SUB_OPTIONAL = ["commit", "dir", "next", "notes"]
SUB_NEEDS_NEXT = {"draft", "changes-requested"}     # waiting on us: say what to do
VERSION_KEYS = {"v", "label", "sent", "built", "patches", "lore", "to"}
SUBMISSIONS_DIR = "submissions"
LABEL_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
PATCH_TAG_RE = re.compile(r"\[(?:RFC )?PATCH(?: v(\d+))?(?: (\d+)/(\d+))?\]")

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
NOT_IN_GIT = " (not in git)"

COMMIT_URL = {
    "linux": "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id={}",
    "scotch": "https://gitlab.inria.fr/scotch/scotch/-/commit/{}",
}
LORE_URL = "https://lore.kernel.org/r/{}"


def load() -> dict:
    with open(LEDGER, "rb") as f:
        d = tomllib.load(f)
    led = {"findings": d.get("finding", []), "submissions": d.get("submission", [])}
    led["runs"] = []
    if RUNS.exists():
        with open(RUNS, "rb") as f:
            led["runs"] = tomllib.load(f).get("run", [])
    for f in led["findings"]:
        f.setdefault("found_by", "hegel-c")
    return led


def sort_key(f: dict):
    return (f.get("project", ""), STATUSES.index(f["status"]) if f.get("status") in STATUSES
            else 99, f.get("found", ""), f.get("id", ""))


def sub_key(s: dict):
    return (SUB_STATES.index(s["state"]) if s.get("state") in SUB_STATES else 99,
            s.get("project", ""), s.get("id", ""))


def path_of(ref: str) -> tuple[str, bool]:
    """A pin/writeup/dir value -> (path, marked as untracked)."""
    if ref.endswith(NOT_IN_GIT):
        return ref[: -len(NOT_IN_GIT)], True
    return ref, False


def submissions_of(led: dict, fid: str) -> list[dict]:
    return [s for s in led["submissions"] if fid in s.get("fixes", [])]


def versions_line(s: dict) -> str:
    parts = []
    for v in s.get("versions", []):
        when = f"sent {v['sent']}" if v.get("sent") else f"built {v.get('built', '?')}, unsent"
        parts.append(f"{version_key(v)} {when}")
    return " · ".join(parts)


def version_key(v: dict) -> str:
    """The folder name of a version: v3, or v3-agent when it has a label."""
    return f"v{v.get('v', '?')}" + (f"-{v['label']}" if v.get("label") else "")


def version_dir(s: dict, v: dict) -> Path | None:
    return ROOT / s["dir"] / version_key(v) if s.get("dir") else None


def read_patch(path: Path) -> dict:
    """One git format-patch mail -> its headers, message, diffstat and diff."""
    text = path.read_text(encoding="utf-8", errors="replace")
    if text.startswith("From ") and "\n" in text:
        text = text.split("\n", 1)[1]                     # the mbox "From <sha> <date>" line
    msg = email.message_from_string(text, policy=email.policy.default)
    subject = " ".join(str(msg.get("Subject", "")).split())
    m = PATCH_TAG_RE.search(subject)
    body = msg.get_payload(decode=True)
    body = body.decode("utf-8", "replace") if isinstance(body, bytes) else str(msg.get_payload())
    message, _, rest = body.partition("\n---\n")
    stat, sep, diff = rest.partition("\ndiff --git ")
    if not sep and rest.startswith("diff --git "):
        stat, diff = "", rest[len("diff --git "):]
        sep = True
    return {"file": path.name, "subject": subject,
            "title": PATCH_TAG_RE.sub("", subject).strip(),
            "v": int(m.group(1)) if m and m.group(1) else (1 if m else None),
            "i": int(m.group(2)) if m and m.group(2) else (None if not m else 1),
            "n": int(m.group(3)) if m and m.group(3) else (None if not m else 1),
            "from": str(msg.get("From", "")), "date": str(msg.get("Date", "")),
            "message": message.strip("\n"), "stat": stat.strip("\n"),
            "diff": ("diff --git " + diff) if sep else "",
            "sob": [l.split(":", 1)[1].strip() for l in message.splitlines()
                    if l.startswith("Signed-off-by:")]}


def read_version(folder: Path) -> dict:
    """The patches (cover letter first) and the other files of one version folder."""
    patches, other = [], []
    for p in sorted(folder.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(ROOT).as_posix()
        if p.suffix == ".patch" and p.parent == folder:
            patches.append(read_patch(p) | {"path": rel})
        else:
            other.append(rel)
    patches.sort(key=lambda x: (x["i"] if x["i"] is not None else 99, x["file"]))
    return {"patches": patches, "other": other}


def check_version(bad, s: dict, v: dict, folder: Path) -> None:
    key = version_key(v)
    if not folder.is_dir():
        if v.get("sent"):          # an unsent draft may stay out of git until it goes out
            bad(f"{key}: folder {folder.relative_to(ROOT).as_posix()} does not exist")
        return
    ver = read_version(folder)
    pats = ver["patches"]
    if s.get("kind") != "patch":
        return
    if not pats:
        bad(f"{key}: no .patch files in {folder.relative_to(ROOT).as_posix()}")
        return
    cover = [p for p in pats if p["i"] == 0]
    real = [p for p in pats if p["i"] != 0]
    if v.get("patches") and len(real) != v["patches"]:
        bad(f"{key}: patches = {v['patches']}, but the folder has {len(real)} (cover letter not counted)")
    for p in pats:
        if p["v"] is None:
            bad(f"{key}: {p['file']}: subject has no [PATCH] tag")
        elif p["v"] != v["v"]:
            bad(f"{key}: {p['file']}: subject says v{p['v']}")
    n = len(real)
    if n > 1:
        if sorted(p["i"] or 0 for p in real) != list(range(1, n + 1)):
            bad(f"{key}: patches are not numbered 1/{n} .. {n}/{n}")
        if any(p["n"] != n for p in pats if p["n"] is not None):
            bad(f"{key}: subjects disagree on the series length (expected /{n})")
        if s.get("project") == "linux" and len(cover) != 1:
            bad(f"{key}: a {n}-patch Linux series needs one cover letter (0/{n})")
    if s.get("project") == "linux" and v.get("sent"):
        for p in real:
            if not p["sob"]:
                bad(f"{key}: {p['file']} was sent but has no Signed-off-by")


def check_folders(errors: list[str], led: dict) -> None:
    """Every folder under submissions/ belongs to a submission, every subfolder to a version."""
    base = ROOT / SUBMISSIONS_DIR
    if not base.is_dir():
        return
    owners = {path_of(s["dir"])[0].rstrip("/"): s for s in led["submissions"] if s.get("dir")}
    for d in sorted(base.iterdir()):
        if not d.is_dir():
            continue
        rel = d.relative_to(ROOT).as_posix()
        s = owners.get(rel)
        if not s:
            errors.append(f"{rel}: no submission has dir = \"{rel}\"")
            continue
        keys = {version_key(v) for v in s.get("versions", []) if isinstance(v, dict)}
        for sub in sorted(d.iterdir()):
            if sub.is_dir() and sub.name not in keys:
                errors.append(f"{s['id']}: folder {sub.name}/ is not one of its versions "
                              f"({', '.join(sorted(keys))})")


# ---------------------------------------------------------------- lint ----

def lint(led: dict) -> list[str]:
    errors: list[str] = []
    tracked = git_tracked()
    ids = {f.get("id") for f in led["findings"]}
    seen: set[str] = set()

    def check_path(bad, k: str, ref: str) -> None:
        p, untracked = path_of(ref)
        if not p.startswith(HOMES[k]):
            bad(f"{k} {p} must be under {' or '.join(HOMES[k])}")
        if untracked:
            return                              # lives outside git on purpose
        if not (ROOT / p).exists():
            bad(f"{k} {p} does not exist")
        elif tracked is not None and not is_tracked(p, tracked):
            bad(f"{k} {p} is not in git: track it or mark it '{NOT_IN_GIT.strip()}'")

    def check_id(bad, obj: dict) -> None:
        if not obj.get("id"):
            return
        if not ID_RE.match(obj["id"]):
            bad("id must be kebab-case with a project prefix")
        elif not obj["id"].startswith(obj.get("project", "") + "-"):
            bad("id must start with the project name")
        if obj["id"] in seen:
            bad("duplicate id (findings and submissions share one namespace)")
        seen.add(obj["id"])

    for n, f in enumerate(led["findings"], 1):
        who = f.get("id") or f"finding #{n}"

        def bad(msg: str, who=who) -> None:
            errors.append(f"{who}: {msg}")

        for k in REQUIRED:
            if not f.get(k):
                bad(f"missing '{k}'")
        for k in f:
            if k not in REQUIRED and k not in OPTIONAL:
                bad(f"unknown field '{k}'")
        check_id(bad, f)
        for k, allowed in (("project", PROJECTS), ("kind", KINDS), ("confidence", LEVELS),
                           ("reach", REACH), ("status", set(STATUSES)), ("source", SOURCES),
                           ("found_by", FOUND_BY)):
            if f.get(k) and f[k] not in allowed:
                bad(f"{k} '{f[k]}' not in {sorted(allowed)}")
        for k in ("found", "since"):
            if f.get(k) and not DATE_RE.match(str(f[k])):
                bad(f"{k} must be YYYY-MM-DD or YYYY-MM")
        if f.get("found") and f.get("since") and str(f["since"]) < str(f["found"])[:7]:
            bad("since is before found")
        for k in ("fixed_by", "introduced"):
            if f.get(k) and not SHA_RE.match(f[k]):
                bad(f"{k} must be a commit sha")
        subs = submissions_of(led, f.get("id"))
        if f.get("status") == "fixed-upstream" and not f.get("fixed_by") and not any(
                s.get("state") == "merged" for s in subs):
            bad("fixed-upstream needs fixed_by or a merged submission")
        if f.get("status") == "reported" and not subs and not f.get("lore"):
            bad("reported needs a submission (or a lore Message-ID)")
        states = {s.get("state") for s in subs}
        if "merged" in states and f.get("status") != "fixed-upstream":
            bad("a submission fixing it is merged: status should be fixed-upstream")
        if states == {"draft"} and f.get("status") != "patch-ready":
            bad("only a draft submission fixes it: status should be patch-ready")
        lore = f.get("lore", [])
        if not isinstance(lore, list) or any("@" not in m or "<" in m for m in lore):
            bad("lore must be a list of bare Message-IDs (no <>)")
        for r in f.get("related", []):
            if not isinstance(r, dict) or set(r) != {"to", "rel"}:
                bad("related entries are { to = \"<id>\", rel = \"...\" }")
            elif r["rel"] not in RELS:
                bad(f"related rel '{r['rel']}' not in {sorted(RELS)}")
            elif r["to"] not in ids or r["to"] == f.get("id"):
                bad(f"related to '{r['to']}' is not another finding")
        for k in FILE_KEYS:
            if f.get(k):
                check_path(bad, k, f[k])
        w = path_of(f.get("writeup", ""))[0]
        if w.startswith(FINDINGS_DIR + "/") and w != f"{FINDINGS_DIR}/{f.get('id')}.md":
            bad(f"a write-up in {FINDINGS_DIR}/ is named after its finding: {FINDINGS_DIR}/{f.get('id')}.md")

    # Every write-up in docs/findings/ belongs to a finding that links it.
    linked = {path_of(f["writeup"])[0] for f in led["findings"] if f.get("writeup")}
    for md in sorted((ROOT / FINDINGS_DIR).glob("*")):
        rel = f"{FINDINGS_DIR}/{md.name}"
        if rel not in linked:
            errors.append(f"{rel}: no finding links it (writeup = \"{rel}\")")

    for n, s in enumerate(led["submissions"], 1):
        who = s.get("id") or f"submission #{n}"

        def bad(msg: str, who=who) -> None:
            errors.append(f"{who}: {msg}")

        for k in SUB_REQUIRED:
            if not s.get(k):
                bad(f"missing '{k}'")
        for k in s:
            if k not in SUB_REQUIRED and k not in SUB_OPTIONAL:
                bad(f"unknown field '{k}'")
        check_id(bad, s)
        for k, allowed in (("project", PROJECTS), ("kind", SUB_KINDS), ("state", set(SUB_STATES))):
            if s.get(k) and s[k] not in allowed:
                bad(f"{k} '{s[k]}' not in {sorted(allowed)}")
        if s.get("since") and not DATE_RE.match(str(s["since"])):
            bad("since must be YYYY-MM-DD or YYYY-MM")
        for fid in s.get("fixes", []):
            if fid not in ids:
                bad(f"fixes unknown finding '{fid}'")
        if s.get("state") == "merged" and not s.get("commit"):
            bad("merged needs commit")
        if s.get("commit") and not SHA_RE.match(s["commit"]):
            bad("commit must be a sha")
        vs = s.get("versions", [])
        keys_seen: set[str] = set()
        for v in vs:
            if not isinstance(v, dict) or not set(v) <= VERSION_KEYS or "v" not in v:
                bad(f"versions entries take {sorted(VERSION_KEYS)} and need v")
                continue
            if v.get("label") and not LABEL_RE.match(str(v["label"])):
                bad(f"v{v['v']}: label must be lowercase kebab-case")
            if version_key(v) in keys_seen:
                bad(f"{version_key(v)} appears twice: give same-number versions distinct labels")
            keys_seen.add(version_key(v))
            for k in ("sent", "built"):
                if v.get(k) and not DATE_RE.match(str(v[k])):
                    bad(f"v{v['v']}: {k} must be YYYY-MM-DD or YYYY-MM")
            if not v.get("sent") and not v.get("built"):
                bad(f"v{v['v']}: needs sent or built")
            if v.get("lore") and ("@" not in v["lore"] or "<" in v["lore"]):
                bad(f"v{v['v']}: lore must be a bare Message-ID")
        any_sent = any(v.get("sent") for v in vs if isinstance(v, dict))
        if s.get("state") == "draft" and any_sent:
            bad("state draft, but a version is marked sent")
        if s.get("state") not in (None, "draft", "abandoned") and not any_sent:
            bad(f"state {s.get('state')} needs a version with a sent date")
        if s.get("state") in SUB_NEEDS_NEXT and not s.get("next"):
            bad(f"state {s['state']} is waiting on us: say what to do in 'next'")
        if s.get("state") in SUB_CLOSED and s.get("next"):
            bad(f"state {s['state']} is closed: drop 'next'")
        if s.get("dir"):
            p, untracked = path_of(s["dir"])
            check_path(bad, "dir", s["dir"])
            if not untracked and p.rstrip("/") != f"{SUBMISSIONS_DIR}/{s.get('id')}":
                bad(f"dir should be {SUBMISSIONS_DIR}/{s.get('id')}")
            if not untracked and (ROOT / p).is_dir():
                for v in vs:
                    if isinstance(v, dict) and "v" in v:
                        check_version(bad, s, v, ROOT / p / version_key(v))
    check_folders(errors, led)
    errors += lint_runs(led, tracked)
    return errors


def corpus_shas() -> dict[str, dict]:
    """docs/our_corpus.json entries by 12-hex Fixed-by."""
    try:
        entries = json.loads(CORPUS.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return {e["Fixed-by"][:12]: e for e in entries if e.get("Fixed-by")}


def run_verdicts(kind: str) -> list[str]:
    return PIN_VERDICTS if kind == "pins" else HUNT_VERDICTS


def lint_runs(led: dict, tracked: set[str] | None) -> list[str]:
    errors: list[str] = []
    fids = {f.get("id") for f in led["findings"]}
    shas = corpus_shas()
    rids = [r.get("id") for r in led["runs"]]
    seen: set[str] = set()
    for i, r in enumerate(led["runs"]):
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
        for fid in r.get("found", []):
            if fid not in fids:
                bad(f"found {fid} is not a finding in docs/findings.toml")
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
                    bad(f"result {t}: no Fixed-by {t} in docs/our_corpus.json")
            elif t not in fids:
                bad(f"result {t}: not a corpus SHA, a finding id or a pin file")
            if kind and res.get("verdict") not in run_verdicts(kind):
                bad(f"result {t}: verdict must be {' | '.join(run_verdicts(kind))}")
            if "blind_credit" in res:
                if kind != "blind-recall":
                    bad(f"result {t}: blind_credit only applies to blind-recall runs")
                elif not isinstance(res["blind_credit"], bool):
                    bad(f"result {t}: blind_credit must be true or false")
                elif res["blind_credit"] and res.get("verdict") != "hit":
                    bad(f"result {t}: blind credit needs a hit")
    linked = {r["dir"].rstrip("/").split("/")[-1] for r in led["runs"] if r.get("dir")}
    if (ROOT / RUNS_DIR).is_dir():
        for p in sorted((ROOT / RUNS_DIR).iterdir()):
            if p.name not in linked:
                errors.append(f"runs.toml: {RUNS_DIR}/{p.name} belongs to no run: add its run "
                              f"or move it")
    return errors


def target_name(t: str, shas: dict[str, dict]) -> str:
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


def cmd_runs(led: dict, args) -> int:
    paint = Paint(use_color())
    shas = corpus_shas()
    runs = sorted((r for r in led["runs"] if not args.project or r.get("project") == args.project),
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
        names = {t: target_name(t, shas) for t in targets}
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
            print(f"  {target_name(t, shas)}: {paint('fixed-upstream', good)}, then "
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

def use_color() -> bool:
    if os.environ.get("NO_COLOR"):
        return False
    return bool(os.environ.get("FORCE_COLOR")) or sys.stdout.isatty()


ANSI = {
    "suspected": "38;5;179", "confirmed": "38;5;203", "patch-ready": "38;5;208",
    "reported": "38;5;75", "fixed-upstream": "38;5;114", "withdrawn": "38;5;244",
    "draft": "38;5;208", "sent": "38;5;75", "changes-requested": "38;5;203",
    "applied": "38;5;114", "merged": "38;5;114", "abandoned": "38;5;244", "superseded": "38;5;244",
    "dim": "2", "bold": "1", "head": "1;4",
}


class Paint:
    def __init__(self, on: bool):
        self.on = on

    def __call__(self, style: str, s: str) -> str:
        return f"\033[{ANSI[style]}m{s}\033[0m" if self.on and style in ANSI else s


def short(s: str, n: int) -> str:
    return s if len(s) <= n else s[: max(n - 1, 0)] + "…"


def counts(items: list[dict], key: str, order: list[str], paint: Paint) -> str:
    parts = []
    for st in order:
        c = sum(1 for x in items if x.get(key) == st)
        if c:
            parts.append(paint(st, f"{c} {st}"))
    return "  ".join(parts)


def cmd_list(led: dict, args) -> int:
    paint = Paint(use_color())
    fs = led["findings"]
    sel = [f for f in fs
           if (not args.project or f["project"] == args.project)
           and (not args.status or f["status"] == args.status)
           and (not args.open or f["status"] not in CLOSED)]
    width = shutil.get_terminal_size((110, 24)).columns
    idw = max([len(f["id"]) for f in sel] + [len(s["id"]) for s in led["submissions"]] + [2])
    stw = 18
    print(paint("bold", f"hegel-c findings — {len(sel)} of {len(fs)}"))
    print(counts(sel, "status", STATUSES, paint))
    for proj in sorted({f["project"] for f in sel}):
        print()
        print(paint("head", proj))
        for f in sorted((f for f in sel if f["project"] == proj), key=sort_key):
            rest = width - 2 - stw - idw - 2
            tags = "" if f["found_by"] == "hegel-c" else f"[{f['found_by']}] "
            title = short(tags + f["title"], rest) if rest > 20 else ""
            mark = paint("dim", "·") if f["source"] != "repo" else " "
            st = f"{f['status']:<{stw}}"
            print(f" {mark}{paint(f['status'], st)}{f['id']:<{idw}}  {paint('dim', title)}")
    subs = [s for s in led["submissions"]
            if (not args.project or s["project"] == args.project)
            and (not args.open or s["state"] not in SUB_CLOSED)]
    if subs and not args.status:
        print()
        print(paint("head", "submissions") + "  " + counts(subs, "state", SUB_STATES, paint))
        for s in sorted(subs, key=sub_key):
            rest = width - 2 - stw - idw - 2
            info = f"{len(s['fixes'])} bug{'s' * (len(s['fixes']) != 1)} · {versions_line(s)}"
            st = f"{s['state']:<{stw}}"
            info = short(info, rest) if rest > 20 else ""
            print(f"  {paint(s['state'], st)}{s['id']:<{idw}}  {paint('dim', info)}")
            if s.get("next") and s["state"] not in SUB_CLOSED:
                print(" " * (4 + stw) + paint("bold", "next: ") + short(s["next"], max(width - 10 - stw, 20)))
    notes = []
    if any(f["source"] != "repo" for f in sel):
        notes.append("· facts from a saved session or Claude's memory — unverified")
    if any(f["found_by"] != "hegel-c" for f in sel):
        notes.append("[...] found by someone else; a submission of ours fixes it")
    if notes:
        print()
        for n in notes:
            print(paint("dim", " " + n))
    return 0


def print_rows(rows: list[tuple[str, str]], width: int, paint: Paint) -> None:
    for k, v in rows:
        wrapped = textwrap.wrap(v, width - 12, break_long_words=False,
                                break_on_hyphens=False) or [""]
        print(paint("dim", f"{k:>10}  ") + wrapped[0])
        for w in wrapped[1:]:
            print(" " * 12 + w)


def cmd_show(led: dict, args) -> int:
    paint = Paint(use_color())
    every = led["findings"] + led["submissions"]
    match = [x for x in every if x["id"] == args.id] or [x for x in every if args.id in x["id"]]
    if len(match) != 1:
        ids = ", ".join(x["id"] for x in match) or "none"
        print(f"nothing single matches '{args.id}' (matches: {ids})", file=sys.stderr)
        return 1
    x = match[0]
    width = min(shutil.get_terminal_size((100, 24)).columns, 100)
    is_sub = "state" in x
    st = x["state"] if is_sub else x["status"]
    label = f"  {x['kind']} submission" if is_sub else ""
    print(paint("bold", x["id"]) + "  " + paint(st, st) + paint("dim", f"  since {x['since']}{label}"))
    for line in textwrap.wrap(x["title"], width):
        print(line)
    print()
    cu = COMMIT_URL[x["project"]]
    if is_sub:
        rows = [("next", x["next"])] if x.get("next") else []
        rows.append(("fixes", ", ".join(x["fixes"])))
        for v in x["versions"]:
            when = f"sent {v['sent']}" if v.get("sent") else f"built {v['built']}, not sent"
            extra = "".join(f", {k} {v[k]}" for k in ("patches", "to") if v.get(k))
            folder = version_dir(x, v)
            if folder and folder.is_dir():
                ver = read_version(folder)
                real = [p for p in ver["patches"] if p["i"] != 0]
                unsigned = sum(1 for p in real if not p["sob"])
                extra += f" — {folder.relative_to(ROOT).as_posix()}/"
                if x["project"] == "linux" and unsigned:
                    extra += f" ({unsigned} without Signed-off-by)"
            rows.append((version_key(v), when + extra))
            if v.get("lore"):
                rows.append(("", LORE_URL.format(v["lore"])))
        if x.get("commit"):
            rows.append(("merged as", cu.format(x["commit"])))
        if x.get("dir"):
            rows.append(("dir", x["dir"]))
    else:
        rows = [("where", x["where"]), ("kind", x["kind"]),
                ("real?", f"{x['confidence']} confidence, {x['reach']} reach"),
                ("found", f"{x['found']} by {x['found_by']} — {x['how']}"), ("source", x["source"])]
        for r in x.get("related", []):
            rows.append(("related", f"{r['rel']} {r['to']}"))
        for s in submissions_of(led, x["id"]):
            rows.append(("submitted", f"{s['id']} ({s['state']})"))
        for k in ("introduced", "fixed_by"):
            if x.get(k):
                rows.append((k.replace("_", " "), cu.format(x[k])))
        for m in x.get("lore", []):
            rows.append(("lore", LORE_URL.format(m)))
        for k in FILE_KEYS + ("checked",):
            if x.get(k):
                rows.append((k, x[k]))
    print_rows(rows, width, paint)
    if x.get("notes"):
        print()
        for line in textwrap.wrap(x["notes"], width):
            print(line)
    return 0


# ---------------------------------------------------------------- html ----

UI_TEMPLATE = Path(__file__).resolve().parent / "findings_ui.html"
INLINE_MAX = 64 * 1024          # bigger files stay links
MD_SUFFIXES = {".md", ".markdown"}


def file_entry(p: str) -> dict:
    """What the page needs to show one linked pin / write-up / folder."""
    path = ROOT / p
    if path.is_dir():
        return {"kind": "dir", "entries": sorted(e.name + ("/" if e.is_dir() else "")
                                                 for e in path.iterdir())}
    if not path.is_file():
        return {"kind": "missing"}
    size = path.stat().st_size
    if size > INLINE_MAX:
        return {"kind": "big", "size": size}
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return {"kind": "big", "size": size}
    return {"kind": "md" if path.suffix in MD_SUFFIXES else "text", "size": size, "content": text}


def patch_body(p: dict) -> list[str]:
    """A patch as compared across versions: message and diff, without blob hashes."""
    lines = [p["subject"], ""] + p["message"].splitlines() + ["---"]
    return lines + [l for l in p["diff"].splitlines() if not l.startswith("index ")]


def compare(a: dict, b: dict) -> dict:
    """What changed from version a to version b, patch by patch (patches paired by title)."""
    pa = [p for p in a["patches"] if p["i"] != 0]
    pb = [p for p in b["patches"] if p["i"] != 0]
    ca = [p for p in a["patches"] if p["i"] == 0]
    cb = [p for p in b["patches"] if p["i"] == 0]
    pairs, used = [], set()
    for y in pb:
        best, score = None, 0.0
        for k, x in enumerate(pa):
            if k in used:
                continue
            r = difflib.SequenceMatcher(None, x["title"], y["title"]).ratio()
            if r > score:
                best, score = k, r
        if best is not None and score >= 0.6:
            used.add(best)
            pairs.append((pa[best], y))
        else:
            pairs.append((None, y))
    gone = [x for k, x in enumerate(pa) if k not in used]
    if ca and cb:
        pairs.insert(0, (ca[0], cb[0]))
    out = []
    for x, y in pairs:
        if x is None:
            out.append({"a": None, "b": y["subject"], "diff": None})
            continue
        d = list(difflib.unified_diff(patch_body(x), patch_body(y), n=3, lineterm=""))[2:]
        out.append({"a": x["subject"], "b": y["subject"], "diff": "\n".join(d)})
    out += [{"a": x["subject"], "b": None, "diff": None} for x in gone]
    return {"from": a["key"], "to": b["key"], "pairs": out}


def submission_files(s: dict) -> dict | None:
    """Everything the page shows for one submission's folder: versions, patches, comparisons."""
    if not s.get("dir") or path_of(s["dir"])[1] or not (ROOT / s["dir"]).is_dir():
        return None
    vers = []
    for v in (v for v in s.get("versions", []) if isinstance(v, dict) and "v" in v):   # ledger order
        folder = ROOT / s["dir"] / version_key(v)
        if not folder.is_dir():
            continue
        ver = read_version(folder)
        top = [o for o in ver["other"] if "/" not in o[len(folder.relative_to(ROOT).as_posix()) + 1:]]
        notes = {}
        for o in top:
            if Path(o).suffix in MD_SUFFIXES | {".txt"}:
                notes[o] = file_entry(o)
        vers.append({"key": version_key(v), "v": v["v"], "folder": folder.relative_to(ROOT).as_posix(),
                     "patches": ver["patches"], "other": ver["other"], "notes": notes})
    cmps = []
    for k, b in enumerate(vers):
        older = [a for a in vers if a["v"] < b["v"]]
        base = [a for a in older if a["v"] == max(x["v"] for x in older)] if older else []
        for a in base + [a for a in vers[:k] if a["v"] == b["v"]]:     # previous number, then siblings
            if a["patches"] and b["patches"]:
                cmps.append(compare(a, b))
    return {"versions": vers, "compare": cmps}


def build_page(led: dict) -> tuple[str, dict]:
    """The whole page, and the linked files it knows about (path -> entry)."""
    files = {}
    refs = [f[k] for f in led["findings"] for k in FILE_KEYS if f.get(k)]
    refs += [s["dir"] for s in led["submissions"] if s.get("dir")]
    for ref in refs:
        p, _ = path_of(ref)
        files.setdefault(p, file_entry(p))
    sub_files = {s["id"]: sf for s in led["submissions"] if (sf := submission_files(s))}
    for sf in sub_files.values():                   # the server may send any file in a version folder
        for v in sf["versions"]:
            for p in v["other"] + [x["path"] for x in v["patches"]]:
                files.setdefault(p, {"kind": "file"})
    data = {"generated": date.today().isoformat(), "statuses": STATUSES, "closed": sorted(CLOSED),
            "sub_states": SUB_STATES, "sub_closed": sorted(SUB_CLOSED),
            "commit_url": COMMIT_URL, "lore_url": LORE_URL,
            "findings": sorted(led["findings"], key=sort_key),
            "submissions": sorted(led["submissions"], key=sub_key), "files": files,
            "sub_files": sub_files}
    blob = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
    blob = blob.replace("<", "\\u003c")          # no "</script>" or "<!--" can end the block
    page = UI_TEMPLATE.read_text(encoding="utf-8").replace("/*__DATA__*/", blob, 1)
    return page, files


def cmd_html(led: dict, args) -> int:
    out = Path(args.output)
    if not out.is_absolute():
        out = ROOT / out
    page, files = build_page(led)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")
    inlined = sum(1 for v in files.values() if "content" in v)
    print(f"wrote {os.path.relpath(out, Path.cwd())}: {len(led['findings'])} findings, "
          f"{len(led['submissions'])} submissions, {inlined} of {len(files)} linked files "
          f"inlined, {out.stat().st_size // 1024} KB")
    return 0


def allowed_files(files: dict) -> set[str]:
    """Repo paths the server may send: linked files, and the files directly in linked folders."""
    ok = set()
    for p, entry in files.items():
        if entry["kind"] == "dir":
            ok.update(p.rstrip("/") + "/" + e for e in entry["entries"] if not e.endswith("/"))
        elif entry["kind"] != "missing":
            ok.add(p)              # "file": a file inside a submission's version folder
    return ok


def cmd_serve(led: dict, args) -> int:
    """Serve the page (rebuilt on every load) and only the files the ledger links to."""
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    from urllib.parse import unquote, urlsplit

    class Handler(BaseHTTPRequestHandler):
        server_version = "hegel-c-findings"

        def send(self, code: int, body: bytes, ctype: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def do_GET(self) -> None:
            path = unquote(urlsplit(self.path).path)
            try:
                page, files = build_page(load())        # re-read: edits show on refresh
            except tomllib.TOMLDecodeError as e:
                self.send(500, f"docs/findings.toml: {e}\n".encode(), "text/plain; charset=utf-8")
                return
            if path in ("/", "/findings.html", "/build/findings.html"):
                self.send(200, page.encode(), "text/html; charset=utf-8")
                return
            rel = path.lstrip("/")
            if rel in allowed_files(files) and (ROOT / rel).is_file():
                self.send(200, (ROOT / rel).read_bytes(), "text/plain; charset=utf-8")
                return
            self.send(404, b"not a file the findings ledger links to\n", "text/plain; charset=utf-8")

        do_HEAD = do_GET

        def log_message(self, fmt: str, *a) -> None:
            sys.stderr.write(f"{self.address_string()}  {fmt % a}\n")

    try:
        httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    except OSError as e:
        print(f"cannot listen on {args.host}:{args.port}: {e}", file=sys.stderr)
        return 1
    shown = "localhost" if args.host in ("0.0.0.0", "", "::") else args.host
    print(f"serving the findings page on http://{shown}:{args.port}/  (bound to {args.host or '*'}"
          f":{args.port}; rebuilt on each load; Ctrl-C stops)", flush=True)
    if args.host in ("0.0.0.0", "", "::"):
        print("reachable from every host that can reach this machine — it shows unreported bugs",
              flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        httpd.server_close()
    return 0


def cmd_lint(led: dict, args) -> int:
    errors = lint(led)
    for e in errors:
        print(e if e.startswith("runs.toml:") else f"findings.toml: {e}", file=sys.stderr)
    if errors:
        print(f"{len(errors)} problem(s)", file=sys.stderr)
        return 1
    print(f"findings.toml: {len(led['findings'])} findings, {len(led['submissions'])} "
          f"submissions, ok; runs.toml: {len(led['runs'])} runs, ok")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("list", help="table of findings and submissions (default)")
    for a in (p, ap):
        a.add_argument("--project", choices=sorted(PROJECTS))
        a.add_argument("--status", choices=STATUSES)
        a.add_argument("--open", action="store_true",
                       help="hide fixed-upstream / withdrawn findings and closed submissions")
    p = sub.add_parser("show", help="one finding or submission in full")
    p.add_argument("id", help="id, or a unique part of it")
    p = sub.add_parser("html", help="self-contained HTML page")
    p.add_argument("-o", "--output", default="build/findings.html")
    p = sub.add_parser("runs", help="docs/runs.toml: each run's verdict per target")
    p.add_argument("--project", choices=sorted(PROJECTS))
    sub.add_parser("lint", help="check fields, statuses, links between entries and to files")
    p = sub.add_parser("serve", help="serve the page over HTTP (rebuilt on each load)")
    p.add_argument("--host", default="0.0.0.0", help="address to bind (default: all, 0.0.0.0)")
    p.add_argument("--port", type=int, default=8080)
    args = ap.parse_args()
    try:
        led = load()
    except tomllib.TOMLDecodeError as e:
        print(f"docs/findings.toml or docs/runs.toml: {e}", file=sys.stderr)
        return 1
    if args.cmd in (None, "list"):
        bad = lint(led)
        if bad:
            print(f"(findings.toml has {len(bad)} problem(s): run tools/findings.py lint)\n",
                  file=sys.stderr)
        return cmd_list(led, args)
    return {"show": cmd_show, "html": cmd_html, "lint": cmd_lint, "runs": cmd_runs,
            "serve": cmd_serve}[args.cmd](led, args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        sys.exit(0)
