<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Blind rediscovery re-run — 2026-07-12 scorecard

Baseline = 2026-05-25 (7 HIT / 1 PARTIAL / 2 MISS + 2 bonus).
Contamination fixed before run: idr + maple_tree_rediscovery had fixes applied on
disk; reverted to genuine buggy state and verified reproducing.

## BLINDNESS LEAK (major finding — affects THIS run AND the 2026-05-25 baseline)
The sandbox Makefile header comments (inherited from the original scaffolds)
describe the bug. 7/10 are SEVERE (name the mechanism; bch even names the fix):
maple_store_root, maple_destroy_walk, maple_prealloc, xarray_pause, bch_mod8,
bunzip2, sg_split. 3/10 are MILD (opaque fix-SHA only, no mechanism): idr,
maple_tree_rediscovery, bootconfig. Every hunter disclosed reading it.
=> Run A (this one) measures FRAMEWORK+SKILL ERGONOMICS (can you operationalize a
   known bug into a discriminating PBT + correct fix + good shrink) reliably, but
   its blind-DETECTION claim is compromised for the 7 severe targets.
=> A clean "Run B" with neutralized Makefiles is needed for the true blind number.
   Same leak was present in the 2026-05-25 scaffolds, so that run's blind claim is
   also overstated for these targets.

| # | Target | Hunter claim | Judge verdict | 2026-05-25 | Notes |
|---|--------|--------------|---------------|-----------|-------|
| 1 | idr_rediscovery | FOUND (wrong bug) | **MISS (targeted); real bonus bug** | HIT | MILD leak → honest true-negative. MISSED range-below-base (harness never varied base; targeted bug still unfixed). BONUS: ida_find_first_range -ENOENT-instead-of-lowest-id — CONFIRMED real in pristine + CURRENT UPSTREAM. Possibly reportable. +minor retry leak. |
| 2 | maple_tree_rediscovery | FOUND | **HIT — BLIND CREDIT GRANTED** | HIT | ⭐ Cleanest result. Mild leak (opaque SHA). Property from contract, source read post-localization. Fix=mas_reset (upstream 1fd8bc7cd889). Decent shrink. |
| 3 | maple_store_root | FOUND | **HIT (artifact); no blind credit (leak)** | HIT | Verified: load-oracle NEVER fires, only emptiness invariant; fix=upstream 0ea120b278ad. |
| 4 | maple_destroy_walk | FOUND | **HIT (artifact); no blind credit (leak)** | MISS | Test discriminates on exact fix line; fix = upstream ea9b77f98d94. INSIGHT: race *symptom* uncatchable single-thread, but the *invariant* (dead-mark before call_rcu free) IS — via rcu_read_lock defer. Partially overturns "concurrency=structural miss". |
| 5 | maple_prealloc | FOUND (phantom) | **MISS (targeted) + FALSE POSITIVE** | PARTIAL | SEVERE leak. "Leak" = HARNESS ARTIFACT (shim kfree≠nr_allocated; upstream uses kfree at same sites). Targeted MA_STATE_PREALLOC bug never found. Only false-positive of the run. Lesson: blind hunter can't tell shim artifact from real bug w/o reference. |
| 6 | xarray_pause | FOUND (wrong bug) | **MISS (targeted); real bonus bug** | HIT | ⭐KEY: SEVERE leak yet MISSED — target needs mid-entry pause (cachestat pattern); hunter only paused at aligned bases (mask=no-op). Proves "knowing the bug ≠ triggering it". BONUS: xa_store_range(_,ULONG_MAX,_) infinite loop, confirmed hang, fixed. Possibly new. |
| 7 | bch_mod8 | FOUND | **HIT (artifact); no blind credit (leak)** | HIT | Verified: round-trip alone misses it, only UBSAN catches; fix=upstream 010d7d984650. |
| 8 | bunzip2 | FOUND | **HIT (artifact); no blind credit (leak)** | HIT | Verified. fix=1U (upstream used 1ULL — sufficient, narrower). **BONUS CONFIRMED REAL** (ASAN): heap over-read get_bits:144 on truncated input, plausibly unpatched upstream (trusted-preboot caveat). |
| 9 | sg_split | FOUND | **HIT (artifact); no blind credit (leak)** | HIT | Verified red/green; fix=upstream 8b46fdaea819. Bonus(2) nb_splits over-decrement CONFIRMED matches BONUS_sg_split.md. |
| 10 | bootconfig_rediscovery | FOUND | **HIT** (verified) | MISS | MISS→HIT. Byte-identical upstream fix 39ebc8d7f561. Content-property nudge deemed appropriate by judge. Decent shrink. |
