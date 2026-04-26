<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# TODO

Open items after the V0 schema API milestone.  For what's already
done, see [README.md](README.md) and [docs/schema-api.md](docs/schema-api.md).

## Open exploration

- **FACET-like sugar on top of LET?**  The pointer+count pattern
  (`HEGEL_LET(n) + HEGEL_ARR_OF(USE(n), ...) + HEGEL_USE(n)`) could
  collapse into a single `HEGEL_ARRAY_LEN(name, ...)` macro that
  emits the triplet — populate a LET as a side effect.  Would only
  work at same level (no cross-scope sharing), and the explicit form
  is already ~3 lines.  Open question whether the surface-area win
  is worth a second way to spell the same thing.

## Binding system — deferred items

The HEGEL_BINDING / HEGEL_LET / HEGEL_USE / HEGEL_ARR_OF system
replaced the old facet mechanism.  Remaining gaps:

1. **HEGEL_USE inside HEGEL_VARIANT cases.**  Case-local bindings
   should work (variant bodies are drawn in the parent ctx today —
   plumbing is probably there, but untested).  Add a test with
   HEGEL_USE inside a HEGEL_CASE that references an outer LET.

2. **HEGEL_USE inside HEGEL_UNION cases.**  Same concern as VARIANT.
   Untested path.

3. **HEGEL_OPTIONAL wrapping a struct with HEGEL_USE.**  The
   optional's inner struct is drawn via hegel__draw_alloc, which
   threads parent_ctx — should work, but no test exercises it.

4. **Pointer-typed bindings.**  HEGEL_LET / HEGEL_USE handle
   int / i64 / u64 / float / double widths today (covered by
   `test_binding_widths.c`).  Pointer-valued bindings (e.g. cache
   an allocated array, USE its pointer in a sibling field) would
   be the natural extension — needs wider slot handling and a
   clear ownership story for who frees the pointed-at memory.

5. **Re-LET abort diagnostic could list the earlier LET's binding
   name.**  Today we have only the integer id.  Stashing names at
   BINDING declaration time would let the error print `HEGEL_LET(n)
   appears twice` instead of `id=7 appears twice`.  Nice-to-have.


## From FP

- lance combien de fois? parametrisable ou?
- HEGEL_SELF => prevenir des le debut, parametriser proba
- link hegel cpp now its out
- HEGEL protocol?
- dependency typo?
- bien preciser version de scotch
- verifier que les liens sont bons dans le README
--- => schema_api
- pas d espace avant les parentheses ouvrantes? un petit linter?
- THREE LAYERS => expliciter que HEGEL_ => shapes
  - preciser DRAW => shape ou val
- free pas en macro? bizarre... free schema vs shape => il a raison, j ai toujours pas clean mdr
- all public api vs all the public api(s) ???
- hegel_schema_t => expliquer que structures opaques, un peu moins expliciter mais linker vers code
  - preciser que runtime fait des checks, preciser lesquels?
- voir les FAST INT, mecanisme de promotion / acceleration => par le choix du compilo?.. comment gerer?
- dans schema api, facet introduites avant d etres definies?
- hegel union, untagged... => expliquer au bon moment?...
  - degager ce dont j ai plus besoin avec les facettes?...
- scotch src libscotch arch.h => pointer vers classe
  - classe a des pointeurs de fonction qui permettent de lire l union
  - arch_mesh_blabla pour voir comment c est instancier
- But ? => However
- rappeler les types dans la description des union?
- C++ => check si moyen de forcer pas de reour a la ligne entre + et +
- directement lier vers les array, int etc plus tot dans le doc? si besoin?
- lo et hi? comment gerer le min/max? documenter? je pense que bien gerer mais verifier code + expliciter
  - en particulier, comment je gere le u64? => expliciter que bien fait
- Text => review de mon cote
- metre des regles locales pour une struct? via un callbak? regenerer tant que pas A et B ou blabla?
- on a skip aux arrays => FACET pas clair! mieux expliquer :)

## rn

- **Filter retry + discardable span parity with `hegel-rust`.**  Both
  `HEGEL_FILTER_INT/I64/DOUBLE` draw paths — the originals in
  `hegel__draw_field` and the composition cases in
  `hegel__draw_integer_into` / `_fp_into` — do one draw attempt and
  call `hegel_assume(tc, 0)` on predicate failure, with no span
  around the attempt.  Reference hegel-rust
  (`inspiration/hegel-rust/src/generators/generators.rs::Filtered::do_draw`)
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

## `HEGEL_COPY` for shared-template instantiation

A schema-tree deep-copy: `HEGEL_COPY(s)` returns a fresh independent
tree with the same shape.  Use case: place the same schema template
at two sites without the manual `hegel_schema_ref` bookkeeping that
`HEGEL_INLINE_REF` currently requires — fixes the silent double-free
footgun documented in "Known bugs" below.  Cheap O(tree size) at
schema-build time, no per-case cost.

**Gotcha to verify before coding:** how `hegel_schema_self()` is
represented.  If it's a by-name sentinel resolved against the
enclosing root at draw time, copy is trivial.  If it stores a pointer
to a specific root, copy needs pointer rewrites during the tree walk.

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

**Risks:** large refactor; schema becomes mutable (fine in fork mode
and sequential suite mode, but we lose the immutability invariant);
every current shape-accessor API (`hegel_shape_tag`, `_array_len`,
`_is_some`, `_field`) needs re-examination.

**Disposition:** exploratory, decision deferred.  No concrete pain
forcing the rewrite right now — LET/USE bindings cover the
coherence cases that motivated this question, and the two-tier
schema/shape split is paying its keep.  Revisit if `HEGEL_COPY` or
named shape accessors get awkward to fit into the current model.

## High-leverage next work

### Point the library at real C code (Scotch, etc.)

**Status: two real-world Scotch demos done 2026-04-12.**

1. **`tests/irl/scotch/test_graph_part_schema.c`** — schema-API
   version of `SCOTCH_graphPart` testing.  Logical
   `(nvert, edges[], npart)` graph generated via
   `hegel_schema_struct` + `HEGEL_ARRAY_INLINE`, CSR built from
   it in a C helper, stronger assertions than the primitive-draw
   version (empty-partition check, load-balance bound).
   Real-world demo: "you can write property tests for an
   existing C library with this."

2. **`tests/irl/scotch/test_graph_order_shrink.c`** — reducer
   demo.  Re-discovers the
   [`hgraphOrderCp` off-by-`ordenum` bug](https://github.com/c4ffein/scotch/blob/ff403d445b36ee1723ff53d2478d368b21a3341f/REPORTS/BUG_REPORT.md)
   from a random schema, then shrinks to the theoretical minimum
   (3 vertices, 1 K₂ pair, 1 isolated vertex).  Wired into the
   Makefile as `TESTS_SHRINK` with parsed-output assertion that
   `nvert <= 5`.  Shrink demo: "integrated shrinking
   lands near the theoretical minimum on a real bug."  See
   [`docs/shrinking.md`](docs/shrinking.md) for the worked
   walkthrough.

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

### Review-readiness packet

| Item | Status |
|---|---|
| Fix `HEGEL_ARRAY_INLINE` fork-mode orphan leak | done — see "Known bugs" below |
| `HEGEL_INLINE` / `HEGEL_INLINE_REF` — inline-by-value sub-struct | done — `test_schema_inline_struct.c`, shared helper `hegel__draw_struct_into_slot`, `HEGEL_SHAPE_GET` now recurses through nested structs |
| Real-world demo: schema-API on actual Scotch | done — `test_graph_part_schema.c` |
| Shrinker demo: reducer on a real Scotch bug | done — `test_graph_order_shrink.c` + `docs/shrinking.md` |
| Health-check failure path coverage in CI | done — `TESTS_HEALTH` selftests (filter_too_much, large_base_example, single + suite versions) |
| Embedded-systems demo | **TODO** — pick a target (Modbus / ring buffer / MQTT-SN / reviewer's choice), build 2–3 schemas, host-compiled |
| `docs/review-framing.md` (one-page packet) | **TODO** — per-reviewer specific questions |
| `HEGEL_SELF` rename / `OPTIONAL_SELF` alias | deferred — cosmetic, not a rejection issue |
| `hegel_draw_regex` fullmatch fix | deferred — existing WARNING in `hegel_gen.h` is sufficient for review; defer to reviewer feedback on the right fix |

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

### Real-world README / quickstart

`docs/schema-api.md` is the reference, `docs/patterns.md` is the
catalog, but there's no "here's your first 5-minute test" walkthrough.
For a library that might get adopted by strangers, a focused
getting-started doc is worth 30 minutes of writing time.

### Missing from Go

1. Named "batteries-included" generators. Go ships Binary, Dates, Datetimes, Emails, URLs, Domains, IPAddresses().IPv4()/IPv6().
C has none. Binary(min, max) is the biggest one for a C library — people test serializers, binary formats, wire protocols, MPI
buffers. You have hegel_schema_text ([a-z] only) but no raw-byte generator. If hegel-c wants "official" status, parity on at
least binary is table stakes.

2. Float control. Go has .Min().Max().ExcludeMin().ExcludeMax().AllowNaN(false).AllowInfinity(false). C has
hegel_schema_float_range(lo, hi) — inclusive only, no NaN/Inf opt-out. For numerical code (Scotch-adjacent, scientific C), NaN
handling is where bugs live. This is a real gap, not an aesthetic one.

3. Options beyond n. Go has WithTestCases, SuppressHealthCheck(FilterTooMuch | TooSlow | ...). C has hegel_run_test_n and
nothing else. The filter health check in particular is load-bearing — if a user's HEGEL_FILTER_INT is too strict, there's
currently no escape hatch.

4. FromRegex(pattern, fullmatch). Go exposes the fullmatch flag. You already know about the C footgun (hegel_draw_regex is
contains-match), it's in TODO and memory. Go solved it by just plumbing the flag through.

## API polish

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

## Known bugs / gotchas

### Fork-mode orphan leak (FIXED 2026-04-12)

**Symptom (now fixed):** schemas using `HEGEL_ARRAY_INLINE`, and in
principle any test where the draw sequence was long enough for
hegel's engine to discard mid-generation, caused `hegel_run_test_n`
to silently spawn extra orphan fork children. Roughly 10% of forks
ended up reparented to init, ran the test body to completion with
garbage draw values, and silently swallowed any assertion failures.

**Root cause:** `tc.draw()` inside `parent_serve` can panic with
hegeltest's `__HEGEL_STOP_TEST` sentinel when the engine decides to
abort a test case mid-generation. The original `run_forked` let
that panic propagate up directly, never reaching the `waitpid`
line. The child stayed alive, blocked on a pipe read that nobody
was answering. When the main process eventually exited, the
child's pipe got EOF, the child-side `pipe_read_exact` silently
returned `false`, the draw functions ignored that return and
yielded zeros, and the child finished running its test body with
garbage inputs as an orphan.

**Fix (in `rust-version/src/lib.rs`):**

1. `run_forked` now wraps `parent_serve` in `catch_unwind`,
   unconditionally closes the parent's pipe ends and `waitpid`s
   the child, then drops `tc` and re-raises the panic via
   `resume_unwind`. The child is always reaped before any panic
   propagates up to hegel's engine.

2. The child-side `hegel_draw_*` functions (`int`, `i64`, `u64`,
   `usize`, `float`, `double`, `text`, `regex`) now check
   `pipe_read_exact`'s return value and call `child_abandoned()`
   (= `_exit(0)`) on read failure, instead of silently returning
   zeros from a dead pipe.

**Verification:**
- 1000 / 1000 cases across 20 runs of the minimal reproducer:
  0 orphans, 0 out-of-range draws.
- All 36 selftest, 19 from-hegel-rust, and 4 Scotch tests pass
  (selftest grew by 4 `TESTS_HEALTH` regression tests covering
  the `parent_serve` panic propagation paths; Scotch grew by 1
  `TESTS_SHRINK` reducer demo).
- Reproducer kept at
  `tests/irl/scotch/test_array_inline_orphan_repro.c` as a
  hand-runnable regression demo.

### `HEGEL_SELF()` as an array element — needs re-verification

Pre-LET/USE investigation noted that `HEGEL_SELF()` nested inside
an array's element schema (e.g. `HEGEL_ARR_OF(struct_w_self, ...)`)
silently produced zero-initialized entries — `hegel__resolve_self`
didn't descend into the array's `elem` to wire up the self target.
The original framing referenced `HEGEL_SCH_SUBSCHEMA` (a facet-era
node kind that no longer exists), so the explanation is partly
stale, but the underlying recursion gap may still apply.  Action:
write a targeted test (`HEGEL_ARR_OF` of a struct that uses
`HEGEL_SELF()`) and either confirm it works under LET/USE or
patch `hegel__resolve_self` to descend into `array_def.elem`.

### `HEGEL_INLINE_REF` refcount footgun

Sharing a pre-built struct schema across two `HEGEL_INLINE_REF`
slots requires the user to call `hegel_schema_ref` once before the
second use; forgetting it is a silent double-free at
`hegel_schema_free` time (visible under ASan, invisible otherwise).
Matches the rest of the API's transfer-on-pass convention, so it's
not a bug per se — but it's a gap that `HEGEL_COPY` would close
(see its own section above).  Until then: add an ASan-caught
deliberate-misuse selftest that pins the current behavior so a
future accidental fix doesn't silently change it.

### `hegel_draw_regex` fullmatch semantics

`hegel_draw_regex` wraps hegeltest's `from_regex()` without
configuring `.fullmatch()`. That means the generator produces
strings that **contain a match** for the pattern, not strings that
**are** a match.

For permissive patterns that match the empty string (like
`[a-z]{0,8}`), every possible string "contains a match," so the
generator returns arbitrary bytes including control characters,
quotes, and non-ASCII.

**Fix:** expose `.fullmatch()` option in the C API. Either add a
new function `hegel_draw_regex_fullmatch` or add a bool parameter.
Requires FFI changes in `hegel_c.h` and `rust-version/src/lib.rs`.

**Workaround until then:** don't use `hegel_draw_regex` for
character-class constraints. Use char-by-char drawing with
`hegel_draw_int(tc, 'a', 'z')` instead. The schema API's text
generator already does this.

Discovered during the JSON round-trip test — hegel found the bug
itself by producing a `"` character from `[a-z]{0,8}`.

## Cleanup

### Extract or generalize `graph_gen.h` / `scotch_helpers.h`

Currently these live in `tests/irl/scotch/` as test-harness
helpers — CSR graph builders, strategy string generators, Scotch
error-code decoders.  They're Scotch-specific in their current
form, which is why they're sitting in the test harness rather
than in hegel-c proper.

But the *patterns* they encode are general:

- CSR construction from a logical `(nvert, edges[])` list is a
  reusable pattern anyone wiring a graph-library test would
  need — maps cleanly to a `hegel_gen.h`-level helper that
  takes a schema-drawn logical graph and produces a canonical
  CSR.
- The strategy string generator is a focused example of
  building a valid DSL string from a schema — useful as a
  pattern reference even if the specific DSL is Scotch's.
- Symmetry / dedup / self-loop-free post-processing is a
  recurring "structural invariant across array elements"
  pattern that came up as a schema-layer gap in the V0
  milestone.

Decision to make: extract into hegel-c as library helpers, keep
them Scotch-specific but document the patterns in
`docs/patterns.md`, or leave alone entirely.  Leaning toward
documenting the patterns first (cheap) and only extracting once
a second IRL target hits the same needs and we can see the real
shape of the abstraction.

Was previously listed as a "design decision" in the README; it's
really an open cleanup task, not a settled choice.

### Relationship between the legacy `hegel_gen_*` combinator API and the schema API

**Feature parity is complete.** Everything the legacy
`hegel_gen_*` API provides is now available at the schema layer
(`hegel_gen.h`), in typed form with automatic ownership tracking
and span emission:

| Legacy | Schema equivalent |
|---|---|
| `hegel_gen_int/i64/u64/float/double/bool` | `hegel_schema_i8..u64`, `_float`, `_double`, `HEGEL_BOOL` |
| `hegel_gen_text` | `hegel_schema_text` (char-by-char, no regex gotcha) |
| `hegel_gen_regex` | `hegel_schema_regex` / `HEGEL_REGEX` (same footgun; see below) |
| `hegel_gen_one_of` (for scalars) | `HEGEL_ONE_OF` (via `HEGEL_SCH_ONE_OF_SCALAR`) |
| `hegel_gen_one_of` (for structs) | `HEGEL_ONE_OF_STRUCT` |
| `hegel_gen_sampled_from(N)` | `hegel_schema_int_range(0, N-1)` |
| `hegel_gen_optional` (of ptr) | `HEGEL_OPTIONAL` |
| `hegel_gen_optional` (of scalar) | `HEGEL_OPTIONAL(T, f, hegel_schema_int_range(...))` — produces a nullable `int *` |
| `hegel_gen_map_int/i64/double` | `HEGEL_MAP_INT/I64/DOUBLE` |
| `hegel_gen_filter_int/i64/double` | `HEGEL_FILTER_INT/I64/DOUBLE` |
| `hegel_gen_flat_map_int/i64/double` | `HEGEL_FLAT_MAP_INT/I64/DOUBLE` |
| `hegel_gen_draw_list_int/i64/u64/float/double` | `HEGEL_ARRAY` (more capable — nested, struct elems, etc.) |

The only remaining "legacy-only" surface is the **standalone draw
functions** (`hegel_draw_int(tc, lo, hi)` etc.) that belong to the
primitive layer (`hegel_c.h`), not to the combinator layer. Those
are a different abstraction level and stay regardless of what
happens to the combinators.

**Regex footgun note:** both `hegel_gen_regex` (legacy) and
`hegel_schema_regex` (new) wrap hegeltest's `from_regex()` without
configuring `.fullmatch()`, so for permissive patterns they produce
strings that *contain* a match rather than *are* a match. See the
"Known bugs" section below for the FFI-level fix.

**Remaining disposition decision:** the legacy `hegel_gen_*`
combinator API is now fully redundant. Safe to deprecate and
eventually remove once existing tests (`from-hegel-rust`, etc.)
that use it get migrated to the schema API. Not urgent for V0 —
the two systems coexist without conflict. Migration is mechanical.

Feature-parity proof: `tests/selftest/test_schema_functional_combinators.c`
has 7 sub-tests covering optional-int pointer, map/filter/flat_map
for int/i64/double, one-of-scalar (small-OR-large distributions),
bool, and regex.

## Infrastructure

### Pool Hegel server across test binaries

`hegeltest` already pools the Hegel server within a single process,
but each test binary pays the ~1s startup cost separately. The
suite API (`hegel_suite_*`) amortizes within a binary; a
multi-binary pooling mechanism would amortize across binaries.

Probably requires a long-lived daemon or shared socket.

### Parallel test execution

Currently tests run sequentially. For suites with dozens of tests,
parallelization (across binaries or across cases within a binary)
would meaningfully cut CI time.

### Port more hegel-rust tests

See `tests/from-hegel-rust/manifest.md`. Remaining ones need
features we don't have (`exclude_min`, NaN/inf) or are Rust-specific.

### Direct CI coverage for the orphan-leak fix

The four `TESTS_HEALTH` selftests added 2026-04-12 cover the
`parent_serve` panic propagation paths (filter_too_much and
large_base_example), which is *indirect* coverage for the orphan
fix — a regression that breaks the `catch_unwind` would cascade
into health-check test failures or hangs.  But there is no test
that *directly* asserts "no orphan processes appeared during
this run."

The hand-runnable `tests/irl/scotch/test_array_inline_orphan_repro.c`
does the orphan check via `grep -c 'ppid=1 '` on stderr after a
`sleep 1`, but it's not wired into any Makefile target.  Future
work: bash-wrap the repro into a Makefile target that asserts
zero orphans, add to selftest CI.  Cheap (~30 min) and would
catch the most subtle regressions.

## TODO
- [ ] Port more hegel-rust tests — see `tests/from-hegel-rust/manifest.md`. Remaining need features (exclude_min, NaN/inf) or are Rust-specific.
- [ ] `hegel_target(tc, value, label)` — property-directed testing. Not in hegeltest 0.1.18 or 0.4.3, blocked upstream.
- [ ] Pool hegel server across test binaries — hegeltest pools within a process, but separate binaries each pay ~1s. Suite API partially addresses this.
- [ ] Parallel test execution
- [ ] Verify Hegel's database replay of failing cases across runs with fork mode
- [ ] More Scotch IRL tests — strategy string fuzzing against real parser, mesh partitioning
- [ ] More IRL targets beyond Scotch in `tests/irl/`
- [ ] Real C implementation (pure C wire protocol, no Rust bridge)
  - [ ] Compare with Rust bridge using PBT
- [ ] Clean up `graph_gen.h` / `scotch_helpers.h` — currently in the Scotch test harness, contain general patterns (CSR builders, strategy generators) worth extracting or generalizing. See `TODO.md`.
