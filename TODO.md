<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# TODO

Open items after the V0 schema API milestone. For what's already
done, see [README.md](README.md) and [docs/schema-api.md](docs/schema-api.md).

## High-leverage next work

### Point the library at real C code (Scotch, etc.)

The schema API has been validated against hand-crafted test types
(trees, tagged unions, matrices). What's still missing: generating
data for a real C library's types and running its real functions.

`inspiration/scotch/` is in the tree. Writing a schema for something
like `SCOTCH_Graph` and testing one of its functions would prove
that the API is adequate for existing C codebases that weren't
designed around our conventions. It will likely expose gaps —
things like embedded structs by value, fixed-size arrays, arbitrary
alignment requirements — that our synthetic tests don't hit.

This is the "does it survive contact with the real world" test.
Next-session priority.

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

## API polish

### Rename / clarify `HEGEL_SELF`

`HEGEL_SELF(T, f)` silently expands to
`hegel_schema_optional_ptr_at(offsetof(T,f), hegel_schema_self())`,
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

### Stray files

- Check for leftover `temp` file at repo root — scratch pad from
  an early session, not used anywhere.

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
