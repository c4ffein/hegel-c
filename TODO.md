<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# TODO

Open items after the V0 schema API milestone. For what's already
done, see [README.md](README.md) and [docs/schema-api.md](docs/schema-api.md).

## Exploratory

### `HEGEL_INLINE(T, …)` — inline sub-struct as a positional field

The positional `HEGEL_STRUCT(T, …)` API currently can't express an
inlined sub-struct field.  Given

```c
typedef struct { uint8_t r, g, b; }   Color;
typedef struct { Color fg; Color bg; } Palette;
```

the user either has to spell every nested byte out manually
(`HEGEL_U8() ×6`) or drop to the low-level
`hegel_schema_struct_v` + `hegel__bind` form.  A `HEGEL_INLINE`
macro would close this gap:

```c
HEGEL_STRUCT (Palette,
    HEGEL_INLINE (Color, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()),
    HEGEL_INLINE (Color, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()));
```

**Sketch:**
- New layout kind `HEGEL_LAY_INLINE_STRUCT`, `slot0_size = sizeof (T)`,
  `slot0_align = _Alignof (T)`.
- Draw-time: when the layout pass reaches an inline struct entry,
  instead of calling `hegel__draw_struct` (which `calloc`s), use a
  new helper `hegel__draw_struct_into (slot, …)` that treats the
  pre-allocated slot (`parent + binding.offset`) as the struct root
  and iterates the sub-schema's bindings from there.  The machinery
  already exists inside `HEGEL_ARRAY_INLINE`'s element handler for
  struct elements — extract it into a shared helper.
- The outer `HEGEL_STRUCT` `sizeof (T)` assertion already catches
  mismatches; the inner `HEGEL_STRUCT`-style build of the sub-schema
  would also assert `sizeof (Color) == computed_total`, giving
  nested safety nets.
- Two API flavors worth shipping together:
  - `HEGEL_INLINE (T, …entries)` — nested form, builds a fresh
    sub-schema each call, composes recursively.  Default.
  - `HEGEL_INLINE_REF (T, pre_built_schema)` — escape hatch for
    reusing a schema built with runtime-computed parameters.
    Requires the usual `hegel_schema_ref` before each extra use.

Estimated scope: ~100–150 lines (new kind, new draw helper, two
macros, one selftest exercising `Palette`).  No fan-out to other
code paths.

**Why exploratory, not in the punch list yet**: the low-level
escape hatch already works for this case, and the most common
shared-schema pattern (same schema passed as the `inner` / `elem`
to `HEGEL_OPTIONAL` / `HEGEL_ARRAY` / `HEGEL_ARRAY_INLINE`) is
already ergonomic.  The only missing case is "same schema bound to
multiple direct fields of a parent struct," which is real but rare.

## Reuse multiple fields of a generator

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

**Follow-up to verify (low priority, not blocking):**
The fix calls `drop(tc)` between `catch_unwind` and
`resume_unwind` in `run_forked` (`rust-version/src/lib.rs`
~line 358).  My read is that it's safe because we're in
normal control flow at that point — `catch_unwind` has
suspended the unwind, and `resume_unwind` only restarts it
later.  The original code's "drop tc BEFORE panicking"
comment is honored because we're not yet panicking when we
drop.  Empirically, the 750/750 orphan stress test exercises
this exact code path on every overflow case and shows no
issue.  But sanity-check against hegeltest's `TestCase::drop`
semantics if anything weird shows up — the original concern
was a double-panic abort if drop talks to the hegel server
during unwind, which catch_unwind should defuse but is worth
spot-checking.

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
| `hegel_gen_one_of` (for scalars) | `HEGEL_ONE_OF_INT` / `_I64` / `_DOUBLE` (all via `HEGEL_SCH_ONE_OF_SCALAR`) |
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

Feature-parity proof: `tests/selftest/test_gen_schema_functional_combinators.c`
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
