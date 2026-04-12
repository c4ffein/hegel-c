<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Schema API

Reference for `hegel_gen.h` — the typed schema/shape system for
property-based testing over C data structures.

## Overview

Writing a property test manually looks like this:

```c
static void my_test (hegel_testcase *tc) {
  int x = hegel_draw_int (tc, 0, 100);
  int y = hegel_draw_int (tc, 0, 100);
  Thing *t = malloc (sizeof (Thing));
  t->x = x; t->y = y;
  HEGEL_ASSERT (my_function (t) >= 0, "x=%d y=%d", x, y);
  free (t);
}
```

That works for one-off cases, but when your tested function takes a
recursive tree, a tagged union, or an array of polymorphic pointers,
the hand-written generator grows into hundreds of lines.

The schema API lets you **describe your C type declaratively** and
get generation, allocation, span annotation (for better shrinking),
and cleanup for free:

```c
static hegel_schema_t thing_schema;  /* build once in main() */

static void my_test (hegel_testcase *tc) {
  Thing *t;
  hegel_shape *sh = hegel_schema_draw (tc, thing_schema, (void **) &t);
  HEGEL_ASSERT (my_function (t) >= 0, "...");
  hegel_shape_free (sh);  /* frees t and every allocation it transitively owns */
}
```

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

```c
typedef struct { hegel_schema *_raw; } hegel_schema_t;
```

Zero runtime cost — `sizeof(hegel_schema_t) == sizeof(void*)`, and
the compiler optimizes the struct wrapping away. Distinct from
`hegel_schema *` at the type level, so you can't accidentally pass
random pointers to schema functions. All public API is typed in
terms of `hegel_schema_t`.

Users should never need to touch `_raw` — it's the library's
internal handle.

## Primitive constructors

Every integer type has a **full-range** version (no args) and a
**range-constrained** version:

```c
hegel_schema_t hegel_schema_i8 (void);               /* [-128, 127] */
hegel_schema_t hegel_schema_i8_range (int64_t lo, int64_t hi);
/* likewise: i16, i32, i64, u8, u16, u32, u64, int, long */
```

Floats:
```c
hegel_schema_t hegel_schema_float  (void);           /* [-FLT_MAX, FLT_MAX] */
hegel_schema_t hegel_schema_float_range  (double lo, double hi);
hegel_schema_t hegel_schema_double (void);
hegel_schema_t hegel_schema_double_range (double lo, double hi);
```

Text (currently char-by-char from `[a-z]`, see TODO.md for the
`hegel_draw_regex` fullmatch fix):
```c
hegel_schema_t hegel_schema_text (int min_len, int max_len);
```

These all produce "field schemas" you plug into a struct via the
convenience macros (next section).

## Struct field macros

For every scalar type, there's a macro that takes a struct type +
field name + optional range:

```c
HEGEL_INT   (MyStruct, field)                       /* full int range */
HEGEL_INT   (MyStruct, field, -1000, 1000)          /* constrained */
HEGEL_U8    (MyStruct, byte_field)                  /* [0, 255] */
HEGEL_I16   (MyStruct, temp, -400, 850)
HEGEL_U32   (MyStruct, serial)
HEGEL_I64   (MyStruct, timestamp)
HEGEL_FLOAT (MyStruct, voltage, 0.0, 5.0)
HEGEL_DOUBLE (MyStruct, latitude, -90.0, 90.0)
HEGEL_TEXT  (MyStruct, label, 0, 8)                 /* [a-z]{0..8}, always present */
```

The macro picks between the full-range and range-constrained form by
argument count.

## Struct constructor

```c
hegel_schema_t s = hegel_schema_struct (sizeof (MyStruct),
    HEGEL_INT  (MyStruct, x, 0, 100),
    HEGEL_INT  (MyStruct, y, 0, 100),
    HEGEL_TEXT (MyStruct, name, 1, 20));
```

Variadic. The macro appends an `H_END` sentinel internally; **do
not write a trailing NULL**. The top-level schema you pass to
`hegel_schema_draw` must be a struct.

## Composite schemas

### `HEGEL_OPTIONAL` — 50/50 nullable pointer

```c
HEGEL_OPTIONAL (MyStruct, ptr_field, inner_schema)
```

On each draw, flips a coin. If true, draws `inner_schema` and stores
the pointer at `ptr_field`. If false, leaves the field NULL. Use for
optional strings (inner = `hegel_schema_text(...)`) or optional
sub-structs.

### `HEGEL_SELF` — optional recursive pointer

```c
HEGEL_SELF (Tree, left)    /* Tree *left; — optional recursive */
```

Shorthand for `HEGEL_OPTIONAL(Tree, left, hegel_schema_self())`.
Must be used inside a `hegel_schema_struct` that declares the type.
The field must be a pointer to the enclosing struct type.
Automatically optional (50/50 NULL) because recursive chains need
termination — unbounded recursion is capped at
`HEGEL_DEFAULT_MAX_DEPTH = 5`.

### `HEGEL_ARRAY` — variable-length array with separate allocation

```c
typedef struct { int *items; int n_items; } Bag;

HEGEL_ARRAY (Bag, items, n_items, hegel_schema_int_range (0, 100), 0, 10)
```

Writes both the malloc'd array pointer (at `items` offset) and the
count (at `n_items` offset). The element schema can be any of:
`hegel_schema_int_range(...)`, `hegel_schema_text(...)`, a struct
schema (each element gets a separate allocation, array stores
pointers), or a `HEGEL_ONE_OF_STRUCT(...)` (heterogeneous pointer
array).

### `HEGEL_ARRAY_INLINE` — contiguous array of inline structs

```c
typedef struct { Point *points; int n_points; } Path;

HEGEL_ARRAY_INLINE (Path, points, n_points, point_schema, sizeof (Point), 1, 8)
```

One contiguous buffer, stride = `sizeof(elem)`. Elements are laid
out inline (no per-element pointer chase). The `elem` schema must be
a struct or union — runtime assert catches misuse. Use for
cache-friendly arrays where every element is the same size.

### `HEGEL_UNION` — tagged union with tag field in the struct

```c
typedef struct {
  int tag;
  union {
    struct { double radius; } circle;
    struct { double w, h; } rect;
  } u;
} Shape;

HEGEL_UNION (Shape, tag,
    HEGEL_CASE (HEGEL_DOUBLE (Shape, u.circle.radius, 0.1, 100.0)),
    HEGEL_CASE (HEGEL_DOUBLE (Shape, u.rect.w, 0.1, 100.0),
                HEGEL_DOUBLE (Shape, u.rect.h, 0.1, 100.0)))
```

Picks a variant, writes the variant index to `tag`, fills the chosen
union member's fields. Each case is `HEGEL_CASE(...)` wrapping the
fields to draw for that variant. No trailing `NULL` in the cases.

### `HEGEL_UNION_UNTAGGED` — tag lives in shape tree only

Same shape as `HEGEL_UNION` but **writes no tag** to the struct. The
user reads the chosen variant from the shape tree via
`hegel_shape_tag()`. Use when your C type doesn't have an outer tag
field (see the common-initial-sequence pattern in
[patterns.md](patterns.md)).

```c
HEGEL_UNION_UNTAGGED (
    HEGEL_CASE (HEGEL_DOUBLE (Thing, circle.radius, 0.1, 100.0)),
    HEGEL_CASE (HEGEL_DOUBLE (Thing, rect.w, 0.1, 100.0)))
```

### `HEGEL_VARIANT` — tag + pointer to separately allocated struct

```c
typedef struct { int tag; void *value; } ShapeVar;

HEGEL_VARIANT (ShapeVar, tag, value, circle_schema, rect_schema)
```

Picks a variant, allocates the chosen struct, writes the tag at
`tag` and the pointer at `value`. The user casts based on tag. Used
when variants are genuinely different-size structs and you want
each in its own allocation.

### `HEGEL_MAP_INT` / `HEGEL_FILTER_INT` / `HEGEL_FLAT_MAP_INT` — functional combinators

The schema API handles *structural* composition (struct fields,
arrays, unions). For *functional* composition — transforming drawn
values, filtering, dependent generation — use these combinators:

```c
/* Map: draw an int, transform through fn, store result. */
int square_fn (int x, void *ctx) { return x * x; }
HEGEL_MAP_INT (MyStruct, field,
               hegel_schema_int_range (0, 100),
               square_fn, NULL)
/* field is always a perfect square in [0, 10000] */

/* Filter: draw an int, keep only those matching the predicate. */
int is_even (int x, void *ctx) { return (x % 2) == 0; }
HEGEL_FILTER_INT (MyStruct, field,
                  hegel_schema_int_range (0, 100),
                  is_even, NULL)
/* field is always even; if hegel rolls too many odd values in a
** row, Hypothesis's filter_too_much health check discards the case */

/* Flat-map: dependent generation — draw n, then draw something
** whose range depends on n. */
hegel_schema_t dep_fn (int n, void *ctx) {
  return hegel_schema_int_range (0, n * 10);
}
HEGEL_FLAT_MAP_INT (MyStruct, field,
                    hegel_schema_int_range (1, 10),
                    dep_fn, NULL)
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

### `HEGEL_ONE_OF_INT` / `_I64` / `_DOUBLE` — pick one of several scalar schemas

When a single range doesn't capture the distribution you want,
`HEGEL_ONE_OF_*` picks between multiple scalar schemas — e.g.,
"small int OR large int" to exercise both ends of a function:

```c
HEGEL_ONE_OF_INT(MyStruct, field,
                 hegel_schema_int_range(0, 10),
                 hegel_schema_int_range(1000, 9999))
```

The draw picks one case uniformly, then draws from it. Each case
must be a scalar schema (INTEGER or FLOAT) matching the target
field's type.

### `HEGEL_BOOL` — 1-byte `bool` field

```c
HEGEL_BOOL(MyStruct, is_active)   /* 1-byte unsigned int in [0,1] */
```

Expands to `hegel_schema_u8_range_at(offsetof(T, f), 0, 1)`. The
field should be `bool` or `_Bool` from `stdbool.h` (1 byte).

### `HEGEL_REGEX` — regex-generated text

```c
HEGEL_REGEX(MyStruct, text, "[a-z]+", 64)
```

Generates a `char *` string matching the pattern, with buffer
capacity 64. **Warning:** the underlying primitive uses hegeltest's
"contains a match" semantics, not full-match. For permissive
patterns (matching the empty string), the generator returns
arbitrary bytes. For stricter constraints, use `hegel_schema_text`
(which draws char-by-char from `[a-z]`). See `TODO.md` for the
FFI-level fix status.

### `HEGEL_ONE_OF_STRUCT` — polymorphic pointer producer

```c
hegel_schema_t one_of = HEGEL_ONE_OF_STRUCT (type_a, type_b, type_c);
/* Then use as an array element or optional inner: */
HEGEL_ARRAY (Collection, items, n_items, one_of, 1, 6)
```

Picks one of several struct schemas, allocates it, returns a raw
pointer. Writes nothing to any parent (it's a "value schema," not a
"field schema"). Used as the element type of `HEGEL_ARRAY` to get
an array of heterogeneous pointers, or inside `HEGEL_OPTIONAL` for
an optional pointer-to-polymorphic-struct.

## Running tests

```c
static hegel_schema_t my_schema;

static void my_test (hegel_testcase *tc) {
  MyStruct *v;
  hegel_shape *sh = hegel_schema_draw (tc, my_schema, (void **) &v);
  /* ... assertions on v ... */
  hegel_shape_free (sh);
}

int main (void) {
  my_schema = hegel_schema_struct (sizeof (MyStruct), ...);
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

## Shape accessors

Most of the time you read data straight from your struct. When the
struct doesn't carry the information (e.g., untagged unions, or
array lengths in parallel parameters), read from the shape tree:

```c
int tag     = hegel_shape_tag       (hegel_shape_field (sh, 0));
int len     = hegel_shape_array_len (hegel_shape_field (sh, 2));
int is_some = hegel_shape_is_some   (hegel_shape_field (sh, 1));
```

Currently positional (by field index). Named accessors are on the
TODO list — see `TODO.md`.

## Refcounting and ownership

Schemas are reference-counted with **ownership-transfer semantics**.
Every constructor starts life with refcount=1. When you pass a
schema to a parent (struct field, array elem, union case, etc.),
the reference is **transferred** — the parent does not bump the
count. After passing, the caller no longer owns the child.

For sharing a schema across multiple parents, explicitly call
`hegel_schema_ref()` to add a reference before passing:

```c
hegel_schema_t color = hegel_schema_struct (sizeof (Color), ...);
hegel_schema_ref (color);  /* +1 for second use */

palette_s = hegel_schema_struct (sizeof (Palette),
    HEGEL_ARRAY_INLINE (Palette, colors, n, color, sizeof (Color), 1, 5));

sprite_s = hegel_schema_struct (sizeof (Sprite),
    HEGEL_ARRAY_INLINE (Sprite, pixels, n, color, sizeof (Color), 1, 4));

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
