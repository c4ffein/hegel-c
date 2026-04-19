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
   `HEGEL_STRUCT`, `HEGEL_ARRAY`, `HEGEL_OPTIONAL`, and the scalar
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

<!-- /include hegel_gen.h:146-146 -->
```c
typedef struct { hegel_schema * _raw; } hegel_schema_t;
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
- `HEGEL_ARRAY` is placed via two `HEGEL_FACET` projections — a
  pointer-sized slot and an int-sized slot — which may appear at
  non-adjacent positions, in either order; see `HEGEL_ARRAY` below
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
hegel_schema_t hegel_schema_i8          (void);  /* [-128, 127] */
hegel_schema_t hegel_schema_i8_range    (int64_t lo, int64_t hi);
```
<!-- /endinclude -->

Same shape for `i16`, `i32`, `i64`, `int`, `long`, `u8`, `u16`, `u32`,
`u64` — each with a no-arg full-range version and a `_range` variant.

Floats:

<!-- /include hegel_gen.h:533-536 -->
```c
hegel_schema_t hegel_schema_float        (void);  /* [-FLT_MAX, FLT_MAX] */
hegel_schema_t hegel_schema_float_range  (double lo, double hi);
hegel_schema_t hegel_schema_double       (void);  /* [-DBL_MAX, DBL_MAX] */
hegel_schema_t hegel_schema_double_range (double lo, double hi);
```
<!-- /endinclude -->

Text (currently char-by-char from `[a-z]`, see TODO.md for the
`hegel_draw_regex` fullmatch fix):

<!-- /include hegel_gen.h:542-542 -->
```c
hegel_schema_t hegel_schema_text (int min_len, int max_len);
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
`HEGEL_ARRAY`, etc. in the same parent — they occupy their own slots
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

### `HEGEL_ARRAY` — variable-length array, projected via facets

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct { int *items; int n_items; } Bag;

hegel_schema_t items_arr =
    HEGEL_ARRAY (hegel_schema_int_range (0, 100), 0, 10);
HEGEL_STRUCT (Bag,
    HEGEL_FACET (items_arr, value),     /* int *items */
    HEGEL_FACET (items_arr, size));     /* int n_items */
hegel_schema_free (items_arr);
```

`HEGEL_ARRAY(elem, lo, hi)` builds an array schema that produces
one drawn buffer per draw. Its two projections — the data pointer
and the length — are placed into the parent struct individually via
`HEGEL_FACET(hat, value)` and `HEGEL_FACET(hat, size)`. The facets
may be non-adjacent and in either order; match them to the struct's
actual field order.

The element schema can be any of: `hegel_schema_int_range(...)`,
`hegel_schema_text(...)`, a struct schema (each element separately
allocated, array stores pointers), or `HEGEL_ONE_OF_STRUCT(...)`
(heterogeneous pointer array).

**Ownership math:** `HEGEL_ARRAY(...)` returns `refcount = 1` for
the user. Each `HEGEL_FACET` use bumps source refcount. Each
`HEGEL_STRUCT` decrements source refcount at struct-free time for
the subschema children it holds. The user must call
`hegel_schema_free(hat)` once after building their struct(s) to
release their own reference — without this, the array schema and
its element schema would leak.

**Scoping:** `hat`-driven facets share within one struct instance,
but are independent across struct instances. If `hat` appears in
an element schema of an outer array, each outer element gets an
INDEPENDENT draw of `hat`.

**Cannot be used directly:** `HEGEL_STRUCT(T, HEGEL_ARRAY(...))`
aborts at schema-build time with a diagnostic. Always name the
array and use `HEGEL_FACET`.

### `HEGEL_FACET` — project a facet of a composite

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
HEGEL_FACET (hat, value)    /* ARRAY's data pointer */
HEGEL_FACET (hat, size)     /* ARRAY's length */
```

Projects a specific facet of a named composite schema into the
parent struct's positional field list. `hat` must be a schema with
facets (currently only `HEGEL_ARRAY`; future kinds may expose
`tag`, `body`, etc.). Bumps source refcount each time; the user
keeps their original reference and must release it after building.
See `HEGEL_ARRAY` for the complete usage pattern.

### `HEGEL_ARRAY_INLINE` — contiguous array of inline structs

<!-- /ignore api-example: distilled illustration backed by the linked test(s) or header file -->
```c
typedef struct { Point *points; int n_points; } Path;

HEGEL_STRUCT (Path,
    HEGEL_ARRAY_INLINE (point_schema, sizeof (Point), 1, 8));
```

Same two-slot shape as `HEGEL_ARRAY`, but elements are laid out in
one contiguous buffer (stride = `sizeof(elem)`) rather than each
allocated separately. The `elem` schema must be a struct or union.

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
hegel_schema_t one_of = HEGEL_ONE_OF_STRUCT (type_a, type_b, type_c);
hegel_schema_t items_arr = HEGEL_ARRAY (one_of, 1, 6);
HEGEL_STRUCT (Collection,
    HEGEL_FACET (items_arr, value),
    HEGEL_FACET (items_arr, size));
hegel_schema_free (items_arr);
```

Picks one of several struct schemas, allocates it, returns a raw
pointer. Writes nothing to any parent (it's a "value schema," not a
"field schema"). Used as the element type of `HEGEL_ARRAY` to get
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
- **ARRAY / ARRAY_INLINE / SELF / ONE_OF_STRUCT** — abort at the top
  level. These kinds only make sense inside a parent struct (see
  `HEGEL_FACET` for the array composition story).

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

**Array facets are asymmetric:** when you call
`hegel_shape_array_len` on an `HEGEL_ARRAY`'s shape, grab the
`value`-facet slot (the pointer field), not the `size`-facet slot.
Only the value-facet slot owns the `HEGEL_SHAPE_ARRAY` shape; the
size-facet slot is a trivial scalar leaf, and `array_len` returns
0 for non-array shapes.

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
