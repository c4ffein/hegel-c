<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# TODO — schema layer (archived)

Schema-layer items moved out of the main `TODO.md` when the schema layer moved
to `experiments/` (2026-10-30). Verbatim, under their original headings; paths
they name (`hegel_gen.h`, `tests/selftest/test_schema_*.c`, `docs/schema-api.md`)
now live under `experiments/schema-with-let/` (the PoC's under
`experiments/schema-with-callbacks/`). See `README.md` next to this file.

## Next up (2026-10-21) — the schema items

- **View callbacks PoC (2026-10-26, branch `poc/view-callbacks`).** A
  third path that came out of the B/C discussion: keep B's declarative
  layer and let fields read the tree drawn so far through callbacks.
  `HEGEL_DEPENDS (T, fn, ctx)` returns the schema for a slot from a
  `hegel_view` (`HEGEL_VIEW_FIELD` checked against a per-struct drawn
  bitmap, `HEGEL_VIEW_AS`, `hegel_view_up`, `hegel_view_index`);
  `HEGEL_DERIVED (T, fn, ctx)` computes a slot after the struct;
  `HEGEL_LEN_OF (T, field)` back-fills a count; `HEGEL_LEN (lo, hi)` is an
  engine-owned `HEGEL_ARR_OF` length (collection protocol). Tests:
  `test_view_depends`, `test_view_len_of_shrink`,
  `test_view_read_before_drawn`; port: `tests/irl/scotch/test_graph_part_view.c`
  (closes gap#1 of `test_graph_part_schema.c`: no clamping, no self-loop
  stripping, no npart clamp; usable edges 61.8% -> 64.3%, the rest are
  duplicates = gap#2). Findings that change the bake-off:
  - *LET lengths shrink fine.* With `LET n` + `ARR_OF (USE (n))` the
    engine still shrinks "contains 77" to `[77]` and "10 then 20" to
    `[10,20]` on 8/8 seeds, same as `HEGEL_LEN`. The "pre-drawing n
    forfeits deletion shrinking" argument below did not reproduce.
  - *`HEGEL_LEN` makes shorter arrays.* Mean length 8.6 vs 16.1 for a
    drawn `n` in [0, 30] (identical to the raw collection protocol), so
    "10 then 20" was found on 3/8 seeds vs 8/8. Engine design, not a bug;
    it argues for keeping drawn lengths available.
  - *It bites real harnesses.* `tests/irl/scotch/test_graph_part_prim.c`
    (primitive generators, collection + `hegel_collection_reject` for
    unique edges — closes gap#2 too) averages 6.4 edges per graph vs ~60
    for the schema/view versions, whatever the max. libhegel's
    `new_collection` takes only min/max: an average-size parameter would
    be worth asking upstream for.
  - *Recursion trios* (prim / schema / view for every recursive
    selftest; `hegel_view_depth` added so a callback can cap its own
    recursion). Binary JSON swap bug: all three shrink to the same
    minimal tree (one node, two leaves differing by 1) on 5/5 seeds.
    What differs is the distribution, and it follows the *mechanism*,
    not the API: over 1000 cases, 50/50 children with a depth cap
    (prim `gen_tree` or view callbacks) average 4.8–5.1 nodes, max 13;
    the recursion protocol (`HEGEL_SELF` or hand-written
    `hegel_recursion_*`) averages 36–41 nodes, max 99–277. Views can't
    reach the protocol: a `DEPENDS` callback may not return `HEGEL_SELF`
    (a schema built in a callback has no struct to resolve it against),
    and a leaf budget needs counter state in `ctx`, which can only
    truncate, not unwind and redraw (`test_view_tree_limits`). Fix if
    views stay: resolve a returned `SELF` against the view's struct.
    Also missing: an "always NULL" pointer schema — the depth cap
    writes `hegel_schema_i64_range (0, 0)` into the pointer slot.
  - *Nested draws.* A callback that ran its own `hegel_schema_draw`
    made the inner draw free the outer, half-built value: the root draw
    took any recursion record still set for one left by an abandoned
    draw (heap-use-after-free, SIGSEGV on 4/40 replays). Records now
    form a stack (`prev`), each top-level draw restoring the one it
    found; the root view owns its draw's record (`hegel__view_rec`);
    the runtime frees really abandoned records through
    `hegel__draws_abandoned` (end of a nofork case, a stateful rule that
    assumed). Fixing it showed an older leak: `hegel__draw_root` dropped
    its record before `recursion_finish` / `_retry` / `_free`, any of
    which can jump out (overrun, case discarded), losing the record —
    it now stays current until freed. `test_view_nested_draw`; ASan
    clean on nested draws with and without jumps, plain and stateful.
  - *First real witness on callbacks.* `tests/irl/scotch/props/test_arch_sub_view.c`
    = `test_arch_sub.c` (pin archSubSingle) with the case from a schema:
    `DEPENDS` for kind-dependent sizes, a bit array sized from them, swap
    indices from `hegel_view_index`; `LEN_OF` counts; `DERIVED` terminal
    list. Same draws in the same order. Both red on 5/5 seeds at
    `b4801dbf` (SIGSEGV), both shrink to the pin's minimal case
    (`archCmplt(2)`, keep `{1}`), both green on the fixed state with ASan
    (3 seeds). Costs: 204 vs 85 non-empty lines (generation ~130 vs ~20;
    the property is the same), runs ~1.3x longer. Not in the pin's
    `PIN_TESTS` yet. Found on the way: `LEN_OF` was a derived field, so a
    `DEPENDS` callback could never read a count; it now fills as soon as
    its array is drawn.
  - *Where callbacks don't reach:* harnesses whose input is converted, not
    drawn — every Scotch prop builds CSR arrays from a topology
    (`props/graph_gen.h`). A schema could draw the topology, but the CSR
    arrays would have to come from a `DERIVED` callback that allocates,
    and nothing owns memory a callback allocates (leak). Needs an
    "owned allocation" hook for derived fields, or these stay primitive.
  - Open: whether the checked accessor feels too heavy in real code (vs
    a poison fill + typed `VIEW_AS` only); a `HEGEL_FIELD` offsetof check
    for positional entries; `HEGEL_ARRAY_INLINE` on the collection
    protocol; whether views make LET/USE/USE_AT/USE_PATH removable.
- **Gen-layer bake-off: B vs C.** Still undecided. The rebase kept today's
  schema layer (`hegel_gen.h`, LET/USE included) and only moved its internals
  to libhegel (`HEGEL_SELF` on the recursion protocol); option C was never
  built. Plan: write the same real harnesses (Scotch CSR, an xarray target, a
  big third-party struct that evolves, one value shipped to a remote target)
  in C's style next to today's, then pick.
  - *The question underneath:* is hegel-c "describe your C types, get
    everything free" (B) or "Hypothesis's engine with C ergonomics" (C)? The
    bughunt workload argues C; a library for strangers and the embedded
    ship-the-schema idea argue B.
  - *B, conservative port:* `HEGEL_STRUCT` stays. LET/USE goes, because the
    collection protocol makes lengths an output of the engine (pre-drawing
    `n` forfeits deletion shrinking): counts are back-filled with
    `HEGEL_LEN_OF(items)`, bounds come from `HEGEL_INT_BELOW(name)`, and
    `HEGEL_FIELD(T, name, entry)` checks offsetof and member size, which also
    catches two same-size fields swapped. 31 schema kinds → 15; most
    selftests unchanged. Loses: parallel arrays sharing one drawn length
    (`xs, ys, n`) become inexpressible; a reference typo becomes a runtime
    abort.
  - *C, fill functions:* a generator is `void gen_foo (hegel_ctx *g, Foo
    *out)`; dependent fields are code reading fields already written (CSR
    endpoints are just `H_INT (g, 0, nvert - 1)` plus `h_list_reject` for
    duplicates). Arena owns all drawn memory, so no free code; lengths come
    out of the call (`out->items = h_arr_int (g, &out->n, 0, 10, 0, 100)`);
    `H_COMPOSITE (g)` labels each function's span; `H_GEN` sugar for the
    trivial case. ~1,100 LOC vs ~2,600 for B. Loses: a forgotten field is
    silently zero, a forgotten `H_COMPOSITE` silently flattens shrink
    structure, and when a third-party struct grows a field nothing fails the
    build (today the sizeof assert does).
  - *Where each wins:* CSR, jagged 2D and unions favor C (dependencies are
    locals, a union is an `if`); recursive trees favor B (the protocol stays
    hidden); struct drift favors B (the build-time alarm is the reason the
    declarative family exists). Only B keeps the schema as data, which is
    what tooling or shipping the schema to a board would need.
  - Two other options were drafted on 2026-10-05 and set aside: A, a thin
    veneer (C without its discipline), and D, handles/facets V2 (one draw,
    named facets projected into slots; biggest migration, one silent
    footgun: a missing `HEGEL_OUTER` turns into per-element draws).
- **Named shape accessors** — see "Named shape accessors" below.
- **Raw-bytes schema field.** `hegel_draw_bytes` exists as a primitive;
  the schema layer has no `HEGEL_BYTES(lo, hi)` (pointer + length) —
  the "Binary" gap in "Missing from Go" below.

## Binding system — deferred items (ALL CLOSED 2026-07-13)

The HEGEL_BINDING / HEGEL_LET / HEGEL_USE / HEGEL_ARR_OF system has
replaced the old facet mechanism.  The seven gaps originally tracked
here are all closed; kept as a map from gap to what closed it:

1. **Indexed USE for array-of-arrays** — DONE.  `HEGEL_LET_ARR` +
   `HEGEL_USE_AT` (+ `HEGEL_USE_PATH`) implement the "sizes drives
   groups" target, including the N / sizes[N] / groups[sizes[i]]
   form.  Tests: `test_binding_let_arr`, `test_binding_jagged_3d`,
   `test_binding_use_path`.

2. **Shrinking quality tests** — DONE.  Probe-file `minimal()`-style
   tests: `test_binding_shrink_bag`, `_jagged`, `_arr_of_structs`,
   and `_shadow` (the re-LET/shadowing case — shrinker treats
   bindings uniformly).  `test_binding_shrink_use_in_cases` extends
   this to the union-case / variant-case / optional paths.

3. **HEGEL_USE inside HEGEL_VARIANT cases** — DONE.  Plumbing was
   indeed there.  `test_binding_use_in_variant` covers outer-LET
   resolution from both cases plus case-local LET shadowing.
   Gotcha pinned in that test: the variant cluster (tag + ptr) is
   pointer-aligned as a unit, so it cannot directly follow a lone
   int field the way a bare C `int tag; void *v;` pair could — the
   sizeof assert catches the mismatch at build time.

4. **HEGEL_USE inside HEGEL_UNION cases** — DONE.
   `test_binding_use_in_union`: tagged + untagged + case-local LET
   shadowing (LET inside HEGEL_CASE works; BIND entries are
   zero-size in the case layout, same as in structs).

5. **HEGEL_OPTIONAL wrapping a struct with HEGEL_USE** — DONE.
   `test_binding_use_in_optional`, including an inner
   `HEGEL_ARR_OF` whose length USE resolves through the optional.

6. **Non-int binding kinds** — DONE.  LET accepts i64/u64/float/
   double (and composed scalars); typed `HEGEL_USE_I64/_U64/_FLOAT/
   _DOUBLE` read them with a hard width/sign/float check.  Test:
   `test_binding_widths`.  Pointers remain out of scope.

7. **Re-LET abort diagnostic with binding names** — DONE.
   `HEGEL_LET` / `HEGEL_LET_ARR` stringify the binding name into
   the schema (`hegel_schema_bind_named` / `_let_arr_named`); the
   duplicate-LET abort now prints `HEGEL_LET (name) appears twice`.
   Un-named `hegel_schema_bind` / `hegel_schema_let_arr` remain for
   dynamic construction.  Test: `test_binding_relet_abort`
   (TESTS_CRASH).



## V2 handles — V1 scope landed 2026-04-17

**Status:** `HEGEL_ARRAY` facets model is in.  `HEGEL_ARRAY(elem, lo,
hi)` builds an array schema projected into the parent struct via
`HEGEL_FACET(hat, value)` + `HEGEL_FACET(hat, size)`.  Facets may be
non-adjacent, in either order.  Per-struct-instance ctx scoping —
facets within one struct share one drawn array; across struct
instances (e.g. array elements) each gets its own draw.  Three new
pattern tests (`facets_nonadjacent`, `facets_reversed`, `facets_nested`)
plus all 7 existing `HEGEL_ARRAY` sites migrated.  Docs updated
(`schema-api.md`, `patterns.md`, `hegel_gen.h`).  Design memo that
drove the work is `v2-handles-handoff.md` (delete once we're
confident the approach stands).

### Out of scope for V1 (still deferred)

- `HEGEL_COPY` — shared template, independent draws.  See its own
  section below.
- Facets for `HEGEL_OPTIONAL` / `HEGEL_UNION` / `HEGEL_VARIANT` —
  V1.1 once the array pattern is shaken out.
- Schema/shape merge — exploratory; see its own section below.
- `HEGEL_ONE_OF` hardening — tracked separately below.

## rn

- **Filter retry + discardable span parity with `hegel-rust`.**  Both
  `HEGEL_FILTER_INT/I64/DOUBLE` draw paths — the originals in
  `hegel__draw_field` and the composition cases in
  `hegel__draw_integer_into` / `_fp_into` — do one draw attempt and
  call `hegel_assume(tc, 0)` on predicate failure, with no span
  around the attempt.  Reference hegel-rust
  (`inspiration/hegel/hegel-rust/src/generators/generators.rs::Filtered::do_draw`)
  retries 3 times, wrapping each attempt in a `labels::FILTER` span
  that is **discarded** (`stop_span(true)`) on rejection — keeps
  shrink quality clean by not polluting the span tree with rejected
  byte ranges.  hegeltest's legacy `gen_draw_int_impl` got the
  3-retry count right but skipped the span; the schema API did
  neither.  Practical effect: a filter rejecting 50% trips
  `filter_too_much` ~4× sooner under the schema API than under
  either Rust implementation (0.5 vs 0.5³ = 12.5% discard rate).
  Fix: loop 3× in each of 3 arms × 2 sites, emit `HEGEL_SPAN_FILTER`
  around each attempt with the discard flag set on predicate
  failure.  `HEGEL_SPAN_FILTER` already exists in `hegel_c.h`
  (line 149), no infrastructure gap.  ~40 lines total, standalone.

- **Harden `HEGEL_ONE_OF`.**  After the primitive-macro refactor,
  `HEGEL_ONE_OF` dispatches on the first case's kind for slot
  size/align, and the draw path at `hegel_gen.c` `HEGEL_SCH_ONE_OF_SCALAR`
  only handles INTEGER / FLOAT cases — anything else (struct, text,
  mismatched-width scalars) is silently wrong.  Two levels of fix:
  (a) **Immediate:** in `hegel_schema_one_of_scalar_v`, walk all
      cases, compute `hegel__schema_slot_info` for each, abort at
      schema-build time if sizes or alignments disagree.  Cheap,
      catches the `HEGEL_ONE_OF(HEGEL_INT(...), HEGEL_DOUBLE(...))`
      footgun before it silently becomes a double slot with ints
      written into it.
  (b) **Design question:** should `HEGEL_ONE_OF` be generalized to
      accept heterogeneous cases (scalars of different widths,
      structs, etc.)?  If yes, it becomes equivalent to
      `HEGEL_UNION_UNTAGGED` with implicit single-field cases and
      we should either merge them or alias one to the other.  If
      no, keep the name narrow ("same-width scalar picker") and
      keep `HEGEL_UNION_UNTAGGED` as the general form.  Defer until
      there's a real use case pushing for the wider semantics.

- **Path-based shape accessor (vs offset-based).**  `HEGEL_SHAPE_GET`
  currently maps `offsetof(T, field)` to a leaf shape and silently
  descends through inline sub-structs when offsets collide (see
  `hegel_shape_get_offset` comment).  That's the right default for
  the common case (leaf assertions), but it means inline wrapper
  shapes are unreachable via this accessor.  If a use case appears
  for walking wrapper shapes (span inspection, introspection
  tooling), add a path-based companion that composes
  `hegel_shape_field` by index.  C preprocessor can't split
  `b.g.karat` into tokens, so the form would be
  `HEGEL_SHAPE_PATH(sh, 0, 0, 0)` or fixed-arity variants — uglier
  than the offsetof form, which is why it's a companion, not a
  replacement.  Skip until an actual use case shows up.

## Exploratory

## Reuse multiple fields of a generator (V2 rethink)

```
❯ How about something like : x = HEGEL_ARRAY(...); s = HEGEL_STRUCT(..., x.array_pointer, x.size); ??? Build the tree in a smart
  way? Everything works on draw?

● This is actually a genuinely good idea, and it generalizes beyond arrays. Let me sketch it honestly.

  What you'd write

  hegel_array_t x = HEGEL_ARRAY(hegel_schema_int_range(0, 100), 0, 10);

  hegel_schema_t s = HEGEL_STRUCT(Bag,
      x.array_pointer,   // goes wherever the ptr field is
      x.size);           // goes wherever the len field is

  /* Or with other fields in between: */
  hegel_schema_t t = HEGEL_STRUCT(Thing,
      HEGEL_INT(0, 100),  // some other field
      x.size,             // len comes first!
      HEGEL_TEXT(1, 5),   // another field
      x.array_pointer);   // ptr comes later

  What each accessor is

  x is a handle containing two layout entries plus a shared pointer to some generation state:

  typedef struct {
    hegel_array_state *  state;      // malloc'd, shared
    hegel_layout_entry   array_pointer;
    hegel_layout_entry   size;
  } hegel_array_t;

  typedef struct {
    hegel_schema *  generator;       // the underlying array schema
    void *          drawn_ptr;       // filled at draw time
    int             drawn_len;       // filled at draw time
    int             drawn_this_run;  // reset per test case
  } hegel_array_state;

  Both x.array_pointer and x.size carry a pointer to the same state. They're independent layout entries from the layout pass's
  point of view (each occupies one slot), but at draw time they collaborate.

  How draw resolves the dependency

  When hegel__draw_struct iterates bindings in order and hits either the x.array_pointer binding or the x.size binding:

  1. Check state->drawn_this_run. If not set, call state->generator's draw — this allocates the heap buffer and picks the length,
  storing both in state.
  2. Write the relevant facet (drawn_ptr or drawn_len) at this binding's offset.
  3. Mark drawn_this_run.

  The second binding encountered skips step 1 because state is already populated. Order doesn't matter — whichever of
  x.array_pointer or x.size comes first in the positional list triggers the actual generation.

  At the end of each test case (or start of the next), reset drawn_this_run = 0 so the next case draws fresh. Either the struct
  schema does this pre-draw, or the state has a "round stamp" compared against the current round.

  This generalizes beautifully

  Once you have "produce one value, project facets," you can do it for more than arrays:

  hegel_union_t u = HEGEL_UNION(
      HEGEL_CASE(HEGEL_DOUBLE(0.1, 100.0)),
      HEGEL_CASE(HEGEL_INT(), HEGEL_INT()));

  HEGEL_STRUCT(Weird,
      u.tag,           // tag at field 0
      HEGEL_FLOAT(),   // an unrelated field in between
      u.body);         // body at field 2 — non-adjacent from the tag!

  Variant, array_inline, even user-defined composites. The "2-slot adjacent" constraint of the current design is lifted entirely.

  The cost

  Not trivial. You need:
  - Shared heap state per grouped generation
  - New layout entry kinds for "read from shared state at this slot"
  - Draw-time resolution: check-then-generate-or-reuse
  - Per-test-case reset of the drawn_this_run flag, probably owned by the enclosing struct schema
  - Ownership story: the shared state must be freed exactly once, even though two layout entries reference it. Refcount on the
  state, or the struct schema takes exclusive ownership when it consumes the handle, or the array schema node itself carries the
  state.

  I'd estimate ~250–400 lines for the array case alone, maybe another 150 to generalize to unions/variants. Plus a handful of
  tests.

  The actual conceptual win

  The part I find most appealing is what it says about the model:

  ▎ A composite schema produces one value. That value has named facets (ptr, length, tag, body). You project facets into struct
  ▎ fields wherever they need to land. The schema's job is the generation; the layout's job is the placement; the two are fully
  ▎ orthogonal.

  That's cleaner than today's "a composite schema occupies 2 adjacent slots in declaration order." It matches how you think about
  the problem, not how C happens to represent it.

  My honest take

  I think this is the right direction for a V2 of the schema API. The positional form we just built is a clear step up from the
  offsetof form, but it still conflates "generate a value" with "place its pieces in a parent struct." Your proposal cleanly
  separates them. If you're building hegel-c with the "many users, upstream-blessed C binding" mindset (which your memory says you
  are), this is probably the shape the API should land in long term.

  Worth filing? Yes, strongly. Not as "nice-to-have exploratory" but as "the next major API iteration, when we're ready to revisit
  the positional form." Want me to write it up as a TODO entry — something clearly distinct from the HEGEL_INLINE exploratory note,
   since this is a bigger rethink rather than a gap-filler?
```

**Related symptom — `HEGEL_INLINE_REF` refcount footgun.**  Today,
sharing a pre-built struct schema across two `HEGEL_INLINE_REF`
slots requires the user to call `hegel_schema_ref` once before the
second use; forgetting it is a silent double-free at
`hegel_schema_free` time (visible under ASan, invisible otherwise).
This is not a bug in `HEGEL_INLINE_REF` specifically — it's the
same ownership gap this entry is trying to close.  Under the
handle model, `INLINE_REF` becomes a "shared template, independent
draws" facet of a handle that owns the underlying schema, and the
ref-bumping requirement disappears because facets are non-owning.

Until the V2 handle model lands: leave `INLINE_REF` semantics
alone (matches the rest of the API's transfer-on-pass convention),
and add an ASan-caught deliberate-misuse selftest that pins the
current behavior so a future accidental fix doesn't silently change
it.

## `HEGEL_COPY` for shared-template instantiation

A schema-tree deep-copy: `HEGEL_COPY(s)` returns a fresh independent
tree with the same shape.  Use case: place the same schema template
at two sites without the manual `hegel_schema_ref` bookkeeping that
`HEGEL_INLINE_REF` currently requires — fixes the silent double-free
footgun from the V2 rethink section above.

Orthogonal to V2 handles: handles solve "shared draw, project
facets"; copy solves "shared template, independent draws".  Cheap
O(tree size) at schema-build time, no per-case cost.

**Gotcha to verify before coding:** how `hegel_schema_self()` is
represented.  If it's a by-name sentinel resolved against the
enclosing root at draw time, copy is trivial.  If it stores a pointer
to a specific root, copy needs pointer rewrites during the tree walk.

**Deferred** until V2 handles land.  Validate the broader "composite
schemas with projected facets" mechanic first — if handles happen
to subsume this use case, we don't need `HEGEL_COPY` at all.

## Merge schema and shape into a single tree? (exploratory)

Today: schema (immutable description, refcounted, built once) and
shape (per-run value wrapper, built on draw, owns value memory) are
separate tree types that mirror each other.

Alternative to ponder: one tree.  Each node carries `(schema_kind +
schema_args + nullable drawn_state)`.  `drawn_state` is null until
populated by draw; reset to null at the start of each test case;
whole tree freed once at end.  The user's phrasing was "two parallel
arrays at the root, one of pointers to schemas, one of either null
or pointer to shapes" — same idea expressed as a sparse mirror.

**Plausibly cleaner:** one ownership story, no two-phase allocation,
no refcount bookkeeping beyond an optional tree-level refcount if we
want `HEGEL_COPY` to share structure.

**Risks:** much bigger refactor than V2 handles alone; schema becomes
mutable (fine in fork mode + sequential suite mode, but we lose the
invariant); every current shape-accessor API (`hegel_shape_tag`,
`_array_len`, `_is_some`, `_field`) needs re-examination.

**Disposition:** exploratory only, decision deferred.  Do V2 handles
in the current two-tier model first.  If handles fit naturally there,
the split is earning its keep.  If they feel forced — uglier draw
path, extra indirection, awkward ownership — that's the empirical
signal to revisit this.


## From "High-leverage next work"

### Named shape accessors

`hegel_shape_field(sh, 0)` is positional. Adding a field before
your union shifts every index, silently breaking any code that
reads metadata from the shape tree. Especially bad for untagged
unions where users **must** use the shape to get the variant tag.

Fix: store field names in the schema, propagate them to struct
shape nodes, add a lookup function:

```c
int tag = hegel_shape_tag (hegel_shape_get (sh, "shape_field"));
```

Moderate work: touching the schema constructors, the shape node
struct, and the test files that currently use positional access.
Big ergonomic payoff.


## From "API polish"

### Rename / clarify `HEGEL_SELF`

`HEGEL_SELF(T, f)` silently expands to a binding that places
`hegel_schema_optional_ptr(hegel_schema_self())` at `offsetof(T,f)`,
which means:
1. The field is always optional (50% NULL chance)
2. The field must be a pointer type
3. Required recursive references aren't expressible

This is usually the right semantic for recursive trees but the
name hides the optionality. Options:

- Rename to `HEGEL_OPTIONAL_SELF(T, f)` — more honest
- Keep `HEGEL_SELF` but document the behavior clearly (the header
  does, but users might skim past it)
- Add both — `HEGEL_SELF` as convenience alias, `HEGEL_OPTIONAL_SELF`
  as the primary name

### Distinct wrapper subtypes for compile-time ARRAY_INLINE safety

Currently `HEGEL_ARRAY_INLINE` does a runtime assert that the
element schema kind is `STRUCT/UNION/UNION_UNTAGGED`. Fails loudly
at setup time if you pass a `VARIANT`, `ONE_OF_STRUCT`, or scalar
schema.

The compile-time version is distinct types per kind:
`hegel_struct_schema_t`, `hegel_union_schema_t`, `hegel_variant_schema_t`.
Constructors return specific types; `HEGEL_ARRAY_INLINE` uses
`_Generic` at the call site to only accept struct / union types.

Cost: doubles the API surface, requires `_Generic` ceremony at
every composable macro. Benefit: real compile-time safety for the
one class of errors we can prevent.

**Decision deferred.** The runtime assert works in practice.
Revisit if the assert fires often in real use, or if distinct
subtypes become valuable for other reasons (e.g., phantom types
for lifetime tracking).

### Optional external-lifetime flag

A per-schema flag like `external_lifetime = true` that makes
`hegel_schema_free` skip freeing. Use case: a schema whose value
memory is NOT owned by hegel (e.g., the user pre-allocated it or
wants to pass through existing data).

**Not needed for any current use case.** Revisit if a concrete
scenario appears.

### Typed `HEGEL_USE_*` variants — strict match vs. auto-widen

Today, `HEGEL_USE_I64(name)` requires `name` to have been bound at
i64 width; mismatches abort at draw time.  Same rule for
`HEGEL_USE_FLOAT` / `HEGEL_USE_DOUBLE` against the binding's
is_float flag.  The validator is implemented at `hegel_gen.c`'s
draw path and the rule is encoded in `use_def`'s width / is_signed
/ is_float fields.

**Question:** should narrower-source-into-wider-slot widen
automatically (i32 binding → i64 slot, sign-extended), matching
C's natural integer promotion?

**Today: keep strict.** Reasons:
- Property testing wants type errors loud — widening hides "I
  thought this was already i64" mistakes.
- `HEGEL_MAP_INT(_I64)` is the documented escape hatch for
  explicit conversion, so widening would be a second way to do
  the same thing.
- One-way ratchet: relaxing later is safe, tightening later
  breaks tests.
- Signedness traps: HEGEL_INT(-1, 100) widened into a u64 slot
  produces UINT64_MAX for -1 via two's-complement; correct C,
  but a property-test author may not be thinking that way.

**Counter:** in C source, `int64_t x = some_int8;` is the natural
spelling and works as expected.  Hegel-c's "the C way" framing
arguably wants the same ergonomics.

**Decision: defer.**  Revisit if a real test bumps into the
strict abort and the only fix is `HEGEL_MAP_INT` boilerplate that
adds nothing.  If we change it, do it at the USE site
(`HEGEL_USE_WIDEN_I64` opt-in) before considering implicit slot-
level widening — the latter changes layout-walk semantics.

### `HEGEL_LEN_PREFIXED` / `HEGEL_TERMINATED` — synthetic shape nodes

The draw path for these allocates a `HEGEL_SHAPE_SCALAR` leaf for
the prefix slot (LEN_PREFIXED, slot 0) and the sentinel slot
(TERMINATED, slot n).  These slots are *synthesized* — no draw
event, no span, no entropy consumed from the byte stream — but
the shape tree records them as if they were real scalar draws.

Today this is harmless: the shape tree's only job is freeing
memory, and `array_shape.len = n+1` matches the buffer size.

**Future risk:** if any consumer of the shape tree gains the
ability to map shapes back to byte-stream regions (e.g. a
`hegel_shape_byte_range(sh)` accessor, or a richer shrinker
hint), synthetic nodes will look identical to real draws and
return wrong answers.

**Cheapest fix when needed:** introduce `HEGEL_SHAPE_SYNTHETIC`
(or set `schema = NULL` as a sentinel) so future code can
distinguish.  Keep the alloc — it makes the free path uniform.
Code lives in `hegel_gen.c`'s `HEGEL_SCH_LEN_PREFIXED` /
`HEGEL_SCH_TERMINATED` cases of `hegel__draw_field`.

## From "High-leverage next work" and "API polish" (moved 2026-10-30)

### Point the library at real C code (Scotch, etc.)

**Gaps surfaced** (workarounds in the test files):

1. **No sibling-dependent field bounds.** Edge endpoints should
   be bounded by the sibling `nvert`, but schema only supports
   constant integer bounds.  Worked around by drawing in
   `[0, MAX_VERT-1]` and clamping `u % nvert` at build time.

2. **No structural invariants across array elements.** The CSR
   must be dedup'd, symmetric, and self-loop-free.  Done in a
   C helper after drawing.

Still TODO: write schemas for types that stress the gaps our
synthetic tests haven't hit — embedded struct by value, fixed-
size arrays, packed/aligned layouts.  Those are the gaps that'll
matter for "does the API handle arbitrary foreign C code".

### `TESTS_CRASH` bucket — heterogeneous semantics

`tests/selftest/Makefile`'s `TESTS_CRASH` claims "fork should
catch crash, exit non-zero," but it's now mixing two distinct
classes of test:

1. **Runtime aborts inside a forked child** — `test_crash_*`,
   `test_binding_unresolved`, `test_binding_overflow`,
   `test_binding_width_mismatch`, `test_binding_use_at_no_iter`,
   `test_array_len_prefixed_overflow`.  Schema construction
   succeeds; the abort fires during `hegel_run_test` inside a
   child; the parent observes SIGABRT/SIGSEGV via `waitpid`.

2. **Schema-construction aborts before fork** —
   `test_arr_of_raw_int_abort`, `test_terminated_sentinel_abort`.
   `hegel_schema_*` constructor catches misuse and aborts in
   `main()` before any test runs.  Whole process dies (exit 134
   from raw SIGABRT), no fork involved.

Both happen to produce non-zero exit so the existing check
passes, but the bucket comment is wrong for class 2.

**Improvements, in order of payoff:**

1. **Add stderr-message checks** (cheapest, biggest value).  Like
   `TESTS_HEALTH` already does, grep stderr for an expected
   substring per test — e.g. `test_arr_of_raw_int_abort` should
   emit "must be HEGEL_USE(name) or HEGEL_CONST(N)".  Catches
   regressions where the abort fires for the *wrong reason* (an
   unrelated NULL check, etc.).

2. **Split into `TESTS_CRASH` and `TESTS_BUILD_ABORT`** when the
   build-abort population reaches ~5+.  Today there are 2 — not
   worth the Makefile boilerplate yet.
