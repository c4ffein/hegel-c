<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Rediscovery corpus — ours + sashiko's

hegel-c rediscovery hunting draws on **two labeled bug corpora**, merged into
one worklist. Keeping both matters: they have complementary biases, and the
overlap is a free cross-validation signal.

## The two sources

| | `docs/our_corpus.json` | sashiko `benchmark.json` |
|---|---|---|
| Origin | Curated by us (git-log/syzbot mining) + bugs hegel-c actually found | Sashiko's ~1000 unfiltered `Fixes:`-tagged upstream bugs |
| Size | 20 (19 curated + our own finds) | 999 → ~236 PBT-eligible after filtering |
| Bias | Selection bias toward *harnessable* bugs (we picked ones with a userspace path) | Unfiltered — measures what the method *actually* reaches, not what looks findable |
| License | MIT — lives in-tree | Apache-2.0 descriptions — stay in gitignored `.corpus/` |
| Extra metadata | `harness_dir`, `status`, `provenance`, `classes` | subsystem + description only |

Our corpus carries provenance we can't get from sashiko: which bugs we've
already built harnesses for (`harness_dir`), which we've *found ourselves*
(`sg_split` overshoot, `packing.c` OOB), and which are still just candidates.
Sashiko's corpus is far larger and unbiased. Neither subsumes the other.

## The pipeline

```
sashiko benchmark.json ──filter-corpus.py──► .corpus/judge_key.json ─┐
                                                                     ├─merge-corpus.py─► .corpus/merged_{hunt_targets,judge_key}.json
docs/our_corpus.json ────────────────────────────────────────────── ┘
```

- `tools/filter-corpus.py` — keep only dynamically-observable bug classes
  (leak/UAF/OOB/overflow/off-by-one/uninit/null-deref/hang/wrong-result),
  drop static-only classes (locking/hardware/build/DT). Splits hunter-visible
  (SHAs only) from judge-only (descriptions).
- `tools/merge-corpus.py` — union both corpora, **dedupe by fix SHA** (12-hex
  normalized, so a short SHA in ours matches a full SHA in theirs). Matched
  entries become `source: "both"`.

Run:

```bash
make inspiration                     # ensures sashiko is checked out at the pin
tools/filter-corpus.py               # -> .corpus/{hunt_targets,judge_key}.json
tools/merge-corpus.py                # -> .corpus/merged_*.json
```

Current merge (sashiko pin `a434adc`): **255 unique bugs** — 19 ours-only, 235
theirs-only, **1 in both** (`c6e8e595a079`, the idr range bug). 12 have an
existing harness under `tests/irl/linux/`.

## Blind hygiene

The 2026-07-12 rediscovery re-run showed that a leaked hint (even a Makefile
comment naming the bug) invalidates a recall number. So the split is
structural, not a promise: a hunter session sees only
`merged_hunt_targets.json` (fix SHA + subsystem + tier + harness_dir), never a
description. Descriptions live only in `merged_judge_key.json`, which the judge
reads to decide whether a finding matches a known bug. Before a hunt, scan the
chosen harness for leaked descriptions in comments / test names / Makefile
text.

## Live rediscovery results (2026-07-12)

Ran the two ready tier-A harnesses to confirm the skill re-finds the bug from
the buggy commit:

| Target | Source | Buggy snapshot | Class | Result |
|---|---|---|---|---|
| `lib/idr` idr_alloc range | **both** | `c6e8e595a079~1` | wrong-result | ✅ re-found — shrunk to `alloc(0,1)` on `IDR_INIT_BASE(1)`, `id=1 > max=0` |
| `lib/packing` LE OOB | ours (found) | `packing_subject.c` | oob | ✅ re-found — ASAN heap-buffer-overflow on non-mult-of-4 buffer |

Both fired within the default sweep. The idr harness is the smoke test the
candidates doc demanded ("if we miss this we have a real problem") — it passes.
Remaining tier-A harness dirs (maple_*, xarray_pause, bch_mod8, bunzip2,
bootconfig) are the next rediscovery batch.
