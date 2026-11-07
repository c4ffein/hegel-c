<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Draft issue for hegeldev/hegel: an average size for collections

Status: **draft, not posted.** Written 2026-10-30 against libhegel 0.48.0.
Target: hegeldev/hegel, component `libhegel`.

---

**Title:** libhegel: let `hegel_new_collection` take an average size

**Problem.** The collection protocol picks its length distribution from
`min_size` / `max_size` alone. `length_p_continue`
(`libhegel/src/native/core/state.rs`) derives the average as
`min(max(min * 2, min + 5), 0.5 * (min + max))`, so with `min = 0` every
collection aims for about 5 extra elements whatever the maximum: a
`max_size` of 50, 1000 or `UINT64_MAX` all give a geometric length with
a mean around 6. The C ABI has no way to ask for more:

<!-- /ignore excerpt of upstream libhegel/include/hegel.h, not in this repo -->
```c
hegel_result_t hegel_new_collection(hegel_context_t *ctx, hegel_test_case_t *tc,
                                    uint64_t min_size, uint64_t max_size,
                                    hegel_collection_t **out_collection);
```

A concrete case from hegel-c (the C binding over libhegel): a graph
harness generating edge lists. Drawn through the collection protocol
with a generous `max_size`, its graphs averaged **6.4 edges**; the same
harness drawing the length itself (`hegel_generate_integer(0, max)`,
then that many edges) averaged **~60**. The collection version is
better in every other way: the engine owns the length, shrinking deletes
elements directly, and `hegel_collection_reject` handles duplicate
edges. But it practically never builds a graph big enough to matter.

**Why it matters.** Some code only misbehaves at scale: a hash table
that resizes after 64 entries, a partitioner that takes a different path
above a few dozen vertices, a buffer that grows in chunks, a tree that
rebalances at a height. Mean-6 inputs reach those paths rarely or never,
so harnesses fall back to drawing a length, giving up element-deletion
shrinking and `reject`. The workaround also has to re-derive a size
distribution by hand that the engine already knows how to draw.

**Proposal.** One new entry point, the existing one unchanged:

<!-- /ignore proposed upstream ABI, not hegel-c code -->
```c
/* Like hegel_new_collection, with the length steered toward
 * `average_size` (clamped to [min_size, max_size]).  0 means "derive it
 * from min/max as hegel_new_collection does". */
hegel_result_t hegel_new_collection_with_average(hegel_context_t *ctx,
                                                 hegel_test_case_t *tc,
                                                 uint64_t min_size,
                                                 uint64_t max_size,
                                                 double average_size,
                                                 hegel_collection_t **out_collection);
```

Inside, `ManyState::new` would pass the caller's average to
`length_p_continue` instead of computing it; the continue/stop choices,
`reject`, the forced stop at `max_size` and shrinking all stay as they
are, and the choice sequence of existing callers doesn't change.
Frontends could expose it as an optional `average_size` on their list
generators; Hypothesis's own `many()` still takes one internally.

**The counter-argument, which we know.** Hypothesis deliberately keeps
lists small and dropped the public `average_size` argument of `lists()`
(deprecated in the 3.x series, removed in 4.0): small inputs find most
bugs, run faster, and shrink faster, and a user-chosen average tends to
be set once, too high, and slow every test down. Our answer: the default
should stay as it is; the knob is for the cases where the author knows
the interesting behaviour starts at a size the default almost never
reaches, and where the alternative today (drawing a length) is worse
for shrinking than a large collection would be. Targeting
(`hegel_target` on the length) helps only late in a run and only once a
large-ish case has been seen.

---

Notes for us (not part of the issue):
- Until this exists, hegel-c's guidance stays: drawn length for large
  lists, the collection protocol when small lists are what you want
  (`CLAUDE.md`, "Writing generators").
- Before posting, re-measure the 6.4 / ~60 numbers on the current
  harness and engine, and check upstream hasn't added something similar
  (`git log -S average libhegel/`).
