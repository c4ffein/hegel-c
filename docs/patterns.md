<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Schema pattern catalog

The selftest suite doubles as documentation. Each pattern below
maps a common C data layout to the test file that demonstrates how
to generate it with the schema API. Use this when you're trying to
figure out "how do I generate X?".

For the API reference itself, see [schema-api.md](schema-api.md).

All test files are in `tests/selftest/` and start with
`test_gen_schema_`. Each one uses real C struct layouts and verifies
the generated data matches its declared constraints.

---

## Recursive trees with optional fields

**C layout:**
```c
typedef struct Tree {
  int val;
  char *label;            /* optional */
  struct Tree *left;      /* optional recursive */
  struct Tree *right;     /* optional recursive */
} Tree;
```

**Schema:**
```c
tree_schema = HEGEL_STRUCT (Tree,
    HEGEL_INT      (-1000, 1000),
    HEGEL_OPTIONAL (hegel_schema_text (0, 8)),
    HEGEL_SELF     (),
    HEGEL_SELF     ());
```

**Test:** [`test_gen_schema_recursive_tree_json_roundtrip.c`](../tests/selftest/test_gen_schema_recursive_tree_json_roundtrip.c)

Exercises: `HEGEL_OPTIONAL`, `HEGEL_SELF`, JSON round-trip as a
canonical PBT property. Also contains two other test functions
showing `HEGEL_ARRAY` (`Bag` with `int *items`) and the full set of
typed integer macros (`Sensor` with u8/i16/u32/i64/float/double).

---

## Nested sub-struct by value

**C layout:**
```c
typedef struct { uint8_t r, g, b; }  RGB;
typedef struct { RGB fg; RGB bg; }   Palette;
```

**Schema (fresh form):**
```c
palette_schema = HEGEL_STRUCT (Palette,
    HEGEL_INLINE (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()),
    HEGEL_INLINE (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()));
```

**Schema (shared form — one `RGB` schema reused at two offsets):**
```c
rgb_schema = HEGEL_STRUCT (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ());
hegel_schema_ref (rgb_schema);
palette_schema = HEGEL_STRUCT (Palette,
    HEGEL_INLINE_REF (RGB, rgb_schema),
    HEGEL_INLINE_REF (RGB, rgb_schema));
```

**Tests:**
- [`test_gen_schema_inline_struct.c`](../tests/selftest/test_gen_schema_inline_struct.c) — fresh form, mixed inline + `HEGEL_OPTIONAL`, and three-level nesting
- [`test_gen_schema_shape_get_and_reuse.c`](../tests/selftest/test_gen_schema_shape_get_and_reuse.c) — shared form + `HEGEL_SHAPE_GET` resolving through nested shapes

Exercises: `HEGEL_INLINE` / `HEGEL_INLINE_REF`. Sub-struct storage is
carved out of the parent's own allocation (no extra malloc per
field). Nests to any depth — each inner `HEGEL_INLINE` runs its own
`sizeof(T)` assert at schema-build time, so field-order or type
mismatches fire at init with a clear diagnostic. `HEGEL_SHAPE_GET`
resolves leaf offsets through any level of nesting.

---

## Tagged union with wrapper struct

**C layout:**
```c
typedef struct {
  int tag;
  union {
    struct { double radius; }                circle;
    struct { double width; double height; }  rect;
  } u;
} Shape;
```

**Schema:**
```c
shape_schema = HEGEL_STRUCT (Shape,
    HEGEL_UNION (
        HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
        HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                    HEGEL_DOUBLE (0.1, 100.0))));
```

**Test:** [`test_gen_schema_union_variant_tagged_untagged.c`](../tests/selftest/test_gen_schema_union_variant_tagged_untagged.c) (Test 1)

Exercises: `HEGEL_UNION` in the classic C tagged-union idiom — tag
field adjacent to the union, inside a wrapping struct. Standard way
to do sum types in C.

---

## Bare union (tag lives in shape tree only)

**C layout:**
```c
typedef union {
  struct { double radius; }                circle;
  struct { double width; double height; }  rect;
} RawShape;
```

**Schema:**
```c
raw_shape_schema = HEGEL_STRUCT (RawShape,
    HEGEL_UNION_UNTAGGED (
        HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
        HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                    HEGEL_DOUBLE (0.1, 100.0))));
```

```c
/* In the test: read the variant tag from the shape tree */
int tag = hegel_shape_tag (hegel_shape_field (sh, 0));
```

**Test:** [`test_gen_schema_union_variant_tagged_untagged.c`](../tests/selftest/test_gen_schema_union_variant_tagged_untagged.c) (Test 2)

Exercises: `HEGEL_UNION_UNTAGGED`. Use when your C code doesn't
store a tag field (e.g., the tested function takes the discriminator
as a separate parameter). The framework picks a variant; you read
it back from the shape tree.

---

## Variant with tag + pointer to separate allocation

**C layout:**
```c
typedef struct { double radius; }           Circle;
typedef struct { double width, height; }    Rect;

typedef struct {
  int    tag;
  void * value;   /* Circle* or Rect* */
} ShapeVar;
```

**Schema:**
```c
hegel_schema_t circle_s = HEGEL_STRUCT (Circle,
    HEGEL_DOUBLE (0.1, 100.0));
hegel_schema_t rect_s   = HEGEL_STRUCT (Rect,
    HEGEL_DOUBLE (0.1, 100.0),
    HEGEL_DOUBLE (0.1, 100.0));

shapevar_schema = HEGEL_STRUCT (ShapeVar,
    HEGEL_VARIANT (circle_s, rect_s));
```

**Test:** [`test_gen_schema_union_variant_tagged_untagged.c`](../tests/selftest/test_gen_schema_union_variant_tagged_untagged.c) (Test 3)

Exercises: `HEGEL_VARIANT`. Use when variants are genuinely
different-size types and you want each in its own allocation
instead of a padded union.

---

## Array of scalars, contiguous

**C layout:**
```c
typedef struct {
  int *items;
  int  n_items;
} Bag;
```

**Schema:**
```c
hegel_schema_t items_arr =
    HEGEL_ARRAY (hegel_schema_int_range (0, 100), 0, 10);
bag_schema = HEGEL_STRUCT (Bag,
    HEGEL_FACET (items_arr, value),
    HEGEL_FACET (items_arr, size));
hegel_schema_free (items_arr);
```

**Tests:**
- [`test_gen_schema_recursive_tree_json_roundtrip.c`](../tests/selftest/test_gen_schema_recursive_tree_json_roundtrip.c) (`Bag` test)
- [`test_gen_schema_array_all_composition_patterns.c`](../tests/selftest/test_gen_schema_array_all_composition_patterns.c) (`ScalarBag` test)

Exercises: `HEGEL_ARRAY` with a scalar element. `items` is one
malloc'd block of `n * sizeof(int)` bytes.

---

## Array of inline structs (contiguous, no pointer chasing)

**C layout:**
```c
typedef struct { uint8_t r, g, b; }             Color;
typedef struct { Color *colors; int n; }        Palette;
```

**Schema:**
```c
hegel_schema_t color_s = HEGEL_STRUCT (Color,
    HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ());

palette_s = HEGEL_STRUCT (Palette,
    HEGEL_ARRAY_INLINE (color_s, sizeof (Color), 1, 5));
```

**Test:** [`test_gen_schema_array_all_composition_patterns.c`](../tests/selftest/test_gen_schema_array_all_composition_patterns.c) (`Palette` + others)

Exercises: `HEGEL_ARRAY_INLINE` with a same-type struct element. One
contiguous allocation, each element at stride `sizeof(Color)`.
Cache-friendly. Also demonstrates `hegel_schema_ref` for sharing
`color_s` across two parent schemas.

---

## Array of inline tagged unions (per-element polymorphism, fixed stride)

**C layout:**
```c
typedef struct {
  int tag;
  union {
    struct { double radius; }                circle;
    struct { double width; double height; }  rect;
  } u;
} Shape;

typedef struct { Shape *shapes; int n_shapes; } Gallery;
```

**Schema:**
```c
hegel_schema_t shape_union = hegel_schema_of (HEGEL_UNION (
    HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
    HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                HEGEL_DOUBLE (0.1, 100.0))));

gallery_schema = HEGEL_STRUCT (Gallery,
    HEGEL_ARRAY_INLINE (shape_union, sizeof (Shape), 1, 6));
```

**Test:** [`test_gen_schema_array_inline_of_tagged_unions.c`](../tests/selftest/test_gen_schema_array_inline_of_tagged_unions.c)

Exercises: `HEGEL_ARRAY_INLINE` + `HEGEL_UNION`. Each array slot is
`sizeof(Shape)` bytes (= max of variant sizes + tag + padding), and
each slot independently holds a circle or a rect. One allocation
for the whole array, zero pointer chasing, per-element variant.

---

## Array of bare unions using common initial sequence

**C layout:**
```c
typedef union {
  struct { int tag; double radius; }                circle;
  struct { int tag; double width; double height; }  rect;
  struct { int tag; int64_t millis; }                timestamp;
} BareShape;
```

The C standard's **common initial sequence** rule: if every union
member begins with the same sequence of fields (here, `int tag`),
you can read those fields through any member regardless of which
was last written. Used by real C interpreters (Lua TValue, Erlang
VM terms, Scheme cell tagging).

**Schema:**
```c
hegel_schema_t bare_shape = hegel_schema_of (HEGEL_UNION_UNTAGGED (
    HEGEL_CASE (HEGEL_INT    (17, 17),
                HEGEL_DOUBLE (0.1, 100.0)),
    HEGEL_CASE (HEGEL_INT    (29, 29),
                HEGEL_DOUBLE (0.1, 100.0),
                HEGEL_DOUBLE (0.1, 100.0)),
    HEGEL_CASE (HEGEL_INT    (42, 42),
                HEGEL_I64    (0, 1000000000))));
```

**Test:** [`test_gen_schema_array_inline_of_bare_unions_common_prefix.c`](../tests/selftest/test_gen_schema_array_inline_of_bare_unions_common_prefix.c)

Exercises: `HEGEL_UNION_UNTAGGED` with each case writing its own
tag as a constant `HEGEL_INT(min==max)` at offset 0 of the slot.
Arbitrary tag values (17, 29, 42) prove they're not just variant
indices. In user code: `*(int*)(&shape)` reads the tag through any
variant.

---

## Array of wrapper structs holding variant pointers

**C layout:**
```c
typedef struct { int tag; int value; }               TypeA;  /* tag=3 */
typedef struct { int tag; char *name; uint8_t b; }   TypeB;  /* tag=5 */

typedef struct {
  int    which;    /* 0 = TypeA, 1 = TypeB */
  void * ptr;
} Elem;

typedef struct { Elem *items; int n_items; } Collection;
```

**Schema:**
```c
hegel_schema_t type_a = HEGEL_STRUCT (TypeA,
    HEGEL_INT (3, 3),          /* constant */
    HEGEL_INT (-1000, 1000));

hegel_schema_t type_b = HEGEL_STRUCT (TypeB,
    HEGEL_INT (5, 5),          /* constant */
    HEGEL_TEXT (1, 10),
    HEGEL_U8 ());

hegel_schema_t elem_schema = HEGEL_STRUCT (Elem,
    HEGEL_VARIANT (type_a, type_b));

coll_schema = HEGEL_STRUCT (Collection,
    HEGEL_ARRAY_INLINE (elem_schema, sizeof (Elem), 1, 5));
```

**Test:** [`test_gen_schema_array_of_variant_struct_pointers.c`](../tests/selftest/test_gen_schema_array_of_variant_struct_pointers.c)

Exercises: `HEGEL_ARRAY_INLINE` + `HEGEL_VARIANT` inside each element
struct. Array of fixed-size `Elem` wrappers, each holding a
different-size variant pointer. The `which` field is the framework's
picked variant index; the pointed-to `TypeA.tag` / `TypeB.tag`
fields are always their respective constants.

---

## Array of raw pointers (no wrapper) to different-size structs

**C layout:**
```c
typedef struct { int tag; int value; }       TypeA;  /* tag=3, 8 B */
typedef struct { int tag; char *name; ... }  TypeB;  /* tag=5, 24 B */
typedef struct { int tag; double x, y, z; }  TypeC;  /* tag=7, 32 B */

typedef struct {
  void **items;       /* bare heterogeneous pointer array */
  int    n_items;
} RawCollection;
```

**Schema:**
```c
hegel_schema_t one_of = HEGEL_ONE_OF_STRUCT (type_a, type_b, type_c);

hegel_schema_t items_arr = HEGEL_ARRAY (one_of, 1, 6);
coll_schema = HEGEL_STRUCT (RawCollection,
    HEGEL_FACET (items_arr, value),
    HEGEL_FACET (items_arr, size));
hegel_schema_free (items_arr);
```

**Test:** [`test_gen_schema_array_of_raw_struct_pointers_by_kind.c`](../tests/selftest/test_gen_schema_array_of_raw_struct_pointers_by_kind.c)

Exercises: `HEGEL_ONE_OF_STRUCT` as the element of `HEGEL_ARRAY`.
No wrapper struct — `items` is a bare `void **`, and each pointer
goes directly to a `TypeA`, `TypeB`, or `TypeC` allocation. The
user reads `*(int*)items[i]` to get the tag (3, 5, or 7) and casts
accordingly. This is the "tag inside the pointed-to struct" idiom.

---

## Nested arrays (array of arrays)

**C layout:**
```c
typedef struct { int *values; int n_values; } Row;
typedef struct { Row *rows;   int n_rows; }   Matrix;
```

**Schema:**
```c
hegel_schema_t values_arr =
    HEGEL_ARRAY (hegel_schema_int_range (0, 99), 1, 6);
hegel_schema_t row = HEGEL_STRUCT (Row,
    HEGEL_FACET (values_arr, value),
    HEGEL_FACET (values_arr, size));
hegel_schema_free (values_arr);

matrix_schema = HEGEL_STRUCT (Matrix,
    HEGEL_ARRAY_INLINE (row, sizeof (Row), 1, 4));
```

**Test:** [`test_gen_schema_nested_array_of_arrays_matrix.c`](../tests/selftest/test_gen_schema_nested_array_of_arrays_matrix.c)

Exercises: `HEGEL_ARRAY_INLINE` containing a struct that contains
`HEGEL_ARRAY`. Outer array is contiguous (rows packed), each row's
`values` is its own separate allocation. Two-level memory layout in
one schema.

---

## All five array composition patterns in one file

**Test:** [`test_gen_schema_array_all_composition_patterns.c`](../tests/selftest/test_gen_schema_array_all_composition_patterns.c)

Five sub-tests in one binary:
1. `ARRAY` of scalars (int)
2. `ARRAY` of pointer-to-struct
3. `ARRAY_INLINE` of structs (same size)
4. `ARRAY_INLINE` containing struct with nested `ARRAY`
5. `ARRAY` of ptr-to-struct containing `ARRAY_INLINE`

Also demonstrates `hegel_schema_ref` to share `color_s` across
`palette_s` and `sprite_s` (two different parent schemas).

---

## Non-adjacent array facets

Sometimes the struct you're trying to generate puts the array's
pointer and length at non-adjacent positions — maybe there's a tag
field or some unrelated scalar between them. The 2-slot inline form
of `HEGEL_ARRAY` can't express this; facets can.

**C layout:**
```c
typedef struct {
  int *items;
  int  tag;         /* unrelated field between the two facets */
  int  n;
} Bag;
```

**Schema:**
```c
hegel_schema_t items_arr =
    HEGEL_ARRAY (hegel_schema_int_range (0, 999), 0, 8);
bag_schema = HEGEL_STRUCT (Bag,
    HEGEL_FACET (items_arr, value),
    HEGEL_INT   (-10, 10),
    HEGEL_FACET (items_arr, size));
hegel_schema_free (items_arr);
```

**Tests:**
- [`test_gen_schema_facets_nonadjacent.c`](../tests/selftest/test_gen_schema_facets_nonadjacent.c) — tag field between the two facets
- [`test_gen_schema_facets_reversed.c`](../tests/selftest/test_gen_schema_facets_reversed.c) — `size` comes before `value` in the struct
- [`test_gen_schema_facets_nested.c`](../tests/selftest/test_gen_schema_facets_nested.c) — outer array whose elements hold their own inner array (verifies per-instance ctx scoping)

Exercises: `HEGEL_FACET` projecting one named `HEGEL_ARRAY` into
separated struct positions. The two projections are resolved into
one consistent drawn array per struct instance; across instances
(e.g. array elements of a parent array), each gets its own draw.

---

## Functional combinators — map, filter, flat_map

The schema API isn't limited to structural composition. For
transforming drawn values, filtering, and dependent generation,
there are `HEGEL_MAP_INT`, `HEGEL_FILTER_INT`, `HEGEL_FLAT_MAP_INT`.

**C layout:**
```c
typedef struct { int square; }      SquaredThing;
typedef struct { int even_val; }    EvenThing;
typedef struct { int dependent; }   DepThing;
```

**Schemas:**
```c
int square_fn (int x, void *ctx) { (void)ctx; return x * x; }
int is_even   (int x, void *ctx) { (void)ctx; return (x % 2) == 0; }
hegel_schema_t dep_fn (int n, void *ctx) {
  (void)ctx;
  return hegel_schema_int_range (0, n * 10);
}

/* map: field is always a perfect square in [0, 10000] */
HEGEL_STRUCT (SquaredThing,
    HEGEL_MAP_INT (hegel_schema_int_range (0, 100), square_fn, NULL));

/* filter: field is always even */
HEGEL_STRUCT (EvenThing,
    HEGEL_FILTER_INT (hegel_schema_int_range (0, 100), is_even, NULL));

/* flat_map: dependent — range of second draw depends on first */
HEGEL_STRUCT (DepThing,
    HEGEL_FLAT_MAP_INT (hegel_schema_int_range (1, 10), dep_fn, NULL));
```

**Test:** [`test_gen_schema_functional_combinators.c`](../tests/selftest/test_gen_schema_functional_combinators.c)

Exercises **all legacy `hegel_gen_*` combinator functionality**
at the schema layer, in 7 sub-tests:

1. Optional int pointer via `HEGEL_OPTIONAL` + `int_range`
2. `map` for int / i64 / double (square, cube, halve)
3. `filter` for int / i64 / double (even, positive, ≥1.0)
4. `flat_map` for int / i64 / double (dependent ranges)
5. `HEGEL_ONE_OF` — "small OR large" distribution
6. `HEGEL_BOOL` — 1-byte bool field
7. `HEGEL_REGEX` — pattern-generated text

This file is the feature-parity proof: the schema API fully
subsumes the legacy `hegel_gen_*` combinator surface. The legacy
API can be deprecated once existing tests that use it get migrated.

---

## Test count

As of this document's last update: **9 schema test files**, all
passing under AddressSanitizer with no leaks, use-after-frees, or
double frees. Full suite: **32/32 selftests** in the `selftest`
target.
