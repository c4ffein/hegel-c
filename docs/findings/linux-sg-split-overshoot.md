<!-- SPDX-License-Identifier: MIT AND GPL-2.0-only
     Copyright (c) 2026 c4ffein
     SPDX-FileCopyrightText: The Linux kernel authors (lib/sg_split.c excerpt)
     Part of hegel-c — see hegel/LICENSE for terms. The quoted kernel code is
     GPL-2.0-only, as in its upstream file. -->

# sg_split `nb_splits` overshoot — how a bonus finding became a patch series

Kept as a record of hegel-c's first finding that went upstream: a bug found
*in passing*, while a blind test was hunting a different, already-fixed bug in
the same file.

## What happened after this write-up

- **2026-05-25** — found by the blind rediscovery subagent hunting
  `8b46fdaea819` (the offset reset). The write-up below is from that day.
- **2026-05-31** — prior art: Alexander Egorenkov reported the same bug in
  2021 (lore `20210418143425.22944-1-egorenar-dev@posteo.net`), never
  reviewed or merged. His fix moves the break above the second decrement,
  which breaks partial coverage ending exactly on an entry boundary (test
  T10). Unpatched 9/16 pass, his fix 15/16, ours (`nb_splits <= 0`) 16/16.
- **2026-06-10** — sent upstream as patch 1 of a 5-patch v2 series (fix,
  KUnit tests, related cleanups), threaded under v1. The patches as sent are in
  `submissions/linux-sg-split-series/` (v1, v2).
- **2026-07-12** — the blind re-run found it again independently.
- **2026-10-19** — the `sg_split` pin applies our one-line fix
  (`tests/irl/linux/pins/sg_split-fixed_alt.patch`) so the pinned
  offset-reset bug can be judged on its own.
- **2026-10-22** — still present: `sg_calculate_split()` is byte-identical in
  v7.3 and linux-next; the series is not merged.

So the "Verification status" and "Recommended next steps" sections below are
superseded by the timeline above. Impact stays as assessed in June: no in-tree
caller triggers it (four of five use full coverage or a DMA-mapped re-split; the
fifth re-clamps), so it is a latent bug for future partial-coverage callers.

## The original write-up (2026-05-25; only heading levels changed)

**Confidence: high.** Found during the blind PBT rediscovery experiment for
upstream commit `8b46fdaea819` (the offset-reset bug). The targeted bug was
rediscovered cleanly; this is a **separate** logic bug in the same file
(`lib/sg_split.c`) that the upstream fix did **not** touch and that — based
on a code read of current HEAD — appears to still be present in the kernel
tree.

Date: 2026-05-25.
Found by: blind PBT subagent in `tests/irl/sg_split/test_sg_split.c`.

### Context

The blind PBT for `sg_split` was asked to test the scatterlist splitting API
with the upstream commit `8b46fdaea819` (the `out_sg->offset = 0;` reset bug)
reverted into the snapshot under test. The subagent's harness asserts two
properties:

- **A.** Sum of entry lengths in each output split equals the requested size.
- **B.** Byte-level oracle: i-th byte of split k equals the
  (skip + sum(sizes[0..k-1]) + i)-th byte of the input.

Property B fires on the targeted offset-reset bug (B2). Property A fires on a
**different** bug (B1) that the upstream fix did not touch.

### The bonus bug (Bug B1)

`sg_calculate_split` in `lib/sg_split.c` decrements `nb_splits` in two
locations inside the same outer loop iteration, and the break check uses
`if (!nb_splits) break;` — testing for exactly zero. When both decrement
sites fire in the same iteration, `nb_splits` overshoots into negative
territory and the break is skipped.

Relevant code, lines 35–72 of the snapshot (matches current upstream):

```c
for_each_sg(in, sg, nents, i) {
    sglen = mapped ? sg_dma_len(sg) : sg->length;
    if (skip > sglen) { skip -= sglen; continue; }

    len = min_t(size_t, size, sglen - skip);
    if (!curr->in_sg0) { curr->in_sg0 = sg; curr->skip_sg0 = skip; }
    size -= len;
    curr->nents++;
    curr->length_last_sg = len;

    while (!size && (skip + len < sglen) && (--nb_splits > 0)) {  /* (1) */
        curr++;
        size = *(++sizes);
        skip += len;
        len = min_t(size_t, size, sglen - skip);
        curr->in_sg0 = sg;
        curr->skip_sg0 = skip;
        curr->nents = 1;
        curr->length_last_sg = len;
        size -= len;
    }
    skip = 0;

    if (!size && --nb_splits > 0) {                               /* (2) */
        curr++;
        size = *(++sizes);
    }

    if (!nb_splits)                                                /* (3) */
        break;
}
```

Both (1) and (2) decrement `nb_splits`. (3) only triggers on exactly 0.

### Minimal failing input (shrunk by Hegel)

```
input scatterlist: 2 entries
  in[0] = { page=0, offset=0, length=2 }
  in[1] = { page=0, offset=0, length=1 }
skip      = 0
nb_splits = 1
sizes     = [1]
```

Expected: split[0] has total length 1 (one byte from in[0]).
Actual:   split[0] has total length > 1 (entries from both in[0] and in[1]).

### Trace

**Iteration 1** (sg = in[0], sglen = 2):
- `skip > sglen`? 0 > 2, no.
- `len = min(1, 2)` = 1.
- `curr->nents` = 1, `curr->length_last_sg` = 1.
- `size -= len` → size = 0.
- Inner while (1): `!size && (skip+len < sglen) && (--nb_splits > 0)`
  → `true && (0+1 < 2) && (nb_splits 1→0, 0>0 false)`. Loop body skipped, but
  **nb_splits is now 0**.
- Outer if (2): `!size && (--nb_splits > 0)`
  → `true && (nb_splits 0→-1, -1>0 false)`. Body skipped, but
  **nb_splits is now -1**.
- Check (3): `!nb_splits` → `!(-1)` → false. **Loop does not break.**

**Iteration 2** (sg = in[1], sglen = 1):
- Continues normally — `curr->nents++` → 2, `length_last_sg` overwritten to 0.
- Both decrement sites fire again, dropping nb_splits to -3.

Resulting splitter has `nents = 2` but mismatched `length_last_sg = 0`. In
`sg_split_phys`, the first output entry gets the full length-2 copy from
in[0] while the last entry gets trimmed to 0. Split 0 total = 2 bytes when
1 was requested.

### Proposed fix

In `sg_calculate_split`, gate the work that follows on `size > 0`. Simplest
patch (subagent's suggestion, traced and verified to fix the minimal repro):

```diff
 for_each_sg(in, sg, nents, i) {
     sglen = mapped ? sg_dma_len(sg) : sg->length;
     if (skip > sglen) { skip -= sglen; continue; }
+    if (!size) continue;

     len = min_t(size_t, size, sglen - skip);
```

This stops appending to `curr` once the current split is full and prevents
the second decrement of `nb_splits` from undershooting the break check.

Alternative (more invasive): consolidate to a single decrement of
`nb_splits` per split-completed event, and change the break check to
`<= 0` so a future double-decrement doesn't escape.

### Verification status

- **Reproduced**: yes, minimal repro is in
  `tests/irl/sg_split/test_sg_split.c` property A.
- **Fix verified locally**: not yet — we should apply the patch to
  `sg_split_buggy.c`, re-run the harness, and confirm property A goes green.
- **Verified against current upstream**: not yet — should `grep` current
  `lib/sg_split.c` HEAD for the same `--nb_splits` pattern and confirm
  the code looks the same.
- **Verified against later upstream fixes**: not yet — should
  `git log --grep="Fixes: 8b46fdaea819"` and broader `git log -- lib/sg_split.c`
  since 2025-03 to see if anyone caught this after the offset fix landed.

### Recommended next steps

1. Apply the proposed fix to the snapshot, re-run the test, confirm green.
2. Check current upstream `lib/sg_split.c` for the same logic.
3. If still present: this is reportable. Worth sending to
   `linux-kernel@vger.kernel.org` Cc: the maintainer
   (`git log -1 --pretty="%an %ae" -- lib/sg_split.c`).
4. Consider this as one data point in the "PBT finds bonus bugs while
   rediscovering a known bug" sub-finding of the larger experiment.
