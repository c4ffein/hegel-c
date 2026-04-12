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

```c
typedef struct EdgePair {
  int u;
  int v;
} EdgePair;

typedef struct Graph {
  int          nvert;
  EdgePair *   edges;
  int          nedges;
} Graph;
```

A graph is a vertex count plus a list of undirected edges. Note
that this is the **logical** input — how we want hegel to think
about graphs — not the CSR representation Scotch wants.

### Step 2: declare the schema

```c
graph_schema = hegel_schema_struct (sizeof (Graph),
    HEGEL_INT (Graph, nvert, 3, 20),
    HEGEL_ARRAY_INLINE (Graph, edges, nedges,
                        edge_schema, sizeof (EdgePair),
                        0, 30));
```

The schema generates graphs with 3–20 vertices and 0–30 edges.
We deliberately keep the ranges small so that shrunken
counterexamples are easy to read.

### Step 3: write a property

The property is: **`SCOTCH_graphOrder` must produce a valid
permutation of `[0, nvert)`** — every value in range, no
duplicates, all positions written.

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
`HEGEL_INT(T, x, 0, 1000000)` shrinks fine, but the *minimum* it
can shrink to is `0`, `1`, etc., based on byte-stream simplicity
order. If your property only cares about whether `x` is even, use
`HEGEL_INT(T, x, 0, 1)` and post-multiply — the minimum you'll see
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

```c
hegel_note (tc, "input shape: ...");
some_function_that_may_crash (input);
HEGEL_ASSERT (post_condition, ...);  /* may not be reached */
```

If the property crashes, the final replay still calls `hegel_note`
once before crashing, so the input shape lands in stderr.

### 3. Don't bury the input behind opaque transformations

If your test does:

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

```c
char *s = malloc (n);
for (int i = 0; i < n; i++) s[i] = hegel_draw_int (tc, 0, 255);
HEGEL_ASSERT (parse (s) == n, "...");
```

Now both `n` *and* the bytes shrink coherently.

The schema API does this for you automatically — `HEGEL_ARRAY*`
draws each element through the schema, which in turn uses
primitive draws. No extra discipline needed if you stick to the
schema layer.

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
