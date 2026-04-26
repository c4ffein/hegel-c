<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Schema API

Reference for `hegel_gen.h` — the typed schema/shape system for
property-based testing over C data structures.

## Overview

A hegel-c property test typically uses one of two draw shapes.

**Scalars — direct by value.** For a primitive you want right now:

<!-- /include tests/docs/test_doc_schema_api.c:26-31 -->
```c
static void opener_primitive (hegel_testcase * tc)
{
  int x = HEGEL_DRAW_INT (0, 100);
  int y = HEGEL_DRAW_INT (0, 100);
  HEGEL_ASSERT (my_function (x, y) >= 0, "x=%d y=%d", x, y);
}
```
<!-- /endinclude -->

**Composed input types — build a schema once, draw instances per case.**
When your tested function takes a struct, a tagged union, a
recursive tree, or an array of polymorphic pointers:

<!-- /include tests/docs/test_doc_schema_api.c:35-46 -->
```c
typedef struct { int x; int y; char * name; } Thing;
static hegel_schema_t thing_schema;

static void opener_composed (hegel_testcase * tc)
{
  Thing *             t;
  hegel_shape *       sh = HEGEL_DRAW (&t, thing_schema);
  HEGEL_ASSERT (t != NULL, "alloc");
  HEGEL_ASSERT (t->x >= 0 && t->y >= 0,
                "x=%d y=%d name=%s", t->x, t->y, t->name);
  hegel_shape_free (sh);
}
```
<!-- /endinclude -->

The schema is built once in `main()` via `HEGEL_STRUCT`:

<!-- /include tests/docs/test_doc_schema_api.c:196-199 -->
```c
  thing_schema = HEGEL_STRUCT (Thing,
      HEGEL_INT  (0, 100),
      HEGEL_INT  (0, 100),
      HEGEL_TEXT (1, 20));
```
<!-- /endinclude -->

### The three layers under the macros

`HEGEL_DRAW_INT` and `HEGEL_DRAW` are ergonomic macros — they
capture `tc` from enclosing scope and dispatch to three layers of
underlying API that you can also call directly if you need to:

1. **Primitive draws** (`hegel_c.h`) — `hegel_draw_int`, `_i64`,
   `_u64`, `_float`, `_double`, `_text`, `_regex`. These consume
   directly from the Hegel byte stream.  `HEGEL_DRAW_INT(0, 10)`
   forwards verbatim to `hegel_draw_int(tc, 0, 10)`.

2. **Schema constructors** (`hegel_gen.h`) — functions and macros
   that build a `hegel_schema_t` value describing what to generate.
   `HEGEL_STRUCT`, `HEGEL_ARR_OF`, `HEGEL_OPTIONAL`, and the scalar
   field macros `HEGEL_INT` / `HEGEL_U8` / etc.  The schema is a
   pure value you reuse across many draws.

3. **Schema-driven draws** (`hegel_gen.h`) — `HEGEL_DRAW(&addr,
   sch)` for any kind of composed schema, plus
   `hegel_schema_draw(tc, sch, (void **)&ptr)` as the older
   struct-only form.  These walk the schema, consume the byte
   stream recursively, allocate and fill the value memory, and
   return a `hegel_shape *` that owns the whole tree.

The scalar by-value family (`HEGEL_DRAW_INT` / `_I64` / `_U64` /
`_DOUBLE` / `_FLOAT` / `_BOOL`) is sugar for layer 1 — no schema
needed.  Everything else routes through layer 3 with a schema
built from layer 2.

## The three layers

Internally the system has three parallel data structures:

1. **Schema tree** (`hegel_schema_t`) — a description you build once
   with `hegel_schema_*` constructors. Lives for the whole test run.
2. **Shape tree** (`hegel_shape *`) — ephemeral metadata describing
   what was actually drawn on each test case: array lengths, variant
   tags, which optional fields were present. Built by
   `hegel_schema_draw`, freed by `hegel_shape_free`.
3. **Value memory** — the actual C struct your tested function
   consumes. Allocated by `hegel_schema_draw`, owned by the shape
   tree, freed when the shape tree is freed.

You write code against layer 3 (your struct), and the framework
manages layers 1 and 2 for you. You rarely touch the shape tree
unless you need metadata that doesn't live in your struct (e.g.,
the variant tag in an untagged-union pattern — see
[patterns.md](patterns.md)).

## The wrapper type

`hegel_schema_t` is a thin wrapper around a pointer to the internal
node struct:

<!-- /include hegel_gen.h:147-147 -->
```c
** Every HEGEL_<kind> macro produces a `hegel_schema_t` — a reference
```
<!-- /endinclude -->

Zero runtime cost — `sizeof(hegel_schema_t) == sizeof(void*)`, and
the compiler optimizes the struct wrapping away. Distinct from
`hegel_schema *` at the type level, so you can't accidentally pass
random pointers to schema functions. All public API is typed in
terms of `hegel_schema_t`.

Users should never need to touch `_raw` — it's the library's
internal handle.

## How HEGEL_STRUCT derives offsets

`HEGEL_STRUCT(T, ...)` does not take field names. It takes a struct
type and a **positional** list of field-schema descriptions — one
per field, in declaration order.

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct { int x; int y; char *name; } Thing;

hegel_schema_t s = HEGEL_STRUCT (Thing,
    HEGEL_INT  (0, 100),
    HEGEL_INT  (),
    HEGEL_TEXT (1, 20));
```

At runtime, `HEGEL_STRUCT` walks the entries, computes each one's
byte offset using the same layout rules the C compiler uses (each
field starts at the smallest offset ≥ `previous_end` aligned to the
field's alignment, total rounds up to `alignof(T)`), builds the
internal schema, and asserts `sizeof(T) == computed_total`. If
your generator list doesn't match the struct fields in order and
type, the assertion fires immediately.

Each `HEGEL_*` macro knows its own target type:
- `HEGEL_INT` targets `int` (sizeof=`sizeof(int)`, alignof=same)
- `HEGEL_I8` / `I16` / `I32` / `I64` / `U8` / … targets the fixed-width
  integer type
- `HEGEL_FLOAT` / `HEGEL_DOUBLE` targets `float` / `double`
- `HEGEL_TEXT` / `HEGEL_OPTIONAL` / `HEGEL_SELF` / `HEGEL_REGEX`
  target pointer-sized slots
- `HEGEL_ARRAY_INLINE` occupies **two** consecutive slots (a
  `void *` pointer and an `int` count), matching the idiom
  `void *ptr; int n_items;`
- `HEGEL_ARR_OF` occupies **one** pointer slot; store the length
  (if you want it) via a sibling `HEGEL_LET` + `HEGEL_USE` pair —
  slot positions are independent since LET is non-positional
- `HEGEL_LET` occupies **zero** slots (non-positional side effect
  during draw)
- `HEGEL_UNION` / `HEGEL_UNION_UNTAGGED` / `HEGEL_VARIANT` occupy
  a "cluster" slot whose size and alignment are derived from the
  cases — tag plus internal body layout — matching the idiom
  `int tag; union { ... } u;`

### Standalone use of `HEGEL_UNION` / `HEGEL_VARIANT`

`HEGEL_UNION` normally appears inside a `HEGEL_STRUCT` and behaves
like one cluster slot. But a union can also be used standalone as
the element type of an `ARRAY_INLINE` — each array slot is then a
self-contained tag+body block. To unwrap the layout entry and get
a raw `hegel_schema_t`, use `hegel_schema_of()`:

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
hegel_schema_t shape_union = hegel_schema_of (HEGEL_UNION (
    HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
    HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                HEGEL_DOUBLE (0.1, 100.0))));

hegel_schema_t gallery = HEGEL_STRUCT (Gallery,
    HEGEL_ARRAY_INLINE (shape_union, sizeof (Shape), 1, 6));
```

## Why positional macros instead of reflection

[hegel-cpp](https://github.com/hegeldev/hegel-cpp) can spell
nested-struct generators as `default_generator<Line>()` with
zero annotations. That works because C++ has **compile-time
reflection**: template metaprogramming enumerates a type's
fields, reads each one's static type, and recursively dispatches
to the right sub-generator — all resolved at build time, the
compiler walking the type tree for you.

**C has none of that.** There is no standard mechanism to ask
"what are the fields of this struct?" at compile time or at
runtime. Any C-level schema system has to get the field list
from the user directly, typically by spelling the fields out in
a macro and trusting the user to keep them in sync with the
struct definition.

`HEGEL_STRUCT` is that trade, executed as carefully as C allows:
the list is **positional** (one generator per field, in
declaration order); each `HEGEL_INT` / `HEGEL_DOUBLE` /
`HEGEL_TEXT` / etc. knows the size and alignment of its target
field; the macro walks the entries using the same layout rules
the C compiler itself uses; and the final
`sizeof(T) == computed_total` assertion is the checksum. Reorder
a field, change a type, or pick the wrong generator and the
totals diverge and the assert fires before any test runs.

These are different tradeoffs. Reflection-based
systems push structural knowledge into the compiler; we push it
into macros and runtime, which is the C way.

## Primitive constructors

Every integer type has a **full-range** version (no args) and a
**range-constrained** version:

<!-- /include hegel_gen.h:508-509 -->
```c
** tag lives inside each pointed-to struct (not in a wrapper).  The
** generator picks a variant per element, allocates it, stores the
```
<!-- /endinclude -->

Same shape for `i16`, `i32`, `i64`, `int`, `long`, `u8`, `u16`, `u32`,
`u64` — each with a no-arg full-range version and a `_range` variant.

Floats:

<!-- /include hegel_gen.h:533-536 -->
```c
** (normal ownership semantics).  The callback's ctx must outlive
** the combinator. */

hegel_schema_t hegel_schema_map_int (
```
<!-- /endinclude -->

Text (currently char-by-char from `[a-z]`, see TODO.md for the
`hegel_draw_regex` fullmatch fix):

<!-- /include hegel_gen.h:542-542 -->
```c
    hegel_schema_t source,
```
<!-- /endinclude -->

These all produce "field schemas" you plug into a struct via the
convenience macros (next section).

## Scalar field macros

Each scalar macro has a full-range form (no args) and a constrained
form (`lo`, `hi`):

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
HEGEL_INT   ()                    /* full int range  */
HEGEL_INT   (-1000, 1000)         /* constrained     */
HEGEL_U8    ()                    /* [0, 255]        */
HEGEL_I16   (-400, 850)
HEGEL_U32   ()
HEGEL_I64   ()
HEGEL_FLOAT (0.0, 5.0)
HEGEL_DOUBLE (-90.0, 90.0)
HEGEL_TEXT  (0, 8)                /* [a-z]{0..8}, always present */
```

The macro picks between the full-range and range-constrained form
by argument count (via `__VA_OPT__`).

## Struct constructor

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
hegel_schema_t s = HEGEL_STRUCT (MyStruct,
    HEGEL_INT  (0, 100),
    HEGEL_INT  (0, 100),
    HEGEL_TEXT (1, 20));
```

Variadic. The macro appends a sentinel internally; **do not write
a trailing NULL**. The top-level schema you pass to
`hegel_schema_draw` must be a struct.

## Composite schemas

### `HEGEL_OPTIONAL` — 50/50 nullable pointer

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
HEGEL_OPTIONAL (inner_schema)
```

On each draw, flips a coin. If true, draws `inner_schema` and stores
the pointer in the corresponding field. If false, leaves the field
NULL. Use for optional strings (inner = `hegel_schema_text(...)`)
or optional sub-structs. Fits a single pointer-sized slot in the
enclosing `HEGEL_STRUCT`.

### `HEGEL_INLINE` / `HEGEL_INLINE_REF` — inline-by-value sub-struct

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct { uint8_t r, g, b; }   RGB;
typedef struct { RGB fg; RGB bg; }    Palette;

/* Fresh form: build a sub-schema in place, once per call. */
palette_schema = HEGEL_STRUCT (Palette,
    HEGEL_INLINE (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()),
    HEGEL_INLINE (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()));

/* Reuse form: share one pre-built sub-schema across multiple
** parent fields.  Bump the refcount once per extra use. */
rgb_schema = HEGEL_STRUCT (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ());
hegel_schema_ref (rgb_schema);
palette_schema = HEGEL_STRUCT (Palette,
    HEGEL_INLINE_REF (RGB, rgb_schema),
    HEGEL_INLINE_REF (RGB, rgb_schema));
```

Lays out `sizeof(T)` bytes aligned to `_Alignof(T)` at the parent
slot; at draw time, the sub-struct's fields are drawn directly into
that region, no separate allocation. Nests to any depth — each
inner `HEGEL_INLINE` asserts its own `sizeof(T)` match at schema
build time, so a layout mismatch fires at init with a clear
diagnostic before any draw happens.

`HEGEL_INLINE` consumes a fresh entry list each call. `HEGEL_INLINE_REF`
takes a pre-built struct schema and asserts at build time that its
kind is `HEGEL_SCH_STRUCT` and its size matches `sizeof(T)`. Ownership
follows the usual "transfer on pass" rule: one reference per use,
call `hegel_schema_ref` once per extra use.

`HEGEL_INLINE` coexists with `HEGEL_OPTIONAL(pointer-to-struct)`,
`HEGEL_ARR_OF`, etc. in the same parent — they occupy their own slots
and dispatch through independent schema kinds at draw time.

`HEGEL_SHAPE_GET(sh, Parent, outer.leaf)` resolves through any level
of inline nesting and returns the leaf scalar shape.

### `HEGEL_SELF` — optional recursive pointer

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
HEGEL_SELF ()    /* Tree *left; — optional recursive */
```

Shorthand for `HEGEL_OPTIONAL (hegel_schema_self ())`. Must be used
inside a `HEGEL_STRUCT` that declares the type. The field must be
a pointer to the enclosing struct type. Automatically optional
(50/50 NULL) because recursive chains need termination — unbounded
recursion is capped at `HEGEL_DEFAULT_MAX_DEPTH = 50` (see
[Recursion depth](#recursion-depth) below for why 50).

### `HEGEL_BINDING` / `HEGEL_LET` / `HEGEL_USE` — let-bindings

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct { int n; int copy_of_n; } Pair;

HEGEL_BINDING (n);

pair_schema = HEGEL_STRUCT (Pair,
    HEGEL_LET (n, HEGEL_INT (2, 5)),   /* non-positional: draws n */
    HEGEL_USE (n),                      /* field 0: int n */
    HEGEL_USE (n));                     /* field 1: int copy_of_n */
```

`HEGEL_BINDING(name)` declares a binding identifier using a
compile-time integer (`enum { name = __COUNTER__ }`). Typos are
undefined-identifier errors at compile time; there is no string
lookup.

`HEGEL_LET(name, inner)` is **non-positional**: it runs as a side
effect during draw — draws from `inner`, caches the value under
`name` in the enclosing struct's draw ctx — but does not consume a
parent-struct slot.

`HEGEL_USE(name)` reads the cached value and writes it to the
current slot (as a layout entry, `int` slot) or produces a value
for a parameter position (as the length of `HEGEL_ARR_OF`). USE
walks the scope chain outward, so a USE in an inner struct can
resolve a LET in any enclosing struct. Unresolved → `hegel_abort`
with the binding id and the available bindings at that point.

**Scoping:** each `HEGEL_STRUCT` (and `HEGEL_INLINE`, and each
element struct of an ARR_OF) gets its own draw ctx linked to its
parent. LETs are scoped to the ctx that declared them. Same-ID
LETs in different ctxs do not share — each struct instance draws
its own value. See `test_binding_jagged_2d.c` for the full
demonstration (n groups, each with its own m_i elements).

**Non-positional means layout order is independent of dependency
order.** A struct `{ int *items; int tag; int n; }` can be built
with LET anywhere in the entry list and USE for `n` at the last
layout position — the value has already been drawn by the time the
USE slot is reached. See `test_schema_facets_nonadjacent.c`.

**Draw order is still the entry-list order.** LET is
non-positional w.r.t. *layout* (consumes no slot), but it draws
**where it appears in the entry list**, interleaved with other
entries. `HEGEL_LET` at index 3 draws after entries 0, 1, 2 and
before entries 4, 5. This matters for shrinking: each draw is
independent in the byte stream, so the shrinker can simplify
unrelated fields independently. Don't feel forced to move all
LETs to the top — put each one just before the first USE that
needs it, and the shrinker sees cleaner locality.

**One LET per id per scope.** Calling `HEGEL_LET(n, ...)` twice
in the same struct's entry list is a hard abort — almost always
a typo'd name colliding with an earlier binding. Use a separate
`HEGEL_BINDING` declaration for a second value. Nested structs
*can* re-LET the same id (inner shadows outer); that's per-scope,
not per-process.

LET's inner schema may be a scalar INTEGER (width 4 or 8), a FLOAT
(single or double), or a composed schema producing one of those
(`HEGEL_MAP_INT` / `_I64` / `_DOUBLE` and friends). The matching
read-back uses the corresponding `HEGEL_USE_*` variant:

| LET inner                       | Slot type   | USE variant         |
|---------------------------------|-------------|---------------------|
| `HEGEL_INT(...)` (width 4)      | `int`       | `HEGEL_USE`         |
| `HEGEL_I64(...)`                | `int64_t`   | `HEGEL_USE_I64`     |
| `HEGEL_U64(...)`                | `uint64_t`  | `HEGEL_USE_U64`     |
| `HEGEL_FLOAT(...)`              | `float`     | `HEGEL_USE_FLOAT`   |
| `HEGEL_DOUBLE(...)`             | `double`    | `HEGEL_USE_DOUBLE`  |

Width / sign / float-ness mismatch between LET and USE is a hard
abort at draw time — silent truncation would defeat the point.
`HEGEL_ARR_OF` length still requires an int-width schema; the wider
USE variants only make sense as struct slots.

### `HEGEL_ARR_OF` — array with schema-valued length

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct { int n; int *items; } Bag;

HEGEL_BINDING (n);
bag_schema = HEGEL_STRUCT (Bag,
    HEGEL_LET    (n, HEGEL_INT (0, 10)),
    HEGEL_USE    (n),
    HEGEL_ARR_OF (HEGEL_USE (n), HEGEL_INT (0, 100)));
```

`HEGEL_ARR_OF(length_schema, elem_schema)` occupies a single
pointer slot in the parent struct. The length is evaluated at
draw time from `length_schema` — `HEGEL_USE(n)` to reuse a bound
value, or `HEGEL_INT(lo, hi)` for a drawn length. The length is
not stored in a field by ARR_OF itself; pair it with a
`HEGEL_USE(n)` slot elsewhere in the struct if you need it.

Element kinds currently supported:

- `HEGEL_INT(...)` and other int-producing schemas
  (`HEGEL_MAP_INT`, `HEGEL_FILTER_INT`, `HEGEL_FLAT_MAP_INT`)
- Struct schemas (each element separately allocated; array stores
  `void *` pointers)
- `HEGEL_OPTIONAL_PTR(...)` — each slot is NULL or a drawn inner
- `HEGEL_SELF()` — optional pointer to enclosing struct (n-ary
  recursive trees)
- `HEGEL_ONE_OF_STRUCT(...)` — polymorphic pointer array

**Per-instance scoping:** in a `HEGEL_ARR_OF(length, struct_elem)`,
each element struct is drawn with its own ctx chained back to the
parent. LETs inside the element schema are per-element; USEs can
still reach outer bindings via the chain walk.

### `HEGEL_ARRAY_INLINE` — contiguous array of inline structs

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct { Point *points; int n_points; } Path;

HEGEL_STRUCT (Path,
    HEGEL_ARRAY_INLINE (point_schema, sizeof (Point), 1, 8));
```

Two-slot layout (pointer + count) where elements are laid out in
one contiguous buffer (stride = `sizeof(elem)`) rather than each
allocated separately. The `elem` schema must be a struct or union.
Unlike `HEGEL_ARR_OF`, the count is written directly to the next
slot in the parent struct — the user's fields must be pointer-then-
int and adjacent.

### `HEGEL_UNION` — tagged union with tag field in the struct

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct {
  int tag;
  union {
    struct { double radius; } circle;
    struct { double w, h; } rect;
  } u;
} Shape;

HEGEL_STRUCT (Shape,
    HEGEL_UNION (
        HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
        HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                    HEGEL_DOUBLE (0.1, 100.0))));
```

Occupies a single "cluster" slot: the int tag followed by the union
body (sized/aligned to the widest case). Each case is a positional
list of field schemas, laid out starting at the union body's base.
No trailing `NULL` in the cases.

### `HEGEL_UNION_UNTAGGED` — tag lives in shape tree only

Same as `HEGEL_UNION` but **writes no tag** — the cluster is just
the union body. Read the chosen variant from the shape tree via
`hegel_shape_tag()`.

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
HEGEL_UNION_UNTAGGED (
    HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
    HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                HEGEL_DOUBLE (0.1, 100.0)))
```

### `HEGEL_VARIANT` — tag + pointer to separately allocated struct

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct { int tag; void *value; } ShapeVar;

HEGEL_STRUCT (ShapeVar,
    HEGEL_VARIANT (circle_schema, rect_schema));
```

Occupies a two-field cluster: int tag + void pointer. The chosen
variant's struct is allocated separately; the pointer lives at the
cluster's second slot.

### `HEGEL_MAP_INT` / `HEGEL_FILTER_INT` / `HEGEL_FLAT_MAP_INT` — functional combinators

The schema API handles *structural* composition (struct fields,
arrays, unions). For *functional* composition — transforming drawn
values, filtering, dependent generation — use these combinators.
They each occupy a single slot sized for their target type.

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
/* Map: draw an int, transform through fn, store result. */
int square_fn (int x, void *ctx) { return x * x; }
HEGEL_MAP_INT (hegel_schema_int_range (0, 100), square_fn, NULL)
/* field is always a perfect square in [0, 10000] */

/* Filter: draw an int, keep only those matching the predicate. */
int is_even (int x, void *ctx) { return (x % 2) == 0; }
HEGEL_FILTER_INT (hegel_schema_int_range (0, 100), is_even, NULL)
/* field is always even; filter_too_much health check if rejection
** rate is too high */

/* Flat-map: dependent generation — draw n, then draw something
** whose range depends on n. */
hegel_schema_t dep_fn (int n, void *ctx) {
  return hegel_schema_int_range (0, n * 10);
}
HEGEL_FLAT_MAP_INT (hegel_schema_int_range (1, 10), dep_fn, NULL)
/* field is in [0, n*10] where n was drawn first */
```

All three take the source schema as a `hegel_schema_t` (ownership
transferred), a callback, and a ctx pointer passed through to the
callback.

**Type variants:** `HEGEL_MAP_INT` / `_I64` / `_DOUBLE`,
`HEGEL_FILTER_INT` / `_I64` / `_DOUBLE`,
`HEGEL_FLAT_MAP_INT` / `_I64` / `_DOUBLE`. Each requires the source
schema to match its type (INTEGER for int / i64, FLOAT for double)
and writes to a matching-width field. The callback signature
matches the type: `int (*fn)(int, void *)`, `int64_t (*fn)(int64_t,
void *)`, `double (*fn)(double, void *)`.

Shrinking works on the **source** (the root draw), which is why
`map` preserves shrinker quality: it doesn't shrink the output
directly but shrinks the input that produced the output.

### `HEGEL_ONE_OF` — pick one of several scalar schemas

When a single range doesn't capture the distribution you want,
`HEGEL_ONE_OF` picks between multiple scalar schemas — e.g.,
"small int OR large int" to exercise both ends of a function:

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
HEGEL_ONE_OF(hegel_schema_int_range(0, 10),
             hegel_schema_int_range(1000, 9999))
```

The draw picks one case uniformly, then draws from it. Each case
must be a scalar schema (INTEGER or FLOAT) and all cases must share
the same scalar width — the slot size and alignment are inferred
from the first case's kind at layout time.

### `HEGEL_BOOL` — 1-byte `bool` field

<!-- /ignore api-literal: one-line macro call shape, correct-syntax form -->
```c
HEGEL_BOOL()   /* 1-byte bool field, drawn as 0 or 1 */
```

Used positionally inside `HEGEL_STRUCT`; the enclosing struct
field should be `bool` or `_Bool` from `stdbool.h` (1 byte).
Under the hood it's `hegel_schema_u8_range(0, 1)` specialized for
the boolean slot.

### `HEGEL_REGEX` — regex-generated text

<!-- /ignore api-literal: one-line macro call shape, correct-syntax form -->
```c
HEGEL_REGEX("[a-z]+", 64)
```

Used positionally inside `HEGEL_STRUCT`; the enclosing struct
field is a `char *`.  Generates a string matching the regex, with
buffer capacity 64. **Warning:** the underlying primitive uses hegeltest's
"contains a match" semantics, not full-match. For permissive
patterns (matching the empty string), the generator returns
arbitrary bytes. For stricter constraints, use `hegel_schema_text`
(which draws char-by-char from `[a-z]`). See `TODO.md` for the
FFI-level fix status.

### `HEGEL_ONE_OF_STRUCT` — polymorphic pointer producer

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
HEGEL_BINDING (n_items);
hegel_schema_t one_of = HEGEL_ONE_OF_STRUCT (type_a, type_b, type_c);
collection_schema = HEGEL_STRUCT (Collection,
    HEGEL_LET    (n_items, HEGEL_INT (1, 6)),
    HEGEL_ARR_OF (HEGEL_USE (n_items), one_of),
    HEGEL_USE    (n_items));
```

Picks one of several struct schemas, allocates it, returns a raw
pointer. Writes nothing to any parent (it's a "value schema," not a
"field schema"). Used as the element type of `HEGEL_ARR_OF` to get
an array of heterogeneous pointers, or inside `HEGEL_OPTIONAL` for
an optional pointer-to-polymorphic-struct.

## Running tests

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
static hegel_schema_t my_schema;

static void my_test (hegel_testcase *tc) {
  MyStruct *v;
  hegel_shape *sh = hegel_schema_draw (tc, my_schema, (void **) &v);
  /* ... assertions on v ... */
  hegel_shape_free (sh);
}

int main (void) {
  my_schema = HEGEL_STRUCT (MyStruct, ...);
  hegel_run_test (my_test);
  hegel_schema_free (my_schema);
  return 0;
}
```

- `hegel_schema_draw(tc, schema, (void **)&ptr)` allocates and fills
  your struct. Returns a `hegel_shape *` owning everything allocated.
- `hegel_shape_free(sh)` frees the whole tree in one call.
- `hegel_schema_free(schema)` cleans up the schema itself (at
  program exit — decrements refcount, frees if zero).

**Schema placement — either works.** The pattern above keeps the
schema as a file-scope static built once in `main`; you can also
build it inside the testcase function and free it at the end of each
call. Schema constructors are pure allocators and don't touch hegel's
draw state, so both produce identical draws. Building in-testcase is
slightly wasteful — the tree gets rebuilt per case, and in fork mode
every child re-allocates it — but schema allocation is tiny next to a
full test case's round-trip cost, so for most schemas the overhead is
in the noise. Hoist to `main` when it matters (deeply nested
composites, large arrays) or when you just want less ceremony per test.

### `HEGEL_DRAW` — unified write-at-address

`hegel_schema_draw` is designed for structs: the `void **` out
parameter works because the framework allocates and hands you back
a pointer. For scalars and other non-allocating kinds, the
`HEGEL_DRAW(&addr, sch)` macro is a more ergonomic entry point:

<!-- /include tests/docs/test_doc_schema_api.c:170-176 -->
```c
static void draw_scalar_demo (hegel_testcase * tc)
{
  int                 x;
  hegel_shape *       sh = HEGEL_DRAW (&x, int_sch);
  HEGEL_ASSERT (x >= 0, "got %d", x);
  hegel_shape_free (sh);
}
```
<!-- /endinclude -->

Dispatch rules:

- **STRUCT kind** — allocates, writes the struct pointer at `*addr`,
  returns the owning shape. Pass `&p` where `p` is `MyStruct *` —
  same as the classic form but with a single `void *` out-parameter.
- **Scalar / text / optional / union / variant kinds** — writes the
  drawn value directly at `addr`. Returns a shape (informational
  leaf for scalars; real tree for composites). You still call
  `hegel_shape_free` on it — not NULL for scalars, contrary to a
  previous doc claim.
- **ARRAY_INLINE / ARR_OF / SELF / ONE_OF_STRUCT / BIND / USE** —
  abort at the top level. These kinds only make sense inside a
  parent struct.

`HEGEL_DRAW` captures `tc` from the enclosing scope by convention,
so the enclosing function's parameter must be named `tc` — matches
existing test style. The schema is **not consumed** — the caller
keeps ownership and must still call `hegel_schema_free` when done.

### Scalar by-value shortcuts

`HEGEL_DRAW_<T>` takes range arguments directly (or none, for the
full type range) and dispatches to the `hegel_draw_*` primitive —
**no schema allocation, no composition**. Same `(lo, hi)` / `()`
overloading as the schema macros `HEGEL_INT` / etc.:

<!-- /include tests/docs/test_doc_schema_api.c:182-186 -->
```c
  int                 x = HEGEL_DRAW_INT    (0, 10);
  int                 y = HEGEL_DRAW_INT    ();           /* INT_MIN..INT_MAX */
  int64_t             a = HEGEL_DRAW_I64    (-100, 100);
  double              d = HEGEL_DRAW_DOUBLE (0.0, 1.0);
  bool                b = HEGEL_DRAW_BOOL   ();           /* no range — 0 or 1 */
```
<!-- /endinclude -->

Variants: `_INT`, `_I64`, `_U64`, `_DOUBLE`, `_FLOAT`, `_BOOL`.

**For composed scalar schemas** — `HEGEL_MAP_INT`, `HEGEL_FILTER_INT`,
`HEGEL_FLAT_MAP_INT`, `HEGEL_ONE_OF` — there's no by-value shortcut.
Hoist the schema and use the general form:

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
static hegel_schema_t squared;   /* built in main(), reused per call */

static void my_test (hegel_testcase *tc) {
  int x;
  hegel_shape *sh = HEGEL_DRAW (&x, squared);
  /* ... use x ... */
  hegel_shape_free (sh);
}

int main (void) {
  squared = HEGEL_MAP_INT (hegel_schema_int_range (0, 100),
                           square_fn, NULL);
  hegel_run_test (my_test);
  hegel_schema_free (squared);
  return 0;
}
```

**No `HEGEL_DRAW_ARRAY` by design.** The typed family is
deliberately scalar-only: C has no way to return a composite "by
value" without re-inventing a `{ptr, len}` struct and its lifetime
story. For anything non-scalar, use `HEGEL_DRAW(&ptr, sch)` — the
general mechanism carries the shape-or-not distinction in its return
type, not in the macro name.

### Recursion depth

Schemas with `HEGEL_SELF` need a termination strategy.
`HEGEL_SELF()` expands to a 50/50 `HEGEL_OPTIONAL(self)`, but for
schemas with branching factor ≥ 2 (e.g. a binary tree with left +
right self-references), the branching process is **critical** under
uniform 50/50 probabilities — a non-trivial fraction of draws would
recurse arbitrarily deep.

`HEGEL_DEFAULT_MAX_DEPTH = 50` is the cap. When a draw hits the
bound, it calls `hegel_health_fail` — signalling a **test-setup**
failure, not a code-under-test failure. The 50 is the empirical
value at which the selftest's binary-tree schema draws reliably
(>1000 consecutive runs without tripping the cap).

For schemas with lower branching (single `HEGEL_SELF`, linear
chains) smaller caps work fine; for heavier branching, increase the
cap with `hegel_schema_draw_n(tc, schema, &out, max_depth)` or
`hegel_schema_draw_at_n(tc, addr, schema, max_depth)`.

This isn't a shrinking-quality concern — the cap does most of the
termination work for deep recursive schemas, and that's by design.

## Shape accessors

Most of the time you read data straight from your struct. When the
struct doesn't carry the information (e.g., untagged unions, or
array lengths in parallel parameters), read from the shape tree:

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
int tag     = hegel_shape_tag       (hegel_shape_field (sh, 0));
int len     = hegel_shape_array_len (hegel_shape_field (sh, 2));
int is_some = hegel_shape_is_some   (hegel_shape_field (sh, 1));
```

### Offset-based lookup — `HEGEL_SHAPE_GET`

Counting field positions is error-prone when the struct grows.
Since every struct binding carries its field offset, shapes can
also be looked up by offset:

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
hegel_shape * field = HEGEL_SHAPE_GET (sh, MyStruct, some_field);
int len = hegel_shape_array_len (HEGEL_SHAPE_GET (sh, MyStruct, items));
```

`HEGEL_SHAPE_GET(sh, T, f)` expands to
`hegel_shape_get_offset(sh, offsetof(T, f))` — a linear scan over
the struct shape's bindings looking for a match. Returns NULL if
the offset doesn't correspond to any bound field, or if `sh` is
not a struct shape.

**Array shape lives at the pointer slot.** When you call
`hegel_shape_array_len` on a `HEGEL_ARR_OF` field's shape, use the
offset of the pointer field (the one written by `HEGEL_ARR_OF`),
not the int field holding the count. Only the pointer slot owns
the `HEGEL_SHAPE_ARRAY` shape; the count slot (written by a sibling
`HEGEL_USE`) is a trivial scalar leaf, and `array_len` returns 0
for non-array shapes.

Named accessors (by field name string) are still on the TODO list
— see `TODO.md`.

## Refcounting and ownership

Schemas are reference-counted with **ownership-transfer semantics**.
Every constructor starts life with refcount=1. When you pass a
schema to a parent (struct field, array elem, union case, etc.),
the reference is **transferred** — the parent does not bump the
count. After passing, the caller no longer owns the child.

For sharing a schema across multiple parents, explicitly call
`hegel_schema_ref()` to add a reference before passing:

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
hegel_schema_t color = HEGEL_STRUCT (Color, ...);
hegel_schema_ref (color);  /* +1 for second use */

palette_s = HEGEL_STRUCT (Palette,
    HEGEL_ARRAY_INLINE (color, sizeof (Color), 1, 5));

sprite_s = HEGEL_STRUCT (Sprite,
    HEGEL_ARRAY_INLINE (color, sizeof (Color), 1, 4));

/* Both palette_s and sprite_s independently own a reference to color.
** Freeing them releases the refs in turn; color is freed when the
** last one drops. */
hegel_schema_free (palette_s);
hegel_schema_free (sprite_s);
```

Call `hegel_schema_ref()` **once per additional use**. The pattern is:
`ref` immediately before the extra handoff, then pass normally.

## Spans (automatic)

Every composite schema emits span annotations to the Hegel server
automatically. The shrinker uses these to perform structural
operations — "drop this list element," "swap these variants,"
"minimize this subtree" — instead of mutating raw bytes. You don't
need to call `hegel_start_span` / `hegel_stop_span` yourself when
using the schema API.

## File layout

- `hegel_c.h` — base primitive FFI (draws, spans, asserts)
- `hegel_gen.h` — schema system declarations (this API)
- `hegel_gen.c` — schema system implementation (pure C, compiled
  into `libhegel_c.a` via `cc` crate + `rust-version/build.rs`)

Users include `hegel_gen.h` and link `libhegel_c.a`. The primitive
`hegel_c.h` is pulled in transitively.

## See also

- [patterns.md](patterns.md) — catalog of C memory layouts with
  pointers to the test files that demonstrate each one
- [`hegel_gen.h`](../hegel_gen.h) — the source, including the
  detailed comment explaining the struct-vs-typedef choice for
  `hegel_schema_t`
- [`TODO.md`](../TODO.md) — open items (named accessors, regex
  fullmatch, old API deprecation, etc.)
