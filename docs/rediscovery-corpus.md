<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Rediscovery corpus — ours + sashiko's

hegel-c rediscovery hunting draws on **two labeled bug corpora**, merged into
one worklist. Keeping both matters: they have complementary biases, and the
overlap is a free cross-validation signal.

## The two sources

| | `docs/corpus.toml` | sashiko `benchmark.json` |
|---|---|---|
| Origin | Curated by us (git-log/syzbot mining) + bugs hegel-c actually found | Sashiko's ~1000 unfiltered `Fixes:`-tagged upstream bugs |
| Size | 22 (20 curated + 2 of our own finds) | 999 → ~236 PBT-eligible after filtering |
| Bias | Selection bias toward *harnessable* bugs (we picked ones with a userspace path) | Unfiltered — measures what the method *actually* reaches, not what looks findable |
| License | MIT — lives in-tree | Apache-2.0 descriptions — stay in gitignored `.corpus/` |
| Extra metadata | `harness_dir`, `pin`, `status`, `provenance`, `classes`, mechanism, harness plan, property to check | subsystem + description only |

Our corpus carries provenance we can't get from sashiko: which bugs we've
already built harnesses for (`harness_dir`), which we've *found ourselves*
(`sg_split` overshoot, `packing.c` OOB), and which are still just candidates.
Sashiko's corpus is far larger and unbiased. Neither subsumes the other.

Bugs hegel-c found itself are tracked with their upstream status in
`docs/findings.toml`; their targets here point at it (`finding = ...`) and
their `found-*` status mirrors it (lint checks). Each target's fields are
documented in the file's header; `make lint-findings` checks them, that each
harness has a pin and that the pin names the same fix.

`status` in our corpus (hunter-visible, so it never describes the bug):

| Status | Meaning |
|---|---|
| `candidate` | Picked; no harness, or not hunted yet |
| `rediscovered` | Found by a hunt that was blind, and the harness is red on buggy, green on fixed |
| `harness-only` | A harness discriminates (red on buggy, green on fixed), but no hunt found the bug blind: the hint leaked, the red was an oracle or snapshot artifact, or rules were added after the hunt |
| `missed` | Harness exists but never reaches the bug (buggy green) |
| `found-and-submitted` / `found-novel` | hegel-c found a bug upstream didn't know about (sent / not yet sent) |

What each run found (the 2026-05-25 run, the 2026-07-12 re-run that found
hints leaking through harness Makefile comments, the 2026-10-19 pin runs that
exposed fake hits) is in `docs/runs.toml`, one verdict per target per run,
keyed by the `fixed_by` SHA here; `make runs` shows the table. Runs name bugs,
so like the findings ledger they never reach a hunter.

## The pipeline

```
sashiko benchmark.json ──filter-corpus.py──► .corpus/judge_key.json ─┐
                                                                     ├─merge-corpus.py─► .corpus/merged_{hunt_targets,judge_key}.json
docs/corpus.toml ────────────────────────────────────────────────── ┘
```

- `tools/filter-corpus.py` — keep only dynamically-observable bug classes
  (leak/UAF/OOB/overflow/off-by-one/uninit/null-deref/hang/wrong-result),
  drop static-only classes (locking/hardware/build/DT). Splits hunter-visible
  (SHAs only) from judge-only (descriptions).
- `tools/merge-corpus.py` — convert our targets to sashiko's shape
  (`fixed_by` → `Fixed-by`, `mechanism` → `problem_description`), union both
  corpora, **dedupe by fix SHA** (12-hex normalized, so a short SHA in ours
  matches a full SHA in theirs). Matched entries become `source: "both"`.

Run:

```bash
make inspiration                     # ensures sashiko is checked out at the pin
tools/filter-corpus.py               # -> .corpus/{hunt_targets,judge_key}.json
tools/merge-corpus.py                # -> .corpus/merged_*.json
```

Current merge (sashiko pin `a434adc`): **257 unique bugs** — 21 ours-only, 235
theirs-only, **1 in both** (`c6e8e595a079`, the idr range bug). 13 have an
existing harness under `tests/irl/linux/`.

## How we pick targets

Our 20 curated targets came from two rounds in May 2026, mined from
`git log --since=2024-11-01 --grep=Fixes: -- lib/ fs/` and syzbot's
fixed-bugs list, ranked on harness effort times interest. What they taught:

- **Scaffolding cost decides everything.** In round 1, the two targets whose
  harness already existed (idr, maple cyclic) were found; the five
  filesystem picks (`effort = "medium"`, hours of stubs each) were never run.
  Round 2 took only `ready` / `small` targets, biased toward files that
  `tools/testing/radix-tree` already compiles.
- **`tools/testing/radix-tree` covers more than expected:** idr, maple tree,
  xarray, radix tree, slab, enough cpumask for `group_cpus` and other small
  `lib/` files. Worth scoping what else compiles there with no extra stubs.
- **`lib/maple_tree.c` is the densest source of real bugs in `lib/`** (ten
  `Fixes:` commits in 18 months). Three maple targets on one harness separate
  the bug-class signal from scaffolding noise.
- **Sanitizer fixes are free targets.** Fixes that cast to unsigned or widen
  to `ULL` to silence `-fsanitize=undefined` (bch, bunzip2) are found by the
  sanitizer the harnesses already build with; a round-trip alone misses them.
- **Skip fixes that aren't bugs** a test can see: docs-only, dead code, build
  workarounds, tracepoint strings, added `BUG_ON`s.
- **Mind diagnostic-content bugs.** The bootconfig bug is in what an error
  message says; a property that only checks that an error exists misses it.
- **A series is several targets for one harness** (the bootconfig series:
  three one-line fixes in one file).
- syzbot's fixed list is mostly networking, wireless and USB; filesystem-image
  bugs are its most PBT-friendly part.

## Blind hygiene

The 2026-07-12 rediscovery re-run showed that a leaked hint (even a Makefile
comment naming the bug) invalidates a recall number. So the split is
structural, not a promise: a hunter session sees only
`merged_hunt_targets.json` (fix SHA + subsystem + tier + harness_dir), never a
description. Descriptions live only in `merged_judge_key.json`, which the judge
reads to decide whether a finding matches a known bug. Before a hunt, scan the
chosen harness for leaked descriptions in comments / test names / Makefile
text.
