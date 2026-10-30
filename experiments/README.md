<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Experiments

Designs we built, measured, and did not keep on main — kept here, in the
tree, rather than on branches nobody reopens. Each one still builds and runs
against main's library, so a claim below can be re-checked instead of trusted.

| Directory | What it is | Status |
|---|---|---|
| [`schema-with-let/`](schema-with-let/) | The declarative schema layer main shipped until 2026-10-30: `HEGEL_STRUCT`, `LET` / `USE` bindings, unions, arrays, `HEGEL_SELF` on the recursion protocol | Retired from main |
| [`schema-with-callbacks/`](schema-with-callbacks/) | A proof of concept on top of it: fields whose schema comes from a callback reading the value drawn so far (`HEGEL_DEPENDS`, `HEGEL_DERIVED`, `HEGEL_LEN_OF`, `HEGEL_LEN`) | Never merged |
| [`TODO-schema.md`](TODO-schema.md) | The schema-layer backlog, moved out of the main `TODO.md` verbatim | Archived |

Main is now imperative: generators are plain C over the `hegel_c.h`
primitives and engine protocols. The reasons are below.

## Running them

```sh
make lib libhegel PREBUILT=1                 # main's library and engine, once
make -C experiments/schema-with-let test
make -C experiments/schema-with-callbacks test
# Scotch harnesses (needs a Scotch build, see tests/irl/scotch):
make -C experiments/schema-with-let scotch-test \
     SCOTCH_INC=-I<scotch>/include SCOTCH_LIB=-L<scotch>/lib
```

Each experiment compiles its own `hegel_gen.c` and links main's core-only
`libhegel_c.a`; binaries go to `<experiment>/build/`. Nothing here is part of
`make test` or CI. Last verified 2026-10-30 against libhegel 0.48.0:
`schema-with-let` 64/64 and `schema-with-callbacks` 73/73 as expected, and
their Scotch harnesses green against Scotch `b4801dbf`.

The `HEGEL_DRAW_INT` / `_I64` / `_U64` / `_FLOAT` / `_DOUBLE` / `_BOOL`
shorthands used to live in `hegel_gen.h`; they are imperative conveniences,
so they moved to `hegel_c.h`, and both snapshots' headers lost their copies.
That is the only change made to the snapshots' code.

## `schema-with-let/`

Main's schema layer as of `3881d33`: `hegel_gen.{c,h}`, its 60 selftests,
the docs-backing test, the original schema versions of three from-hegel-rust
ports, the reference (`docs/schema-api.md`) and pattern catalog
(`docs/patterns.md`), and its real harnesses:

- `irl/scotch/test_graph_part_schema.c` — Scotch graph partitioning from a
  schema;
- `irl/scotch/test_graph_order_shrink.c` — the schema version of the pin
  `hgraphOrderCp` witness (red by design before Scotch `064292127f`; main
  now has a primitive port at the same path in `tests/irl/scotch/`);
- `irl/scotch/test_array_inline_orphan_repro.c` — a hand-run demo of a
  fork-mode bug of the old Rust bridge;
- `irl/linux/ts_kmp/test_ts_kmp_vs_memmem_schema.c` — the schema variant of
  the ts_kmp differential test (record only: it needs the ts_kmp pin's
  kernel tree and Makefile, see `tests/irl/linux/ts_kmp/`).

The docs' transclusion markers point at these files' new paths but are no
longer checked by `make docs-check`; line numbers refer to the files as they
were on main.

## `schema-with-callbacks/`

The view-callbacks PoC (branch `poc/view-callbacks`, 2026-10-26 to 10-29).
Its `hegel_gen.c` is a superset of `schema-with-let`'s, and its Makefile runs
all 64 of that experiment's tests against it, plus its own nine:

- `HEGEL_DEPENDS (T, fn, ctx)` — a slot whose schema `fn (view, ctx)`
  returns at draw time; `HEGEL_VIEW_FIELD (v, T, f)` reads a field drawn
  earlier, checked against a per-struct drawn bitmap (`test_view_depends`,
  `test_view_read_before_drawn`);
- `HEGEL_DERIVED`, `HEGEL_LEN_OF` — slots computed from the struct,
  `LEN_OF` as soon as its array is drawn (`test_view_len_of_shrink`);
- `HEGEL_LEN (lo, hi)` — an array length owned by the engine's collection
  protocol;
- `hegel_view_up` / `_index` / `_depth` — the struct above, the array
  element being drawn, the nesting depth;
- nested draws: a callback may run its own schema draw; recursion records
  form a stack (`test_view_nested_draw`).

Real harnesses: `irl/scotch/test_graph_part_view.c` and
`test_graph_part_prim.c` (the callback and primitive thirds of the Scotch
graph-partitioning trio; the schema third is in `schema-with-let`), and
`irl/scotch/test_arch_sub_view.c`, the pin `archSubSingle` witness drawn
through callbacks (red by design on Scotch `b4801dbf`).

## What we measured

| Question | Result |
|---|---|
| Does a drawn length (`LET n`) forfeit deletion shrinking? | No. "contains 77" shrank to `[77]` and "10 then 20" to `[10, 20]` on 8/8 seeds, as with an engine-owned length. |
| Do the three styles shrink differently? | No. On the JSON tree swap bug, primitive, schema and callback generators all shrank to the same minimal tree (one node, two leaves differing by 1) on 5/5 seeds. |
| What decides the size of what gets generated? | The engine mechanism, not the API layer. Collection protocol: ~5 elements whatever the max (graphs of 6.4 edges where a drawn length gave ~60). Recursion protocol: trees of 36–41 nodes; 50/50 children with a depth cap: ~5 nodes, written by hand or through callbacks alike. |
| Can callbacks express real dependencies? | Yes. They removed the clamping and self-loop stripping of the Scotch graph harness (usable edges 61.8% → 64.3%), and `test_arch_sub_view` finds the `archSubSingle` bug, shrinks it to the pin's minimal case (`archCmplt(2)`, keep `{1}`) and is green on the fix, like the primitive witness. |
| At what cost? | `test_arch_sub_view`: 204 non-empty lines against 85 for the primitive witness, ~130 lines of generation against ~20, ~1.3× the run time. |
| How much real code uses the schema layer? | Before the move: 4 of the 85 harness files under `tests/irl/` on main (Scotch, Linux), plus the 2 written during the PoC. Every Scotch property test builds CSR arrays from a topology with plain draws; Scotch's API has no struct inputs (all its public structs are opaque `double dummy[N]`). `LET_ARR`, `USE_AT`, `USE_PATH` appeared in no real harness. |

## Why main went imperative

For bug hunting — what this repository does — plain draws plus small helper
functions fit nearly every harness: shorter, clearer, as good at shrinking,
and they reach every engine protocol. The schema layer earned its keep in two
places only:

- recursive structures, where the recursion protocol's branch / leaf / retry
  / finish loop and the unwinding of a half-built tree are easy to get
  wrong — main now aims to offer that as a helper for hand-written
  generators;
- large C structs whose layout changes upstream, where `HEGEL_STRUCT`'s
  `sizeof` / offset check fails at schema-build time; nothing in this
  repository exercised that.

Callbacks made the schema layer more capable, not more needed. The question
the old TODO called the "B vs C bake-off" — "describe your C types, get
everything free" against "Hypothesis's engine with C ergonomics" — is
answered for now in favour of the second; its write-up is in
[`TODO-schema.md`](TODO-schema.md).

## Not pursued

Ideas discussed for `schema-with-callbacks` and never built:

- **`HEGEL_SELF` returned from a callback.** A schema a callback builds is
  never walked by `HEGEL_STRUCT`'s `SELF` resolution, so its `target` stays
  NULL and the draw segfaults. Fix: resolve still-unresolved `SELF`s in the
  returned schema against the view's struct, without descending into nested
  structs. Without it, callback-driven trees can't use the recursion
  protocol (coin flips instead: ~5 nodes against ~37).
- **`HEGEL_NULL ()`.** No schema means "always NULL"; the callback tests
  write `hegel_schema_i64_range (0, 0)` into pointer slots.
- **`LET` values readable from callbacks** (`HEGEL_VIEW_LET`) — hidden
  values drawn into the view and never into value memory; would have made
  `LET_ARR` / `USE_AT` / `USE_PATH` removable.
- **Owned allocations for derived fields.** A `DERIVED` callback that
  allocates (CSR arrays from a drawn topology) has nowhere to hand the
  memory, so it leaks; this is what kept the Scotch property tests out of
  reach.

## Known issues in the snapshots

- `schema-with-let`: `hegel__draw_root` drops its recursion record from
  `hegel__rec_cur` before `hegel_recursion_finish` / `_retry` / `_free`;
  an engine call that jumps out there (overrun, case discarded) leaks the
  record. Fixed in `schema-with-callbacks` only. Callbacks must also stay
  pure: a map / filter function that runs a schema draw frees the outer
  draw (fixed, with nested draws, in `schema-with-callbacks`).
- `schema-with-callbacks`: callbacks re-run on every retry of a recursive
  draw, so side effects repeat; the gaps listed under "Not pursued".

## What stayed on main

- `hegel_target` and its selftest (`tests/selftest/test_target.c`).
- `tests/selftest/test_recursion_nary_tree.c` — a hand-written n-ary tree
  on the recursion protocol, from the recursion comparison.
- Primitive ports, same draws as the schema versions they replace:
  `tests/irl/scotch/test_graph_order_shrink.c` (pin `hgraphOrderCp`:
  red on the parent of `064292127f`, shrinks to `nvert=3`, one edge; green
  on the fix), `tests/selftest/test_filter_too_much{,_suite}.c`, and
  `tests/from-hegel-rust/test_combinators_{filter,optional}.c`,
  `test_shrink_bounded_positive.c`.
- `core/`'s `hegel__draws_abandoned` hook (run at the end of a nofork case
  and when a stateful rule assumes), which `schema-with-callbacks` uses to
  free abandoned draws.
