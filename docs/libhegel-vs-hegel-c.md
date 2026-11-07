<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# libhegel vs hegel-c, side by side

The same property written twice: on libhegel's C ABI alone (`hegel.h`) and
on hegel-c (`hegel_c.h`). Same bounds, same function under test
([`sut.h`](../experiments/libhegel-vs-hegel-c/sut.h)), same engine file.
Lines are non-blank, non-comment. Measured on libhegel 0.50.0, 20 seeds;
method, full output and conclusions in
[`experiments/libhegel-vs-hegel-c/README.md`](../experiments/libhegel-vs-hegel-c/README.md)
(`make -C experiments/libhegel-vs-hegel-c run` reruns it).

| pair | libhegel | lines | hegel-c | lines | what differs |
|---|---|---:|---|---:|---|
| minimal | [raw_minimal.c](../experiments/libhegel-vs-hegel-c/raw_minimal.c) | 71 | [hc_minimal.c](../experiments/libhegel-vs-hegel-c/hc_minimal.c) | 14 | same counterexample (n = 5) and blob; raw prints nothing unless it replays the blob itself |
| crash | [raw_crash.c](../experiments/libhegel-vs-hegel-c/raw_crash.c) | 71 | [hc_crash.c](../experiments/libhegel-vs-hegel-c/hc_crash.c) | 14 | raw dies on the first SIGSEGV (exit 139, no input, no blob); hegel-c fork shrinks it to id = 42 |
| crash, isolated | [raw_crash_fork.c](../experiments/libhegel-vs-hegel-c/raw_crash_fork.c) | 86 | [hc_crash_iso.c](../experiments/libhegel-vs-hegel-c/hc_crash_iso.c) | 16 | hand-rolled fork vs nofork + `HEGEL_ISOLATED`: same result, same cost; only hegel-c's child can send asserts and notes |
| graph | [raw_graph.c](../experiments/libhegel-vs-hegel-c/raw_graph.c) | 106 | [hc_graph.c](../experiments/libhegel-vs-hegel-c/hc_graph.c) | 38 | collection protocol + spans; byte-identical blobs; raw frees on 2 exit paths, hegel-c uses `hegel_alloc` |
| tree | [raw_tree.c](../experiments/libhegel-vs-hegel-c/raw_tree.c) | 136 | [hc_tree.c](../experiments/libhegel-vs-hegel-c/hc_tree.c) | 35 | recursion protocol by hand (arena, retry loop) vs `hegel_draw_recursive`; same choices, same blob |
| two bugs | [raw_twobugs.c](../experiments/libhegel-vs-hegel-c/raw_twobugs.c) | 78 | [hc_twobugs.c](../experiments/libhegel-vs-hegel-c/hc_twobugs.c) | 19 | both report one bug by default, both with `HEGEL_REPORT_MULTIPLE_FAILURES=1` |
| misuse | [raw_misuse.c](../experiments/libhegel-vs-hegel-c/raw_misuse.c) | 72 | [hc_misuse.c](../experiments/libhegel-vs-hegel-c/hc_misuse.c) | 15 | `[0, -1]` bounds: raw (examples' pattern) blames LargeInitialTestCase; hegel-c names `min > max` |
| replay | [raw_replay.c](../experiments/libhegel-vs-hegel-c/raw_replay.c) | 78 | [hc_minimal.c](../experiments/libhegel-vs-hegel-c/hc_minimal.c) | 14 | raw: +7 lines for `hegel_run_start_blob`; hegel-c: `HEGEL_C_REPRODUCE` on every report, no code |

Shared by the raw side: [`raw_check.h`](../experiments/libhegel-vs-hegel-c/raw_check.h)
(18 lines, `CHECK` / `STR`). Of each raw file, ~54 lines are the same `main`
(run loop, result reading, blob replay for display); a project would write it
once as a harness.

Cost: hegel-c fork mode runs 24x (graph) to 41x (tree) the raw wall time
(every draw is a pipe round trip); nofork is 1.1–1.2x.

Coverage: hegel-c binds 69 of libhegel 0.50.0's 157 functions; the other 88
are classified in [`tools/libhegel-coverage.toml`](../tools/libhegel-coverage.toml)
(`make check-libhegel-coverage`, gaps in [`TODO.md`](../TODO.md)).
