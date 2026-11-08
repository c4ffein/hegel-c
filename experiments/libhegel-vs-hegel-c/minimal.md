<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Pair 1, minimal: `n` in [0, 100] must fit a 5-slot buffer

`buffer_fits (n)` is false for n >= 5 ([sut.h](sut.h)). One draw, one check.

| file | code lines | of which boilerplate | what it does |
|---|---:|---:|---|
| [lite_minimal.c](lite_minimal.c) | 43 | 31 | finds and shrinks the bug; prints `FAILED: n = 5`, or the engine's message if the run errors |
| [raw_minimal.c](raw_minimal.c) | 71 | 56 | the same, in upstream's `examples/` style: every call checked (`CHECK` aborts with the engine's message), every handle freed; prints the origin, the counterexample (by replaying the failure's blob) and the blob |
| [hc_minimal.c](hc_minimal.c) | 14 | 5 | a full failure report (draw trace, message, `file:line` origin, reproduce line) with no printing code |

Measured over 20 seeds on libhegel 0.50.0 (see [README.md](README.md)): all three shrink to n = 5 on every seed, after 39 executions (median) for lite and hegel-c, 40 for careful (its extra blob replay). 5 ms for both libhegel versions, 4 ms for hegel-c nofork, 22 ms in fork mode.

## lite vs careful (43 vs 71 lines)

The engine work is the same. Careful adds what a test you rely on needs:

- **Return codes.** Lite checks only the draw. A failing `hegel_settings_new` or `hegel_run_start` goes unnoticed, and the run loop then reads uninitialized handles.
- **The origin.** Careful names the check (`"buffer_fits (n)"`) and prints it. Lite passes `NULL`, which is fine for one check (see [twobugs.md](twobugs.md) for two).
- **The blob.** Careful prints it, so the failure can be replayed later ([replay.md](replay.md)). Lite prints none.
- **Frees.** Careful frees every handle; lite lets the process exit.

## The pitfall lite has to get right

To print the counterexample, lite prints from the cases the engine stamps for capture (`hegel_test_case_should_capture`). The engine stamps more than the final replay: it also stamps the first check of each failure it discovers. Printing every stamped failing case gives this, on seed 1:

```
FAILED: n = 55
FAILED: n = 55
FAILED: n = 55
FAILED: n = 55
FAILED: n = 5
```

Lite keeps the *last* stamped failing case and prints it after the run. Careful avoids the question by replaying the reported blob (one more execution); hegel-c's runner does what lite does, per origin.

## What hegel-c adds (14 lines)

The run loop, the result reading, the capture and the report are in the runner. The test is the property and one `hegel_run_test_n` call. `HEGEL_ASSERT`'s `file:line` is the origin, and the reproduce line is printed with every failure. The cost is in fork mode (22 ms here: every draw is a pipe round trip); nofork is as fast as libhegel.
