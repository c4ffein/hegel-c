<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# libhegel vs hegel-c, side by side

The same property written three times, against the same function under test
([`sut.h`](../experiments/libhegel-vs-hegel-c/sut.h)) and the same engine file:

- **lite libhegel**: the shortest program on libhegel's C ABI (`hegel.h`) that
  does the job correctly (finds the bug, shrinks it, prints the minimal
  counterexample once, says why a run errored), with no defensive extras;
- **careful libhegel**: upstream `libhegel/examples/` style, every call
  checked, everything freed, origin and blob printed;
- **hegel-c** (`hegel_c.h`).

Each pair's write-up (the `.md` column) explains what each file does and
doesn't do. Lines are non-blank, non-comment. Measured on libhegel 0.50.0,
20 seeds, 2026-11-08; method and full output in
[`experiments/libhegel-vs-hegel-c/README.md`](../experiments/libhegel-vs-hegel-c/README.md)
(`make -C experiments/libhegel-vs-hegel-c run` reruns it).

| pair | lite libhegel | lines | careful libhegel | lines | hegel-c | lines | compared in | the difference that matters |
|---|---|---:|---|---:|---|---:|---|---|
| minimal | [lite_minimal.c](../experiments/libhegel-vs-hegel-c/lite_minimal.c) | 43 | [raw_minimal.c](../experiments/libhegel-vs-hegel-c/raw_minimal.c) | 71 | [hc_minimal.c](../experiments/libhegel-vs-hegel-c/hc_minimal.c) | 14 | [minimal.md](../experiments/libhegel-vs-hegel-c/minimal.md) | all shrink to n = 5; lite must print only the *last* stamped failing case, or it prints the first-check replays too |
| crash | [lite_crash.c](../experiments/libhegel-vs-hegel-c/lite_crash.c) | 49 | [raw_crash_fork.c](../experiments/libhegel-vs-hegel-c/raw_crash_fork.c) | 86 | [hc_crash.c](../experiments/libhegel-vs-hegel-c/hc_crash.c) | 14 | [crash.md](../experiments/libhegel-vs-hegel-c/crash.md) | without its own fork a libhegel test dies on the first SIGSEGV ([raw_crash.c](../experiments/libhegel-vs-hegel-c/raw_crash.c)); with one, lite matches `HEGEL_ISOLATED`'s speed |
| graph | [lite_graph.c](../experiments/libhegel-vs-hegel-c/lite_graph.c) | 66 | [raw_graph.c](../experiments/libhegel-vs-hegel-c/raw_graph.c) | 106 | [hc_graph.c](../experiments/libhegel-vs-hegel-c/hc_graph.c) | 38 | [graph.md](../experiments/libhegel-vs-hegel-c/graph.md) | every engine call in the generator can end the case, so even lite checks them all; careful frees on 2 paths |
| tree | [lite_tree.c](../experiments/libhegel-vs-hegel-c/lite_tree.c) | 80 | [raw_tree.c](../experiments/libhegel-vs-hegel-c/raw_tree.c) | 136 | [hc_tree.c](../experiments/libhegel-vs-hegel-c/hc_tree.c) | 35 | [tree.md](../experiments/libhegel-vs-hegel-c/tree.md) | the recursion retry loop has two RETRY cases that must not be mixed up; `hegel_draw_recursive` runs it |
| two bugs | [lite_twobugs.c](../experiments/libhegel-vs-hegel-c/lite_twobugs.c) | 53 | [raw_twobugs.c](../experiments/libhegel-vs-hegel-c/raw_twobugs.c) | 78 | [hc_twobugs.c](../experiments/libhegel-vs-hegel-c/hc_twobugs.c) | 19 | [twobugs.md](../experiments/libhegel-vs-hegel-c/twobugs.md) | two origins needed even in lite (and they must outlive the case); with multiple failures on, lite prints one of two |
| misuse | [lite_misuse.c](../experiments/libhegel-vs-hegel-c/lite_misuse.c) | 44 | [raw_misuse.c](../experiments/libhegel-vs-hegel-c/raw_misuse.c) | 72 | [hc_misuse.c](../experiments/libhegel-vs-hegel-c/hc_misuse.c) | 15 | [misuse.md](../experiments/libhegel-vs-hegel-c/misuse.md) | `[0, -1]` bounds: both libhegel versions blame LargeInitialTestCase; hegel-c names `min > max` |
| replay | [lite_replay.c](../experiments/libhegel-vs-hegel-c/lite_replay.c) | 48 | [raw_replay.c](../experiments/libhegel-vs-hegel-c/raw_replay.c) | 78 | [hc_minimal.c](../experiments/libhegel-vs-hegel-c/hc_minimal.c) | 14 | [replay.md](../experiments/libhegel-vs-hegel-c/replay.md) | lite can replay a blob but never prints one; hegel-c prints the reproduce line with every failure |

**Same results:** all three versions shrink to the same counterexample on
every pair. Lite runs exactly as many executions as hegel-c on every seed;
careful runs one more (its blob replay). Careful's and hegel-c's blobs are
byte-identical.

**Lines:** hegel-c is 1.7x (graph) to 3.5x (crash) fewer lines than lite, and
2.8x to 5.1x fewer than careful. About 30 lines of each lite file are the same
run loop, and about 54 of each careful one; a project would write that once,
as a harness.

**Cost:** hegel-c fork mode runs 25x (graph) to 43x (tree) the libhegel wall
time (every draw is a pipe round trip); nofork is 1.0x to 1.6x.

**Coverage:** hegel-c binds 111 of libhegel 0.50.0's 157 functions; the other
46 are classified in [`tools/libhegel-coverage.toml`](../tools/libhegel-coverage.toml)
(`make check-libhegel-coverage`, gaps in [`TODO.md`](../TODO.md)).
