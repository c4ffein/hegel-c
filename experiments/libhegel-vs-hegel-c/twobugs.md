<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Pair 5, two bugs: `midpoint (lo, hi)` over the whole int range

Two independent bugs in one function ([sut.h](sut.h)):

- **A:** `lo + hi` wraps for large values, so the result falls outside `[lo, hi]`;
- **B:** C division rounds toward zero, so for a negative odd sum the result is one above `floor ((lo + hi) / 2)`.

| file | code lines | of which boilerplate | what it does |
|---|---:|---:|---|
| [lite_twobugs.c](lite_twobugs.c) | 53 | 32 | two origins (static strings), one counterexample printed |
| [raw_twobugs.c](raw_twobugs.c) | 78 | 57 | two origins, every reported failure printed with its counterexample and blob |
| [hc_twobugs.c](hc_twobugs.c) | 19 | 5 | two `HEGEL_ASSERT`s: each `file:line` is its own origin |

Measured over 20 seeds (see [README.md](README.md)):

| | default (one failure per run) | `HEGEL_REPORT_MULTIPLE_FAILURES=1` |
|---|---|---|
| lite | B: `lo = -1, hi = 0`; 54 executions | **still one printed** (B), though the engine found both: 137 executions |
| careful | B; 55 executions | A (`lo = -2147483648, hi = -1`) and B; 139 executions |
| hegel-c | B; 54 executions | A and B, each with its own reproduce line; 137 executions |

## Origins are the bug identity

The engine shrinks each origin as a separate bug. With one origin for both checks, a run that met A and B would treat them as one bug and shrink toward whichever is smaller, so A could never be reported while B exists. So even lite needs two origin strings.

They must outlive `prop`, too: the engine reads the origin at `hegel_mark_complete`. The first version of `lite_twobugs.c` used a compound literal inside `prop`. gcc caught the dangling pointer (`-Wdangling-pointer`); without the warning, it would have been a silent use of dead stack memory.

## The pitfall: one printout for several failures

By default, all three report B only: the engine stops at the first failure and keeps the smallest one. With multiple failures on, the engine finds both for lite too (137 executions, like hegel-c). But lite prints "the last stamped failing case", and that is one of them. Printing all of them means keeping the last capture *per origin* and walking `hegel_run_result_failure`. Careful does that (via blob replay); hegel-c's runner does it by capture.

## What hegel-c adds (19 lines)

Distinct asserts are distinct bugs for free: the `file:line` origin never dangles, and the report is numbered ("failure 1 of 2") with a reproduce line each. Multiple failures are opt-in in hegel-c (`.report_multiple = 1` or the environment variable), as in the engine.
