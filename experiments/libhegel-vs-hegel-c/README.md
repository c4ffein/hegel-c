<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# libhegel directly vs hegel-c

What does hegel-c add over writing tests directly against libhegel's C ABI
(`hegel.h`)? Each property here is written twice, `raw_<pair>.c` on
`hegel.h` alone and `hc_<pair>.c` on `hegel_c.h`, with the same bounds,
the same 100-case budget, the same function under test (`sut.h`, shared
and not counted) and **the same engine file** for both sides.

```
make -C experiments/libhegel-vs-hegel-c run LIBHEGEL_SO=/path/to/libhegel.so \
     [HEGEL_C_ROOT=<hegel-c tree>] [SEEDS="1 2 3"]
```

`run` builds everything, runs every program once per seed (`HEGEL_SEED`),
prints the table below, replays a blob each way and runs a leak check;
`make loc` gives the line counts only. Not part of `make test`.

## Setup of the measured run (2026-11-06)

- Engine: libhegel 0.50.0, `libhegel-linux-amd64.so`, sha256
  `e290467d…fce56298` (matches the release's `.sha256`). raw_* link it
  directly (`-Wl,-rpath`); hc_* load it through
  `HEGEL_LIBHEGEL_PATH`.
- hegel-c: the main checkout's working tree as of today, built into a
  scratch copy and passed as `HEGEL_C_ROOT`. The worktree's commit
  (11b87e3) has no `hegel_alloc` / `hegel_draw_recursive` yet, so
  `hc_graph.c` and `hc_tree.c` don't build against it; the default
  `HEGEL_C_ROOT` (this repo) works once those land.
- `include/hegel.h`: libhegel 0.50.0's header copied unchanged (MIT,
  Antithesis, LLC; `include/hegel.h.license` carries the REUSE notice).
- gcc 14.2, -O2, 8 cores, Debian 13. 20 seeds (1..20) for every program.
- Stateful testing is not compared: hegel-c's stateful driver crashes on
  0.50.0 until the port in progress lands.

How things are measured:

- **execs**: executions of the property body, counted by `count_exec()`
  (`common.h`) appending one byte to a file, so executions inside fork
  children are counted too.
- **ms**: wall time of the whole process, median over the seeds.
- **lines**: non-blank lines once comments are removed (`loc.sh`);
  **boilerplate**: the lines between `/* B{ */` and `/* }B */` markers
  (run loop, settings, result reading, error-code mapping, frees,
  hand-written fork). Lines that mix a draw with its error check
  (`if ((rc = hegel_generate_integer (…)) != HEGEL_OK) return rc;`) are
  counted as property code, not boilerplate, which flatters the raw side.
  **chars**: non-whitespace characters outside comments.

How the raw side is written: as `libhegel/examples/failing.c` and
`spans_collections.c` do it (a `CHECK` macro for calls that can only fail
by misuse, any non-OK draw ends the case as OVERRUN), plus what a user
needs on failure that the examples don't print: the counterexample's
values, obtained by replaying the failure's blob with
`hegel_test_case_from_blob` and a `show` flag (the route `hegel.h`
describes), and the blob. Each raw file is self-contained, like the
examples; the ~54-line `main` is the same in all of them (see "Lines").

## Pairs

| # | property | function under test (bug) |
|---|----------|---------------------------|
| 1 minimal | n in [0, 100] fits a 5-slot buffer | `buffer_fits`: false for n >= 5 |
| 2 crash | `name_len (id) > 0`, id in [0, 100] | `name_of` returns NULL for id >= 42, `name_len` dereferences it (SIGSEGV) |
| 3 graph | nvert in [1, 20], edges via the collection protocol (max 40), endpoints < nvert, span per graph and per edge; `adj_build` stores 2 entries per edge | 3-slot adjacency rows silently drop the 4th neighbour |
| 4 tree | `+`/`-` tree, leaves 0..9, depth 50 / leaves 100, recursion protocol; `eval (simplify (e)) == eval (e)` | `simplify` rewrites `0 - b` to `b` |
| 5 twobugs | `midpoint (lo, hi)` over the full int range: A in [lo, hi]; B equals floor((lo+hi)/2) | A: `lo + hi` wraps; B: C division rounds toward zero |
| 6 misuse | len in [0, 10], then idx in [0, len - 1] | a bug in the **test**: len = 0 asks for [0, -1] |
| 7 replay | pair 1's blob replayed, then a blob that no longer fails | — |

Variants: `raw_crash_fork.c` (raw with hand-rolled isolation: draw in the
engine's process, fork for the code under test, signal = INTERESTING),
`hc_crash_iso.c` (nofork + `HEGEL_ISOLATED`), `hcn_*` (the hc_*.c
sources built with `-Dhegel_run_test_n=hegel_run_test_nofork_n`, i.e.
hegel-c without per-case fork), `raw_replay.c` (raw_minimal + a `REPRO`
env var for `hegel_run_start_blob`).

## Results (20 seeds)

```
pair       side                       env    exits   execs med  [range]     ms    rep  counterexample (same on all 20 seeds)
1 minimal  libhegel                          20x1    40         [39-43]     4     1    n = 5
1 minimal  hegel-c fork                      20x1    39         [38-42]     22    1    Draw 1: 5
1 minimal  hegel-c nofork                    20x1    39         [38-42]     4     1    Draw 1: 5
2 crash    libhegel                          20x139  2          [2-5]       3     0    (process killed by SIGSEGV, no output)
2 crash    libhegel + own fork               20x1    59         [50-64]     18    1    id = 42
2 crash    hegel-c fork                      20x1    58         [49-63]     30    1    Draw 1: 42
2 crash    hegel-c nofork                    20x139  2          [2-5]       3     0    (process killed by SIGSEGV, no output)
2 crash    hegel-c nofork+ISOLATED           20x1    58         [49-63]     18    1    Draw 1: 42
3 graph    libhegel                          20x1    2389       [2349-2491] 76    1    nvert=1 edges=[(0,0) (0,0)]
3 graph    hegel-c fork                      20x1    2388       [2348-2490] 1806  1    nvert=1 edges=[(0,0) (0,0)]
3 graph    hegel-c nofork                    20x1    2388       [2348-2490] 85    1    nvert=1 edges=[(0,0) (0,0)]
4 tree     libhegel                          20x1    1993       [1770-2528] 89    1    (0 - 1)
4 tree     hegel-c fork                      20x1    1992       [1769-2527] 3623  1    (0 - 1)
4 tree     hegel-c nofork                    20x1    1992       [1769-2527] 109   1    (0 - 1)
5 twobugs  libhegel                          20x1    55         [49-114]    5     1    B: lo = -1, hi = 0
5 twobugs  hegel-c fork                      20x1    54         [48-113]    28    1    B: lo = -1, hi = 0
5 twobugs  libhegel                   multi  20x1    139        [121-609]   8     2    A: lo = INT_MIN, hi = -1 (18/20; 2/20: lo = 1, hi = INT_MAX) + B
5 twobugs  hegel-c fork               multi  20x1    54         [48-113]    29    1    B only (env ignored)
6 misuse   libhegel                          20x1    1          [1-1]       2     0    run error: FailedHealthCheck: LargeInitialTestCase …
6 misuse   hegel-c fork                      20x1    1          [1-1]       3     0    Health check failure: hegel-c: integer draw with min > max
```

`multi` = `HEGEL_REPORT_MULTIPLE_FAILURES=1`. `rep` = failures reported.

Per execution (ms / execs, medians): graph 0.032 raw, 0.036 hegel-c
nofork, 0.76 hegel-c fork; tree 0.045 raw, 0.055 nofork, 1.82 fork.

**Exploration is identical.** On every one of the 20 seeds of pairs 2-5,
the raw program ran exactly one body execution more than hegel-c (its
blob replay for display; hegel-c prints from the engine's own stamped
final replay), and hegel-c fork and nofork ran the same count. The
reproduce blobs are byte-identical between the two sides (pair 3:
`AXic42AAAi5GIMHIyAhhQPkMGHwGAActAEY=`, pair 4:
`AXicY2dgYGBkYGRk4GIEsYAEmMEAYzACAAN1ADA=` on all seeds), including
pair 4, where the raw side runs the recursion protocol by hand and
hegel-c runs `hegel_draw_recursive`. hegel-c changes nothing about what
is generated or how it shrinks; every difference below is in the code
the user writes, what the user sees, and what it costs.

Leak check (`make leaks`, ASan + LSan, nofork for hegel-c): none of
raw_graph, raw_tree, hcn_graph, hcn_tree leaks — the raw side only after
care (below).

## Lines

```
file              lines  boilerplate  property  chars
raw_minimal.c        71           56        15   1990
hc_minimal.c         14            5         9    228
raw_crash.c          71           56        15   1990    (dies, see pair 2)
raw_crash_fork.c     86           68        18   2291
hc_crash.c           14            5         9    232
hc_crash_iso.c       16            5        11    259
raw_graph.c         106           61        45   3183
hc_graph.c           38            5        33    920
raw_tree.c          136           74        62   3795
hc_tree.c            35            5        30    703
raw_twobugs.c        78           57        21   2264
hc_twobugs.c         19            5        14    422
raw_misuse.c         72           57        15   2060
hc_misuse.c          15            5        10    261
raw_replay.c         78           63        15   2254    (+7 lines over raw_minimal)
raw_check.h          18            0        18    440    (CHECK / STR, shared by raw_*)
```

Of each raw file's boilerplate, ~54 lines are the same `main` (context,
settings, run loop, `mark_complete`, result and failure reading, blob
replay for display, five frees). A user with several tests would move it
into a harness once (≈ 60 lines, with a property callback and an origin
out-parameter, as here); per test that leaves the "property" column plus
the error-code plumbing woven into the generators: minimal 15 vs 9
property lines, graph 45 vs 33, tree 62 vs 30 (whole files: 3.5x the
characters for graph, 5.4x for tree).

## What the user sees (seed 1, trimmed)

Pair 1:

```
libhegel                                  hegel-c
FAILED: buffer_fits (n)                   Draw 1: 5
n = 5                                     Property test failed: n = 5 does not fit
blob: AAEAAAAACgEAAAAF
                                          === hegel-c: property test FAILED ===
                                          origin: HEGEL_ASSERT at hc_minimal.c:15
                                          message: n = 5 does not fit
                                          reproduce: HEGEL_C_REPRODUCE='1:AAEAAAAACgEAAAAF'
```

With no printing code of its own, the raw program reports nothing: the
engine prints nothing at the default verbosity, and the result carries
only origin, blob and caveat. The `n = 5` line exists because the raw
test replays the blob with a `show` flag (~10 lines in `main`).

Pair 2 (crash):

```
libhegel (natural)             libhegel + own fork        hegel-c fork / nofork+ISOLATED
Segmentation fault  (shell)    FAILED: crash: Segmentation fault   Draw 1: 42
exit 139, nothing else         id = 42                    Property test failed: test case crashed with
                               crash: Segmentation fault    signal 11 (Segmentation fault)
                               blob: AAEAAAAACgEAAAAq     origin: crash: signal 11 (Segmentation fault)
                                                          reproduce: HEGEL_C_REPRODUCE='1:AAEAAAAACgEAAAAq'
                                                          [ISOLATED: origin: HEGEL_ISOLATED at hc_crash_iso.c:17]
```

The natural raw program dies on the 2nd-5th case: no shrink, no input
shown, no blob. hegel-c nofork without `HEGEL_ISOLATED` behaves exactly
the same. With 15 lines of fork/waitpid (raw_crash_fork.c) the raw side
gets the same minimal input (42), the same blob, and the same execution
count; what it doesn't get is a channel from the child (an assert's
message, notes printed in the child, assume) — hegel-c's ISOLATED child
has one.

Pair 3 (graph):

```
libhegel                                        hegel-c
FAILED: adj_build stores 2 entries per edge     Draw 1: 1
graph: nvert=1 nedge=2 edges=[(0,0) (0,0)]      Draw 2: 0  (… Draw 5: 0)
stored 3 of 4 adjacency entries                 graph: nvert=1 nedge=2 edges=[(0,0) (0,0)]
blob: AXic42AAAi5GIMHIyAhhQPkMGHwGAActAEY=      Property test failed: stored 3 of 4 adjacency entries
                                                origin: HEGEL_ASSERT at hc_graph.c:43
                                                reproduce: HEGEL_C_REPRODUCE='1:AXic42AAAi5…'
```

For a structured value hegel-c's `Draw N:` trace (raw draws, no
collection decisions) is not readable on its own; both sides need a
printer. hegel-c's `hegel_note` takes a string, so the test formats into a
buffer (`fmemopen`, 5 lines in hc_graph.c and hc_tree.c).

Pair 5 (two bugs), with `HEGEL_REPORT_MULTIPLE_FAILURES=1`:

```
libhegel                                            hegel-c
FAILED: midpoint in [lo, hi]                        Draw 1: 0
lo = -2147483648, hi = -1, midpoint = 1073741823    Draw 2: -1
blob: AAIAAAAACgEAAAD/AAoEAAAAAAAAgA==              Property test failed: lo = -1, hi = 0, midpoint = 0
FAILED: midpoint is floor ((lo + hi) / 2)           origin: HEGEL_ASSERT at hc_twobugs.c:21
lo = -1, hi = 0, midpoint = 0
blob: AXicY2IAAi5GBjj1HwACBAEY
```

At the engine default both sides report one bug, B (the smaller one), on
all 20 seeds; the overflow bug A never shows until B is fixed. libhegel
reports both when asked (setting or env var). hegel-c, when this was
measured, set `report_multiple_failures` off after reading the environment
and had no option for it, so it could not report A. Fixed on 2026-11-06:
`hegel_run_options.report_multiple` or `HEGEL_REPORT_MULTIPLE_FAILURES=1`
now reports both, each shrunk (to 501 and -501 in
`tests/selftest/test_report_multiple.c`) with its own reproduce line.

Pair 6 (misuse in the test):

```
libhegel   run error: FailedHealthCheck: LargeInitialTestCase — the smallest natural input for this
           test is very large, … suppress the LargeInitialTestCase health check …
hegel-c    Health check failure: hegel-c: integer draw with min > max
```

With the examples' "any non-OK draw is an overrun" pattern, the raw test
turns `HEGEL_E_INVALID_ARG` into an overrun and the engine then blames
the input size — a misleading diagnosis. The engine's own diagnostic is
better than either (`generate_integer requires min_value <= max_value,
got [0, -1]`, measured by printing `hegel_context_last_error` in a
scratch variant), and a raw user who prints it gets it. hegel-c names the
cause but drops the bounds and gives no location.

Pair 7 (replay): both replay the n = 5 blob to the same failure and both
say a passing blob "did not reproduce" (exit 0). Raw needs 7 more lines
(`hegel_run_start_blob`, plus re-using the given blob for display since
a replayed failure carries none); hegel-c's line is printed with every
failure and needs no code. hegel-c's `N:` prefix also selects one run in
a multi-run binary.

## Conclusion

Where hegel-c adds value (measured here):

- **Crashes.** The natural libhegel test dies with the first crashing
  input (exit 139, 2-5 cases, no input, no blob). hegel-c fork mode or
  `HEGEL_ISOLATED` turns it into a shrunk failure (id = 42) with a
  reproduce line, at 0 extra lines. A raw user can get the same result
  with ~15 lines of fork/waitpid, but has no channel for asserts, notes
  or assume from inside the child.
- **Code volume.** As written, the raw file is 2.8x (graph) to 5.1x
  (minimal) the lines and 3.5x to 8.7x the characters. After factoring
  the raw run loop into a harness, the raw property code is still 1.4x
  (graph) to 2.1x (tree) the lines; the difference is error-code
  propagation and memory release.
- **Recursion.** The raw generator takes 55 lines (arena, node function,
  retry loop) against hegel-c's 13-line node function, and it is easy
  to get wrong. My first raw draft called `hegel_recursion_retry` after
  a mispriced `recursion_finish` (`hegel.h` forbids that) and leaked the
  nodes `simplify` unlinks. I caught both by rereading `hegel.h`; no
  tool or test flagged them. Both versions draw exactly the same choices.
- **Memory.** `hegel_alloc` / `hegel_realloc` remove every free path
  (graph: the raw side frees on 2 exit paths; tree: an arena). Both
  sides are leak-free under LSan; the raw side only by construction.
- **Reports.** A failure report (input trace, message, file:line origin,
  reproduce line) with no printing code; the raw side prints nothing
  unless the user writes the blob replay.
- **Distinct origins** come for free from `HEGEL_ASSERT`'s file:line; the
  raw side has to name an origin per check (here: 2 string literals).

Where it adds little:

- **Generation and shrinking**: identical. Same choices, same
  counterexamples, same blobs, same execution counts (±1) on all 20
  seeds; hegel-c's spans/collections/recursion calls are thin wrappers.
- **Simple properties** without crashes: the difference is boilerplate
  that a ~60-line harness absorbs once.
- **Structured counterexamples**: both sides need a printer; hegel-c's
  string-only `hegel_note` costs a `fmemopen` dance.

What hegel-c costs or hides:

- **Fork mode time**: 24x (graph) to 41x (tree) the raw wall time,
  0.76 / 1.82 ms per case against 0.03 / 0.05 ms; the per-case cost
  grows with the number of engine calls (every draw is a pipe round
  trip). Nofork costs 1.1-1.2x raw — and loses crash isolation unless
  the code under test is in `HEGEL_ISOLATED`, which costs about what a
  hand-rolled fork does (18 ms vs 18 ms on pair 2).
- **Multiple failures**: forced off, no option (pair 5: bug A is
  invisible). libhegel reports both on request. *Fixed since:*
  `report_multiple` / `HEGEL_REPORT_MULTIPLE_FAILURES=1` (opt-in; the
  default is still one failure per run).
- **Engine output**: verbosity forced to QUIET (`HEGEL_VERBOSITY=verbose`
  shows the engine's phase/case trace for raw, nothing for hegel-c);
  `HEGEL_STATISTICS` is likewise inert, and `hegel_event` isn't exposed.
- **Diagnostics**: misuse messages drop the engine's details (pair 6).
- **ABI coverage**: hegel-c binds 69 of libhegel 0.50.0's 157 functions.
  Not reachable from hegel-c: date/time/datetime/uuid/ipv4/ipv6/email/
  url/domain and grammar generators, `hegel_repeat_*`, test-case clones
  (multi-threaded generation), events/statistics, the pretty printer,
  `stop_on_first_failure` / `derandomize` / profiles as code (the env
  vars and `hegel.toml` still apply).
- **Fork mode semantics**: state the body changes in the process doesn't
  survive the case; counting executions here needed an `O_APPEND` file
  for that reason.
