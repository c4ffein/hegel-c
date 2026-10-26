#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 c4ffein
# Part of hegel-c — see hegel/LICENSE for terms.
"""Union our curated/found bug corpus with sashiko's filtered corpus.

Two sources, one merged rediscovery worklist:
  ours    docs/corpus.toml             — hegel-c's rediscovery targets: curated
                                          upstream fixes + our own finds (idr,
                                          sg_split, packing, ...). MIT; in-tree.
  theirs  .corpus/judge_key.json        — sashiko's PBT-eligible slice, produced
                                          by tools/filter-corpus.py. Apache-2.0
                                          descriptions; stays in gitignored .corpus/.

Dedup key is the fix SHA (Fixed-by), normalized to 12 hex chars — one source
may carry the full 40-char SHA and the other a short one (the idr bug is in
BOTH: ours c6e8e595a079, theirs c6e8e595a0798ad6...). A matched entry gets
source "both" and keeps our richer metadata (harness_dir, status, provenance).

Emits the same blind split as filter-corpus.py:
  merged_hunt_targets.json — Fixed-by + subsystem + tier + source + harness_dir.
                             NO description. Hunter-visible.
  merged_judge_key.json    — full entries. Judge-only.

Our targets are converted to sashiko's shape: fixed_by -> Fixed-by, mechanism
(or title) -> problem_description; a target that is one of our findings has no
fix SHA and is keyed by its finding id.

Usage: tools/merge-corpus.py [--ours docs/corpus.toml]
                             [--theirs .corpus/judge_key.json] [-o .corpus/]
Keep merged_judge_key.json out of the MIT tree — it embeds Apache-2.0 text.
"""

import argparse, json, sys, tomllib
from pathlib import Path


def norm(sha):
  return sha.lower()[:12] if sha else None


def as_sashiko(t):
  """One docs/corpus.toml target in the shape of a sashiko benchmark entry."""
  e = {k: t[k] for k in ("subsystem", "classes", "provenance", "tier", "harness_dir",
                         "status", "finding") if k in t}
  e["Fixed-by"] = t.get("fixed_by")
  e["problem_description"] = " ".join(t.get("mechanism", t["title"]).split())
  return e


def main():
  ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  ap.add_argument("--ours", default="docs/corpus.toml")
  ap.add_argument("--theirs", default=".corpus/judge_key.json")
  ap.add_argument("-o", "--outdir", default=".corpus")
  args = ap.parse_args()

  with open(args.ours, "rb") as f:
    ours = [as_sashiko(t) for t in tomllib.load(f).get("target", [])]
  theirs = json.load(open(args.theirs)) if Path(args.theirs).exists() else []
  if not theirs:
    print(f"note: {args.theirs} missing — run tools/filter-corpus.py first "
          f"(merging ours only)", file=sys.stderr)

  merged = {}          # key -> entry ; key = norm(Fixed-by) or synthetic
  order = []

  def add(e, source):
    key = norm(e.get("Fixed-by")) or e.get("finding") or f"_nofix_{e.get('subsystem')}_{len(order)}"
    if key in merged:
      prev = merged[key]
      prev["source"] = "both"
      # keep whichever description is longer/richer; keep our harness metadata
      for f in ("harness_dir", "status", "provenance", "tier", "classes"):
        if e.get(f) and not prev.get(f):
          prev[f] = e[f]
      if len(e.get("problem_description", "")) > len(prev.get("problem_description", "")):
        prev["problem_description"] = e["problem_description"]
      return
    merged[key] = dict(e, source=source)
    order.append(key)

  for e in ours:
    add(e, "ours")
  for e in theirs:
    add(e, "theirs")

  full = [merged[k] for k in order]
  targets = [{k: e.get(k) for k in
              ("Fixed-by", "subsystem", "tier", "source", "harness_dir", "status")}
             for e in full]

  out = Path(args.outdir)
  out.mkdir(parents=True, exist_ok=True)
  (out / "merged_hunt_targets.json").write_text(json.dumps(targets, indent=1) + "\n")
  (out / "merged_judge_key.json").write_text(json.dumps(full, indent=1) + "\n")

  import collections
  src = collections.Counter(e["source"] for e in full)
  har = sum(1 for e in full if e.get("harness_dir"))
  print(f"merged corpus: {len(full)} unique bugs by fix SHA")
  print(f"  sources: ours-only={src['ours']} theirs-only={src['theirs']} both={src['both']}")
  print(f"  with an existing harness_dir: {har}")
  print(f"  -> {out/'merged_hunt_targets.json'} (hunter-visible, no descriptions)")
  print(f"  -> {out/'merged_judge_key.json'} (JUDGE ONLY)")
  if src["both"]:
    both = [e["Fixed-by"] for e in full if e["source"] == "both"]
    print(f"  cross-corpus overlap (in both ours AND sashiko): {both}")


if __name__ == "__main__":
  sys.exit(main())
