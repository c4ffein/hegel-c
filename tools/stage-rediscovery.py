#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
"""Stage SCRUBBED, BLIND rediscovery arenas from existing harness dirs.

For each pool target: copy its harness dir to an arena folder, strip the
existing test file(s) and build products, put a plain copy (no .git — the
pin's object store would reveal the fix commit) of the kernel checkout at the
pin's buggy state under tNN/linux, scrub bug-naming comments from
Makefile/shims, and drop a neutral BRIEF.md telling a blind hunter only "test
the code in <file> with hegel-c; report any bug with a minimal reproducer."
NO description, NO fix SHA, NO pin, NO existing test.  The arena gets its own
pin.mk (arena/pin.mk, which the harness Makefiles' `include ../pin.mk`
finds) pointing LINUX at that copy.

Then GATE: grep the arena's meta files (Makefile, shims, BRIEF, filenames —
NOT the kernel source body, which legitimately contains the buggy code) for
leak keywords. A target that still leaks is EXCLUDED from the run, not silently
shipped. The 2026-07-12 re-run showed a single leaked hint invalidates recall.

Usage: tools/stage-rediscovery.py [-o SCRATCH/rediscovery_arena] [--seed N]
Emits arena/tNN/ dirs + arena/manifest.json (arena path + difficulty +
gate result per target). The judge key (descriptions) is NEVER written here.
"""

import argparse, json, re, shutil, subprocess
from pathlib import Path

# Pool = the buildable-and-blindable targets: a pin under
# tests/irl/linux/pins/ (its buggy state is what the hunter gets) and a
# working build env. maple's four bugs collapse to one hunt target (a blind
# agent gets the whole maple_tree.c). difficulty routes model tiering.
POOL = [
  {"id": "packing",   "dir": "packing_roundtrip",      "pin": "packing_roundtrip",
   "natural": "lib/packing.c",            "difficulty": "leaf"},
  {"id": "sg_split",  "dir": "sg_split",               "pin": "sg_split",
   "natural": "lib/sg_split.c",           "difficulty": "leaf"},
  {"id": "idr",       "dir": "idr_rediscovery",        "pin": "idr_rediscovery",
   "natural": "lib/idr.c",                "difficulty": "medium"},
  {"id": "bch",       "dir": "bch_mod8",               "pin": "bch_mod8",
   "natural": "lib/bch.c",                "difficulty": "leaf"},
  {"id": "bunzip2",   "dir": "bunzip2",                "pin": "bunzip2",
   "natural": "lib/decompress_bunzip2.c", "difficulty": "leaf"},
  {"id": "bootconfig","dir": "bootconfig_rediscovery", "pin": "bootconfig_rediscovery",
   "natural": "lib/bootconfig.c",         "difficulty": "medium"},
  {"id": "maple_tree","dir": "maple_tree_rediscovery", "pin": "maple_tree_rediscovery",
   "natural": "lib/maple_tree.c",         "difficulty": "hard"},
  {"id": "xarray",    "dir": "xarray_pause",           "pin": "xarray_pause",
   "natural": "lib/xarray.c",             "difficulty": "hard"},
]

# Leak vocabulary: names the bug's CLASS, its provenance, or its fix. Bare
# "bug"/"fix" are excluded — too generic (they appear in neutral instructions
# like "report any bug"). Match inflected forms too (rediscover -> rediscovery,
# snapshot -> snapshotted): no trailing \b, allow a word-suffix.
LEAK = re.compile(
  r"(buggy|rediscover\w*|\bCVE|memory.leak|overflow|out.of.bounds|"
  r"\boob\b|off.by.one|underflow|use.after.free|\buaf\b|double.free|deadlock|"
  r"regression|counterexample|shrunk|snapshot\w*|clobber\w*|overshoot\w*|"
  r"silent success|exhaustion)|[0-9a-f]{12,40}",
  re.I)

# Sibling files that reveal the answer: fixed versions, version ladders,
# reproducers, core dumps, target pins. Copied dirs must NOT carry these.
LEAK_FILE = re.compile(
  r"(_fixed|_bugc|_v\d|repro|ladder|full_|core\.\d|\.conf$|_current|_alt|einval|_tol$|^campaign\.mk$)",
  re.I)
# Meta files we scrub + gate. The kernel source body is exempt (it IS the target).
META_SUFFIX = {".md", ".txt", "Makefile", "makefile"}
SHIM_HINTS = ("compat.h", "shim")


# Arena run target: build test_hunt, run it in-place with the engine path set.
RUN_TARGET = ("test: test_hunt\n"
              "\tHEGEL_LIBHEGEL_PATH=$(REPO_ROOT)/build/libhegel.so ./test_hunt\n\n")


def is_meta(p: Path):
  return p.name in ("Makefile", "makefile") or p.suffix in (".md", ".txt") \
      or any(h in p.name for h in SHIM_HINTS)


def scrub_comments(text, is_makefile):
  """Drop comment lines that leak. Keep SPDX + code lines."""
  out = []
  for line in text.splitlines():
    s = line.strip()
    is_comment = s.startswith("#") or s.startswith("//") or s.startswith("*") \
        or s.startswith("/*")
    if is_makefile:
      is_comment = s.startswith("#")
    if is_comment and "SPDX" not in line and LEAK.search(line):
      # scrub this leaky comment line, but keep a block comment's opener /
      # closer so the file still parses
      if not is_makefile and "/*" in s and "*/" not in s:
        out.append("/*")
      elif not is_makefile and "*/" in s and "/*" not in s:
        out.append("*/")
      continue
    out.append(line)
  return "\n".join(out) + "\n"


def kernel_checkout(src_root, pin):
  """Path of the pin's buggy-state checkout (fetched + built on demand)."""
  out = subprocess.run([str(src_root / "scripts/pin-worktree.sh"), "linux", pin,
                        "--state", "buggy"], check=True, capture_output=True, text=True)
  return Path(out.stdout.strip().splitlines()[-1])


def write_arena_pin_mk(src_root, arena):
  """arena/pin.mk: the real pin.mk with LINUX pointed at the target's own
  copy (tNN/linux) instead of a pinned worktree."""
  body = (src_root / "tests/irl/linux/pin.mk").read_text()
  body = re.sub(r"^LINUX \?=.*?^endif\n", "LINUX = $(CURDIR)/linux\n", body,
                flags=re.M | re.S)
  body = "\n".join(l for l in body.splitlines()
                   if not (l.startswith("#") and "SPDX" not in l)) + "\n"
  (arena / "pin.mk").write_text(body)


def in_kernel(p, dst):
  return (dst / "linux") in p.parents


def stage(t, src_root, arena, idx):
  src = src_root / "tests/irl/linux" / t["dir"]
  dst = arena / f"t{idx:02d}"
  if dst.exists():
    shutil.rmtree(dst)
  shutil.copytree(src, dst, ignore=shutil.ignore_patterns(
    "test_*", ".hegel", "*.o", "*.output", "REPORT.md", "NOTES*", "README*",
    "*_pinned.c", "pinned-include", "build-pin.log"))
  # remove sibling files that leak the answer (fixed versions, ladders, repros)
  for p in list(dst.rglob("*")):
    if p.is_file() and LEAK_FILE.search(p.name):
      p.unlink()
  # the kernel at the buggy state, without its git metadata
  shutil.copytree(kernel_checkout(src_root, t["pin"]), dst / "linux", symlinks=True,
                  ignore=shutil.ignore_patterns(".git", ".hegel-built-*", "build-*.log"))
  mk = dst / "Makefile"
  if mk.exists():
    body = mk.read_text()
    body = re.sub(r"^PIN\s*=.*\n", "", body, flags=re.M)   # the pin name leaks
    body = re.sub(r"\b(\w+)_buggy\.o\b", r"\1_unit.o", body)  # object names too
    # the dir name leaks; kernel paths (lib/<name>.c, .h) must stay intact
    body = re.sub(r"(?<![\w.])" + re.escape(t["dir"]) + r"(?![\w.])", f"t{idx:02d}", body)
    # the header comment describes the target's history; the build needs none
    # of it.  Keep the SPDX lines, drop the rest of the leading comment block.
    lines = body.splitlines()
    i = 0
    while i < len(lines) and lines[i].startswith("#") and (
        "SPDX" in lines[i] or "Copyright" in lines[i] or "hegel/LICENSE" in lines[i]):
      i += 1
    j = i
    while j < len(lines) and lines[j].startswith("#"):
      j += 1
    body = "\n".join(lines[:i] + lines[j:]) + "\n"
    # names of the stripped tests in clean recipes
    body = "\n".join(re.sub(r"\s+test_(?!hunt\b)\w+", "", l) if "rm -f" in l else l
                     for l in body.splitlines()) + "\n"
    body = scrub_comments(body, is_makefile=True)
    # absolute repo root (arena lives outside the repo tree) + simple run target
    body = re.sub(r"^REPO_ROOT\s*=.*$",
                  f"REPO_ROOT = {src_root}", body, flags=re.M)
    body = re.sub(r"^TESTS\s*=.*$", "TESTS = test_hunt", body, flags=re.M)
    body = re.sub(r"^test:.*?(?=^\S|\Z)", RUN_TARGET, body,
                  flags=re.M | re.S)
    mk.write_text(body)
  # scrub any other meta file (shims, leftover docs) — not the kernel tree
  for p in dst.rglob("*"):
    if p.is_file() and not in_kernel(p, dst) and is_meta(p) and p.name != "Makefile":
      try:
        p.write_text(scrub_comments(p.read_text(), is_makefile=False))
      except (UnicodeDecodeError, ValueError):
        pass
  # neutral brief for the blind hunter
  (dst / "BRIEF.md").write_text(
    "# Target\n\n"
    f"Write a hegel-c property test that exercises the C code in `linux/{t['natural']}` "
    "and report any bug you find with a minimal reproducer. Use the hegel-bughunt "
    "skill for methodology and the hegel skill for the API. Build with the "
    "provided Makefile (create `test_hunt.c`; `make` links it against the "
    "kernel sources in `linux/`). You are auditing unfamiliar code; no bug is "
    "guaranteed.\n")
  # GATE: scan meta files + filenames for surviving leaks (kernel tree exempt:
  # it IS the target)
  leaks = []
  for p in dst.rglob("*"):
    if not p.is_file() or in_kernel(p, dst):
      continue
    if LEAK.search(p.name):
      leaks.append(f"filename:{p.name}")
    if is_meta(p):
      try:
        for i, line in enumerate(p.read_text().splitlines(), 1):
          if "SPDX" in line:
            continue
          if LEAK.search(line):
            leaks.append(f"{p.relative_to(dst)}:{i}:{line.strip()[:60]}")
      except (UnicodeDecodeError, ValueError):
        pass
  return {"id": t["id"], "arena": str(dst), "difficulty": t["difficulty"],
          "natural": t["natural"], "gate": "PASS" if not leaks else "LEAK",
          "leaks": leaks[:8]}


def main():
  ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  ap.add_argument("-o", "--arena",
                  default="/tmp/claude-1000/-home-dev-workspace-hegel-c/"
                          "0bae734c-99a2-45b6-83e8-acf47f0fc218/scratchpad/"
                          "rediscovery_arena")
  ap.add_argument("--repo", default=".")
  args = ap.parse_args()
  arena = Path(args.arena)
  arena.mkdir(parents=True, exist_ok=True)
  write_arena_pin_mk(Path(args.repo).resolve(), arena)
  manifest = [stage(t, Path(args.repo).resolve(), arena, i)
              for i, t in enumerate(POOL)]
  (arena / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
  npass = sum(1 for m in manifest if m["gate"] == "PASS")
  print(f"staged {len(manifest)} targets -> {arena}")
  for m in manifest:
    tag = "OK  " if m["gate"] == "PASS" else "LEAK"
    print(f"  [{tag}] t{POOL.index(next(t for t in POOL if t['id']==m['id'])):02d} "
          f"{m['id']:<12} ({m['difficulty']})"
          + ("" if m["gate"] == "PASS" else f"  <- {len(m['leaks'])} leak(s)"))
    for lk in m["leaks"]:
      print(f"          leak: {lk}")
  print(f"\n{npass}/{len(manifest)} passed the blindness gate and are eligible.")


if __name__ == "__main__":
  main()
