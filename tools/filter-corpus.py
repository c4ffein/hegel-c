#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
"""Filter sashiko's labeled kernel-bug corpus down to PBT-eligible targets.

Sashiko (inspiration/kernel-review/sashiko, pinned — see Makefile SASHIKO_PIN)
ships benchmarks/benchmark.json: ~1000 unfiltered Fixes:-tagged upstream bugs,
each {Commit, Fixed-by, subsystem, problem_description}.  That is ground truth
for blind-rediscovery runs — bigger and less biased than hand-curated
shortlists (it contained BUG_REDISCOVERY_CANDIDATES.md's #1 idr bug verbatim).

This tool selects the slice a userspace hegel-c harness can plausibly reach
and splits the output for blind hygiene (the 2026-07-12 re-run showed hint
leakage compromises scoring):

  hunt_targets.json — Commit, Fixed-by, subsystem, tier.  NO description.
                      This is ALL a hunter may see.
  judge_key.json    — full entries incl. problem_description.  Judge-only.

Eligibility = bug CLASS is dynamically observable (crash/ASAN/LSAN/invariant)
AND the class is not static-only territory (lockdep, DT, build, hardware).
Subsystem tiers describe harnessing effort, not eligibility:
  A — lib-like, known-stubbable (lib/*, idr, xarray, maple_tree, ...)
  B — core-algorithmic, harnessable with real shim work (mm, net core, ...)
  C — everything else (drivers etc.); kept in judge_key, dropped from targets
      unless --all-tiers.

Usage:
  tools/filter-corpus.py [-i benchmark.json] [-o .corpus/] [--all-tiers]
Judge-key text is Apache-2.0 (sashiko) — keep outputs out of the MIT tree
(default output dir is gitignored).
"""

import argparse, json, re, sys
from pathlib import Path

DYNAMIC_CLASSES = {  # observable by executing the code under ASAN/LSAN/UBSAN + invariants
    "leak": r"\bleak",
    "uaf": r"use.after.free|\buaf\b|after (it is|being|the .{0,20}is) freed",
    "double-free": r"double.free",
    "oob": r"out.of.bounds|\boob\b|beyond the (end|bounds)|past the end",
    "overflow": r"overflow",
    "off-by-one": r"off.by.one",
    "uninit": r"uninitializ",
    "null-deref": r"null pointer|null deref",
    "hang": r"infinite loop|hang|never terminates|busy.loop",
    "wrong-result": r"incorrect(ly)? (result|value|calculat|comput|return)|returns? (an? )?(id|value|size|length)s? outside|wrong (result|value|size|length)",
    "error-path": r"error (path|handling)",
}

STATIC_ONLY = {  # sashiko's turf: not observable in a single-threaded userspace harness
    "locking": r"deadlock|lockdep|race|atomic context|rcu|spinlock|mutex.*(held|order)|concurren",
    "hardware": r"\bdma\b|register|firmware|\birq\b|interrupt|suspend|resume|endian|\bphy\b|hardware",
    "build": r"build (fail|error)|compile|kconfig|makefile|linker|\blto\b|devicetree|\bdts\b|binding",
    "docs": r"documentation|comment|typo|spelling",
}

TIER_A = r"^(lib\b|lib/|idr|xarray|maple_tree|radix|bitmap|string|vsprintf|kstrto|list|rbtree|scatterlist|sg\b|kunit|bootconfig|\bmpi\b|zstd|lzo|lz4|zlib|bunzip|decompress|crc|xxhash|siphash|asn\.?1|bch\b|packing|parman|objagg|textsearch|kfifo|bitmap)"
TIER_B = r"^(mm\b|mm/|slab|slub|vmalloc|net\b|net/core|ipv[46]|tcp|udp|epoll|vfs|fs\b|ext4|btrfs|block|bpf|sched/fair|time(r|keeping))"
# device-driver subtrees the broad net/mm prefixes would misclassify
TIER_B_DEMOTE = r"mlx5|dsa|wwan|idpf|\bice\b|can\b|team|bonding|phy|wireless|wifi|mac80211"


def classify(desc):
  d = desc.lower()
  hits = [k for k, p in DYNAMIC_CLASSES.items() if re.search(p, d)]
  blocks = [k for k, p in STATIC_ONLY.items() if re.search(p, d)]
  return hits, blocks


def tier(subsystem):
  s = subsystem or ""
  if re.match(TIER_A, s, re.I):
    return "A"
  if re.match(TIER_B, s, re.I) and not re.search(TIER_B_DEMOTE, s, re.I):
    return "B"
  return "C"


def main():
  ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  ap.add_argument("-i", "--input",
                  default="inspiration/kernel-review/sashiko/benchmarks/benchmark.json")
  ap.add_argument("-o", "--outdir", default=".corpus")
  ap.add_argument("--all-tiers", action="store_true",
                  help="include tier C (driver-heavy) entries in hunt_targets.json")
  args = ap.parse_args()

  entries = json.load(open(args.input))
  eligible, key = [], []
  stats = {"total": len(entries), "dynamic": 0, "blocked": 0, "tiers": {"A": 0, "B": 0, "C": 0}}

  for e in entries:
    desc = e.get("problem_description") or ""
    hits, blocks = classify(desc)
    if not hits or blocks:
      stats["blocked"] += bool(blocks)
      continue
    stats["dynamic"] += 1
    t = tier(e.get("subsystem"))
    stats["tiers"][t] += 1
    full = dict(e, classes=hits, tier=t)
    key.append(full)
    if t in ("A", "B") or args.all_tiers:
      eligible.append({k: full[k] for k in ("Commit", "Fixed-by", "subsystem", "tier")})

  out = Path(args.outdir)
  out.mkdir(parents=True, exist_ok=True)
  (out / "hunt_targets.json").write_text(json.dumps(eligible, indent=1) + "\n")
  (out / "judge_key.json").write_text(json.dumps(key, indent=1) + "\n")

  print(f"corpus: {stats['total']} entries")
  print(f"  dynamic-class, not static-blocked: {stats['dynamic']}"
        f"  (tiers A={stats['tiers']['A']} B={stats['tiers']['B']} C={stats['tiers']['C']})")
  print(f"  -> {out/'hunt_targets.json'}: {len(eligible)} hunter-visible targets (no descriptions)")
  print(f"  -> {out/'judge_key.json'}: {len(key)} full entries (JUDGE ONLY — do not show hunters)")


if __name__ == "__main__":
  sys.exit(main())
