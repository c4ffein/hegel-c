<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Shrinking (the reducer)

When a property test fails, the input that caused the failure is
usually a random mess — hundreds of bytes of generated data, most
of which is irrelevant to the bug. The interesting question isn't
*"what mess broke it?"* but *"what's the smallest version of the
mess that still breaks it?"*

That smaller version is called the **minimal counterexample**, and
producing it is called **shrinking** (or **reducing**). It's the
single most valuable feature of property-based testing — without
it, PBT failures are hard to debug; with it, you typically end up
staring at a tiny obviously-wrong input that points straight at the
bug.

This doc explains how shrinking works in hegel-c, walks through a
real worked example (a Scotch bug shrunk from a 14-vertex graph
down to 3 vertices), and gives the rules for writing tests that
shrink well.

## What integrated shrinking is, and why it matters

There are two schools of shrinking:

- **Type-based shrinking** (the original QuickCheck approach):
  every type knows how to make itself smaller. `int` shrinks
  toward 0, `list` shrinks by removing elements, struct shrinks
  by shrinking its fields. Simple to understand, but it falls
  apart on combinators (`map`, `filter`, `flat_map`) — once you
  transform a value, the type-shrinker doesn't know how to invert
  the transform, so shrink quality degrades.

- **Integrated shrinking** (Hypothesis, and therefore Hegel):
  the framework controls a finite **byte stream** that all
  generators consume from. Shrinking operates on that byte stream
  — making it shorter or lexicographically smaller — and then
  *replays* the entire generation. Every generator, every
  combinator, every nested draw consumes from the same stream, so
  shrinking the stream shrinks every part of the generated value
  *coherently*. `map`, `filter`, `flat_map` don't degrade
  anything because they're just consumers of the same bytes.

hegel-c gets integrated shrinking for free from the underlying
`hegeltest` engine. The schema API (`hegel_gen.h`) is built on
primitive draws (`hegel_draw_int` etc.) which all read from the
shared byte stream, so a `HEGEL_ARRAY_INLINE` of structs with
nested `HEGEL_INT` fields shrinks coherently — the array length
shrinks, individual element values shrink, and they shrink
together rather than as independent dimensions.

The practical implication: **you don't write any shrink logic.**
You write the property, hegel-c does the rest.

## When you'll see shrinking happen

Shrinking only happens on **failure**. A passing run (every case
satisfies the property) produces no shrink output — there's
nothing to minimize. When a case fails, hegeltest:

1. Records the failing input as the "current best"
2. Tries variations of the byte stream that produce smaller
   inputs (shorter byte sequences, lower numeric values)
3. For each variation, replays the entire test
4. If the variation still fails, it becomes the new current best
5. Repeats until no smaller variation fails
6. Runs one **final replay** with the minimal counterexample,
   with `is_last_run` set so diagnostic output (notes) appears

The final replay is the only one whose output you should care
about. Anything printed during generation or shrinking iteration
is suppressed — only the final replay shows you the minimal case.
This is what `hegel_note(tc, msg)` is for: it prints `msg` only
during the final replay.

## A worked example: finding a real Scotch bug

The Scotch graph-partitioning library has a bug in
`hgraphOrderCp` (an off-by-`ordenum` error in the compression
expansion loop). It causes `SCOTCH_graphOrder` to silently
produce an invalid permutation when the strategy uses
`SCOTCH_STRATDISCONNECTED` and the graph has multiple connected
components with at least one non-first component undergoing
vertex compression. Full bug report:
[c4ffein/scotch BUG_REPORT.md](https://github.com/c4ffein/scotch/blob/ff403d445b36ee1723ff53d2478d368b21a3341f/REPORTS/BUG_REPORT.md)

The bug report includes a hand-crafted reproducer with **14
vertices and 4 edges**. We're going to write a hegel-c test that
re-discovers this bug *from scratch* and lets the shrinker reduce
it to the smallest possible failing graph.

The test lives at
[`tests/irl/scotch/test_graph_order_shrink.c`](../tests/irl/scotch/test_graph_order_shrink.c).

### Step 1: write a logical input type

<!-- /include tests/irl/scotch/test_graph_order_shrink.c:65-74 -->
```c
typedef struct EdgePair {
  int                 u;
  int                 v;
} EdgePair;

typedef struct Graph {
  int                 nvert;
  EdgePair *          edges;
  int                 nedges;
} Graph;
```
<!-- /endinclude -->

A graph is a vertex count plus a list of undirected edges. Note
that this is the **logical** input — how we want hegel to think
about graphs — not the CSR representation Scotch wants.

### Step 2: declare the schema

where `MAX_VERT = 20` and `MAX_EDGES = 30`:

<!-- /include tests/irl/scotch/test_graph_order_shrink.c:85-91 -->
```c
  hegel_schema_t edge_schema = HEGEL_STRUCT (EdgePair,
      HEGEL_INT (0, MAX_VERT - 1),
      HEGEL_INT (0, MAX_VERT - 1));

  graph_schema = HEGEL_STRUCT (Graph,
      HEGEL_INT (3, MAX_VERT),
      HEGEL_ARRAY_INLINE (edge_schema, sizeof (EdgePair), 0, MAX_EDGES));
```
<!-- /endinclude -->

The schema generates graphs with 3–20 vertices and 0–30 edges,
with edge endpoints drawn in `[0, nvert)`.  Ranges deliberately
small so that shrunken counterexamples are easy to read.

### Step 3: write a property

The property is: **`SCOTCH_graphOrder` must produce a valid
permutation of `[0, nvert)`** — every value in range, no
duplicates, all positions written.

<!-- /ignore worked-example: Scotch-reducer narrative step with inline prose placeholders (...) and (/* out of range, fail */) -->
```c
SCOTCH_graphInit (&grafdat);
SCOTCH_graphBuild (&grafdat, ...);
SCOTCH_graphCheck (&grafdat);
SCOTCH_stratInit (&stradat);
SCOTCH_stratGraphOrderBuild (&stradat, SCOTCH_STRATDISCONNECTED, 3, 0.2);

SCOTCH_Num *permtab = calloc (nvert, sizeof (SCOTCH_Num));
memset (permtab, 0xBB, nvert * sizeof (SCOTCH_Num));  /* sentinel */

SCOTCH_graphOrder (&grafdat, &stradat, permtab, NULL, &cblknbr, NULL, NULL);

/* Validate: every entry in [0, nvert), no duplicates */
for (int i = 0; i < nvert; i++) {
  if (permtab[i] < 0 || permtab[i] >= nvert) /* out of range, fail */;
  if (seen[permtab[i]]) /* duplicate, fail */;
  seen[permtab[i]] = 1;
}
```

The 0xBB memset is important: any position Scotch fails to
write stays as `0xBBBBBBBB`, which is detectably out of range.

### Step 4: annotate the test case for the final replay

This is the bit that makes the demo useful:

<!-- /ignore worked-example: Scotch-reducer narrative step with inline prose placeholder (/* ... append ... */) -->
```c
char note_buf[512];
snprintf (note_buf, sizeof (note_buf),
          "MINIMAL nvert=%d nedges=%d edges=[", nvert, g->nedges);
/* ... append each edge as "(u,v)" ... */
hegel_note (tc, note_buf);
```

`hegel_note` is no-op during normal generation and shrinking. It
**only** prints during the final replay (after shrinking lands on
the minimal counterexample). So at the end of the test run, we
get exactly one `MINIMAL ...` line in stderr describing the
shrunken case.

This works even if the failure manifests as a SIGSEGV instead of
an assertion. (Scotch's bug is non-deterministic in *which* way
it fails — sometimes the uninitialized memory looks like a valid
int, sometimes it crashes Scotch internals — but the shrunken
input shape is reliable.)

### Step 5: run the test

```bash
$ make test-local
==== Scotch reducer-quality tests (must shrink to small counterexample) ====
  test_graph_order_shrink            OK (shrunk to MINIMAL nvert=4 nedges=1 edges=[(2,3)])
```

That's the whole story. hegel-c started from the schema's full
range (3–20 vertices, 0–30 edges), found a failing case within a
handful of tries (~25% of small random graphs trigger the bug),
shrunk the failing byte stream until no smaller variant still
failed, and reported a 4-vertex / 1-edge graph as the minimum.

The theoretical minimum for this bug is **3 vertices, 1 edge** (one
isolated vertex + one K₂ pair, where the K₂ ordens at `ordenum>0`).
We hit that on roughly 2 of every 5 runs; the other runs land at
`nvert=4, nedges=1` or `nvert=3, nedges=2`, all within one step of
the theoretical minimum. That variance is normal — integrated
shrinking is greedy on the byte stream, not on the structural
size, so it doesn't always discover the absolute structural
minimum.

### Step 6: assert in CI

The Makefile parses the `MINIMAL nvert=N` from stderr and asserts
`N <= 5`. A regression that breaks shrinking quality (e.g., the
shrinker stops too early) would land at `nvert=10` or `nvert=15`
and fail the assertion. The CI line looks like:

```make
nvert=$$(echo "$$minimal" | sed -n 's/MINIMAL nvert=\([0-9]*\).*/\1/p')
if [ "$$nvert" -gt 5 ]; then
  echo "FAIL — shrunk to nvert=$$nvert (expected <= 5)"
fi
```

Loose enough to absorb shrinker variance, tight enough to catch
real degradation.

## Rules for writing tests that shrink well

After building this test (and several others), the rules that
matter are:

### 1. Make the schema small at the boundary, not the body

Bound the schema's value ranges as tightly as the property allows.
`HEGEL_INT(0, 1000000)` shrinks fine, but the *minimum* it
can shrink to is `0`, `1`, etc., based on byte-stream simplicity
order. If your property only cares about whether `x` is even, use
`HEGEL_INT(0, 1)` and post-multiply — the minimum you'll see
in the shrunken case is much closer to what's interesting.

For our Scotch test: `nvert ∈ [3, 20]` instead of `[3, 1000]`.
The shrinker can reach `3` either way, but the upper bound bounds
the cost of the early *generation* phase.

### 2. Use `hegel_note()` for crash-prone inputs

If your property might trigger a SIGSEGV (or any other panic that
hegel-c reports as `crashed (signal N)`), the shrunken trace
won't include your `HEGEL_ASSERT` message — the test never reached
the assert. `hegel_note()` is the workaround: call it *before*
the dangerous operation, with a description of the input shape.
It only prints on the final replay, so generation-time output is
clean.

<!-- /ignore illustration: crash-prone test pattern, generic placeholder names -->
```c
hegel_note (tc, "input shape: ...");
some_function_that_may_crash (input);
HEGEL_ASSERT (post_condition, ...);  /* may not be reached */
```

If the property crashes, the final replay still calls `hegel_note`
once before crashing, so the input shape lands in stderr.

### 3. Don't bury the input behind opaque transformations

If your test does:

<!-- /ignore counter-example: uses libc rand() outside hegel's byte stream, breaks shrinking -->
```c
int n = hegel_draw_int (tc, 1, 100);
char *s = malloc (n);
randomize_with_libc_rand (s, n);
HEGEL_ASSERT (parse (s) == n, "...");
```

…then hegel can shrink `n`, but the *contents* of `s` come from
`rand()`, not from the byte stream. Shrinking `n` will produce
different `rand()` outputs, so the same logical input no longer
exists at smaller sizes. Shrink quality degrades to "smaller `n`
maybe, eventually."

The fix is to draw all randomness through hegel:

<!-- /ignore illustration: fixed version of the counter-example above -->
```c
char *s = malloc (n);
for (int i = 0; i < n; i++) s[i] = hegel_draw_int (tc, 0, 255);
HEGEL_ASSERT (parse (s) == n, "...");
```

Now both `n` *and* the bytes shrink coherently.

The schema API does this for you automatically — `HEGEL_ARR_OF`
and `HEGEL_ARRAY_INLINE` draw each element through the schema,
which in turn uses primitive draws. No extra discipline needed if
you stick to the schema layer.

### 4. Make the assertion message reproducer-friendly

The shrunken counterexample is the entry point into your debugger.
Print enough information in the assertion message (or `hegel_note`)
to **paste into a unit test** without further investigation. For
the Scotch bug, that's `nvert + edges`. For a parser bug, it's
the input string. For a numeric bug, it's the inputs and the
computed result.

Avoid:
- "test failed"
- "x out of range"
- "expected != got"

Prefer:
- "MINIMAL nvert=3 nedges=1 edges=[(1,2)]"
- "parser failed on input: \"x\\u0000\""
- "expected gcd(12,18)=6, got 0"

### 5. Don't assert in inner loops if the outer property is stronger

If your test draws 100 elements and you `HEGEL_ASSERT` on each one,
the first failure stops the test. The shrinker will optimize for
*"shortest list with the failing element first"* — which isn't
necessarily the structurally smallest counterexample. Aggregate
the check:

<!-- /ignore counter-example: /* worse */ vs /* better */ assertion-placement comparison -->
```c
/* worse */
for (int i = 0; i < n; i++) HEGEL_ASSERT (process (xs[i]) >= 0, ...);

/* better */
for (int i = 0; i < n; i++) {
  if (process (xs[i]) < 0) { bad = i; break; }
}
HEGEL_ASSERT (bad < 0, "process(xs[%d]) failed", bad);
```

This makes it easier for the shrinker to home in on the *first*
failing element across all replay variants.

## Watching the shrinker work (verbose mode)

By default, hegel-c is silent during generation and shrinking —
only the final replay's output reaches stderr. That's the right
default for CI, but it makes it impossible to *watch* the shrinker
narrow in on the minimum, which is useful both for debugging and
for understanding what hegel is doing.

Set `HEGEL_VERBOSE_TRACE=1` in the environment to print every
primitive draw and every test-case boundary to stderr:

```bash
HEGEL_VERBOSE_TRACE=1 ./tests/irl/scotch/test_graph_order_shrink 2>shrink.log
```

The output looks like this (one cluster per test case):

```
[hegel] case_start #5
[hegel]   start_span(7)
[hegel]   draw_i64(3,20) -> 14
[hegel]   start_span(1)
[hegel]   draw_int(0,30) -> 7
[hegel]   start_span(2)
[hegel]   start_span(7)
[hegel]   draw_i64(0,19) -> 13
[hegel]   draw_i64(0,19) -> 16
[hegel]   stop_span(discard=false)
[hegel]   stop_span(discard=false)
[hegel]   ... (more draws) ...
[hegel]   stop_span(discard=false)
[hegel]   stop_span(discard=false)
[hegel] case_end #5 ok
```

Each case is bracketed by `case_start #N` / `case_end #N <kind>`,
where `kind` is one of:

- `ok` — test passed for this input
- `fail` — test failed via `HEGEL_ASSERT` / `hegel_fail`
- `assume` — test discarded via `hegel_assume(false)`
- `discard (__HEGEL_STOP_TEST)` — engine cut the case short
  (overflow, byte budget exhausted)
- `discard (__HEGEL_ASSUME_FAIL)` — engine-internal discard
- `eof` — child crashed (SIGSEGV, etc.) or exited unexpectedly
  (see [`design_rust_bridge.md`](design_rust_bridge.md) for the
  fork-per-case architecture that makes this recoverable and
  explains how draws flow between the child and the parent-owned
  server connection)
- `panic (...)` — uncategorized panic

The shrinker's behavior is visible in the pattern: during the
generation phase you'll see varied input shapes; once a case
fails, subsequent cases are micro-variations of the failing input
as hegel tries to find something smaller.

Cost: one stderr line per primitive draw. For schema-API tests
with array fields, that's ~10–500 lines per test case, so a
1000-case run can produce ~50k–500k lines. Pipe to `grep`, a
file, or a pager rather than dumping it all to your terminal.

### Three useful invocations

Full trace to a file (one line per primitive draw — large):

```bash
HEGEL_VERBOSE_TRACE=1 ./tests/irl/scotch/test_graph_order_shrink 2>shrink.log
```

Just the case results (one line per case — readable, ideal for
scanning the run):

```bash
HEGEL_VERBOSE_TRACE=1 ./tests/irl/scotch/test_graph_order_shrink 2>&1 \
    | grep '^\[hegel\] case_end'
```

Watch the shrinker progress live, plus the final `MINIMAL` line
(the `--line-buffered` flag is what makes it stream rather than
batch):

```bash
HEGEL_VERBOSE_TRACE=1 ./tests/irl/scotch/test_graph_order_shrink 2>&1 \
    | grep --line-buffered 'case_end\|MINIMAL'
```

A typical scan of the case-end summary looks like:

```
[hegel] case_end #1 ok
[hegel] case_end #2 ok
[hegel] case_end #3 discard (__HEGEL_STOP_TEST)
... (generation phase, mix of ok and discard) ...
[hegel] case_end #25 fail        <- shrinker activates
[hegel] case_end #26 fail
[hegel] case_end #27 fail
[hegel] case_end #28 ok          <- variant that no longer triggers
[hegel] case_end #29 fail        <- smaller failing variant found
... (alternating fail/ok as the shrinker probes) ...
[hegel] case_end #106 eof        <- final replay
```

The pattern of `fail`/`ok` after the first failure tells you the
shrinker is working — it's trying smaller byte sequences and
keeping the ones that still fail.

The trace is library-level — every test built against
`libhegel_c.a` gets it for free, no per-test instrumentation
needed.

## Reading the shrunken trace

A typical hegel-c failure with `hegel_note` looks like:

```
MINIMAL nvert=3 nedges=1 edges=[(1,2)]

thread '<unnamed>' (374129) panicked at runner.rs:871:13:
Property test failed: <message>
```

The first line is your `hegel_note` from the final replay. The
panic block below is hegeltest reporting that the test ultimately
failed. If the test failed via `HEGEL_ASSERT` / `hegel_fail`, the
panic message is your assertion text. If it failed via SIGSEGV
(crash), the panic message is `crashed (signal 11)` or similar.
In either case, the `MINIMAL ...` line is your reproducer
material.

If you see *no* `MINIMAL` line and just `Property test failed: ...`,
either:
- The test didn't reach `hegel_note(tc, ...)` before failing
  (move the note earlier in the function)
- Or the failure is in setup code that runs before `test_fn`
  itself — check that the test compiles and the schema builds

## Other shrink-quality demos in this repo

| File | Demonstrates |
|---|---|
| [`tests/from-hegel-rust/test_shrink_int_to_zero.c`](../tests/from-hegel-rust/test_shrink_int_to_zero.c) | Single int, shrinks to 0 |
| [`tests/from-hegel-rust/test_shrink_int_above_13.c`](../tests/from-hegel-rust/test_shrink_int_above_13.c) | Bounded int, shrinks to the boundary |
| [`tests/from-hegel-rust/test_shrink_boundary_101.c`](../tests/from-hegel-rust/test_shrink_boundary_101.c) | Wide-range int, shrinks to the lower bound |
| [`tests/selftest/test_shrink_square.c`](../tests/selftest/test_shrink_square.c) | Integer overflow in `square()`, shrinks to the boundary value |
| [`tests/selftest/test_strategy_shrink.c`](../tests/selftest/test_strategy_shrink.c) | Grammar-based string fuzzer, shrinks to a minimal failing string (`/vert>0?b:b;`) |
| [`tests/selftest/test_span_tree_json_shrinks.c`](../tests/selftest/test_span_tree_json_shrinks.c) | Recursive tree with span-based structural shrinking |
| [`tests/irl/scotch/test_graph_order_shrink.c`](../tests/irl/scotch/test_graph_order_shrink.c) | **Real bug** in Scotch's `hgraphOrderCp`, shrinks to 3-vertex / 1-edge minimum |

The Scotch test is the most realistic of these — every other
test on this list is testing a known-buggy function written for
the test suite. The Scotch one finds and shrinks a bug in code
that exists for production use, on inputs that exercise a real
property of a real algorithm. If you want one example of what
hegel-c can do for a real C library, that's the one to read.

## Reference

- [`hegel_c.h`](../hegel_c.h) — `hegel_note`, `hegel_fail`,
  `HEGEL_ASSERT`
- [`hegel_gen.h`](../hegel_gen.h) — schema API for declarative
  input description
- [`docs/schema-api.md`](schema-api.md) — schema API reference
- [`docs/patterns.md`](patterns.md) — schema patterns by C layout
- Hypothesis docs on integrated shrinking:
  https://hypothesis.readthedocs.io/en/latest/details.html#how-hypothesis-handles-failures
