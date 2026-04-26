# Hegel C Reference

## Table of Contents

- [Setup](#setup) — vendor, static link, `gcc` flags
- [Test Structure](#test-structure) — `hegel_run_test*`, suites, per-case setup
- [TestCase Functions](#testcase-functions) — `hegel_draw_*`, `hegel_assume`, `hegel_note`, `hegel_fail`, `HEGEL_ASSERT`
- [Schema System](#schema-system) — three-layer model, refcount-transfer ownership
- [Schema Reference](#schema-reference) — scalars, text, struct composition, arrays, optionals, unions, bindings, combinators
- [Drawing and Lifecycle](#drawing-and-lifecycle) — `HEGEL_DRAW`, `hegel_schema_draw`, `hegel_shape_free`, shape accessors
- [C-Specific Examples](#c-specific-examples)
- [Gotchas](#gotchas)
- [Stateful Testing](#stateful-testing)

## Setup

hegel-c is **not yet published as a package**. Vendor it as a submodule and link the static library:

```bash
git submodule add https://github.com/c4ffein/hegel-c third_party/hegel-c
cd third_party/hegel-c/rust-version && cargo build --release
```

This produces `third_party/hegel-c/rust-version/target/release/libhegel_c.a`. Add the include path and link flags to your build:

```make
CFLAGS  += -I third_party/hegel-c -funwind-tables -fexceptions
LDFLAGS += -L third_party/hegel-c/rust-version/target/release
LDLIBS  += -lhegel_c -lpthread -lm -ldl
```

The `-funwind-tables -fexceptions` flags are required — hegel-c relies on Rust's panic unwinding to surface failures across the FFI boundary.

A standalone compile of one test:

```bash
gcc -O2 -I third_party/hegel-c -funwind-tables -fexceptions \
    -o test_foo test_foo.c \
    -L third_party/hegel-c/rust-version/target/release \
    -lhegel_c -lpthread -lm -ldl
```

The first run downloads the Hegel server (a Python package) into `.hegel/venv/` via [uv](https://docs.astral.sh/uv/). Add `.hegel/` to `.gitignore`. If something goes wrong with server installation, see https://hegel.dev/reference/installation.

**Two layers of public API:**

- `hegel_c.h` — primitive draws and the test runner. Stable.
- `hegel_gen.h` — the **schema system** for declaratively describing C structs and getting generation, allocation, and cleanup for free. This is what you should use for anything beyond a one-shot scalar test.

> The header `hegel_c.h` also exposes a legacy `hegel_gen_*` combinator API (e.g. `hegel_gen_int`, `hegel_gen_one_of`, `hegel_gen_optional`, `hegel_gen_map_int`). **Do not use it.** It predates the schema system, will be deleted, and is not covered in this reference.

## Test Structure

A hegel-c test is a regular C function with the signature `void test_fn(hegel_testcase *tc)`. You hand it to one of the test runners:

```c
/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 Your Name
** Part of yourproject — see LICENSE for terms. */

#include "hegel_c.h"
#include "hegel_gen.h"

static void test_addition_commutes(hegel_testcase *tc) {
    int a = HEGEL_DRAW_INT();   /* full int range */
    int b = HEGEL_DRAW_INT();
    HEGEL_ASSERT(a + b == b + a, "a + b != b + a for a=%d b=%d", a, b);
}

int main(void) {
    hegel_run_test(test_addition_commutes);
    return 0;
}
```

Tests live alongside other tests in the same project — there is no separate "hegel test directory". Build them like any other C executable.

### Runners

| Runner | Mode | Cases | Failure handling |
|---|---|---|---|
| `hegel_run_test(fn)` | fork | 100 | Prints counterexample, calls `exit(1)` |
| `hegel_run_test_n(fn, n)` | fork | `n` | Prints counterexample, calls `exit(1)` |
| `hegel_run_test_result(fn)` | fork | 100 | Returns `0` on success, `1` on failure |
| `hegel_run_test_result_n(fn, n)` | fork | `n` | Returns `0`/`1` |
| `hegel_run_test_nofork(fn)` | nofork | 100 | No crash isolation — a SIGSEGV kills the process |
| `hegel_run_test_nofork_n(fn, n)` | nofork | `n` | No crash isolation |

**Fork mode is the default and what you want.** Each test case runs in a forked child; the parent serves draw requests over a pipe and turns child crashes (SIGSEGV, SIGABRT) into hegel failures so they can shrink. **Nofork mode** exists for benchmarking and for code that can't be forked (e.g., libraries that initialize MPI before the test runs).

The `_result_*` variants are for binaries that run multiple tests in one process — they don't `exit()` on failure, so you can collect results from several `hegel_run_test_*` calls and return your own status code at the end.

### Suites

For multiple tests sharing one binary, use a suite — it amortizes the ~1 s server startup across all tests instead of paying it per binary:

```c
int main(void) {
    hegel_suite *s = hegel_suite_new();
    hegel_suite_add(s, "addition_commutes", test_addition_commutes);
    hegel_suite_add(s, "multiplication_commutes", test_multiplication_commutes);
    int rc = hegel_suite_run(s);
    hegel_suite_free(s);
    return rc;
}
```

`hegel_suite_run` returns 0 if all tests pass, non-zero otherwise.

### Per-case setup

If the library under test has global state that must be reset between cases (RNG seeds, caches, allocator pools), register a setup callback:

```c
static void reset_library(void) {
    SCOTCH_randomSeed(42);
    SCOTCH_randomReset();
}

int main(void) {
    hegel_set_case_setup(reset_library);
    hegel_run_test(test_partition);
    return 0;
}
```

Pass `NULL` to clear. The callback runs in the child process before each case in fork mode, and once per case in nofork mode.

### Configuration

There is no `Settings` struct yet — case count is the only knob, set via the `_n` runner variants. Seed control, verbosity, and health-check suppression are not exposed on the C side in v0.

## TestCase Functions

The opaque `hegel_testcase *tc` handle is passed into your test function. It is **not** copyable — don't store it past the end of the test callback.

The table below shows the standard ways to draw values and signal results. The `HEGEL_DRAW_*` macros are the idiomatic form for scalar draws — they capture `tc` from the enclosing scope and accept either zero arguments (full range of the type) or `(lo, hi)`.

| Macro / function | Purpose |
|---|---|
| `HEGEL_DRAW_INT()` / `HEGEL_DRAW_INT(lo, hi)` | Draw `int` (full range, or `[lo, hi]`) |
| `HEGEL_DRAW_I64()` / `HEGEL_DRAW_I64(lo, hi)` | Draw `int64_t` |
| `HEGEL_DRAW_U64()` / `HEGEL_DRAW_U64(lo, hi)` | Draw `uint64_t` |
| `HEGEL_DRAW_FLOAT()` / `HEGEL_DRAW_FLOAT(lo, hi)` | Draw `float` |
| `HEGEL_DRAW_DOUBLE()` / `HEGEL_DRAW_DOUBLE(lo, hi)` | Draw `double` |
| `HEGEL_DRAW_BOOL()` | Draw `bool` (no range) |
| `hegel_draw_text(tc, min, max, buf, cap)` | Write a null-terminated string of length in `[min, max]` into `buf` |
| `hegel_draw_regex(tc, pattern, buf, cap)` | Write a null-terminated string matching `pattern` into `buf` |
| `hegel_assume(tc, cond)` | Discard this case if `cond == 0` (not a failure) |
| `hegel_note(tc, msg)` | Print `msg` only on the final replay of a failing case |
| `hegel_fail(msg)` | Fail the test with `msg`; triggers shrinking |
| `hegel_health_fail(msg)` | Fail with `"Health check failure: " + msg` — signals the test setup is broken, not the code under test |
| `HEGEL_ASSERT(cond, fmt, ...)` | If `!cond`, format and call `hegel_fail` |

The lower-level functions (`hegel_draw_int`, `hegel_draw_i64`, …) are what these macros call. You'll only see them surface in error messages or when reading older code — prefer the macros.

```c
static void test_division(hegel_testcase *tc) {
    int64_t a = HEGEL_DRAW_I64();
    int64_t b = HEGEL_DRAW_I64();
    hegel_assume(tc, b != 0);
    char buf[128];
    snprintf(buf, sizeof buf, "a=%lld b=%lld", (long long)a, (long long)b);
    hegel_note(tc, buf);
    int64_t q = a / b;
    int64_t r = a % b;
    HEGEL_ASSERT(a == q * b + r, "a != q*b + r");
}
```

`hegel_note` does nothing during generation and shrinking — it only prints on the final replay of the minimal counterexample. Don't use it for progress logging.

`hegel_fail` and `hegel_health_fail` differ in intent:

- **`hegel_fail`** — the **code under test** is broken. Shrink, show the user a minimal counterexample.
- **`hegel_health_fail`** — the **test setup** is broken (e.g., the schema's recursion probabilities mean every draw exhausts the depth bound). The user needs to fix the test, not the code.

Most tests use `HEGEL_ASSERT` (which routes through `hegel_fail`) and never need to call `hegel_health_fail` directly — the schema system emits it automatically when the depth bound trips.

## Schema System

The schema system is the high-level API for describing C data structures. It handles allocation, span annotation, ownership tracking, and cleanup automatically. Use it for anything that isn't a one-shot scalar test.

### Three-layer architecture

A draw with the schema API touches three things:

1. **Schema tree** (`hegel_schema_t`) — a description of the type. Built once by the user, refcounted, lives across many test cases.
2. **Shape tree** (`hegel_shape *`) — per-run metadata. Built automatically on each draw. Owns the value memory.
3. **Value memory** — the actual C struct (or scalar) passed to the function under test. Allocated and filled in by the draw, freed when the shape is freed.

A test loop looks like this:

```c
static hegel_schema_t TREE_SCHEMA;   /* built once, in main() */

static void test_tree(hegel_testcase *tc) {
    Tree *t = NULL;
    hegel_shape *sh = hegel_schema_draw(tc, TREE_SCHEMA, (void **)&t);
    /* ... call function under test on t ... */
    hegel_shape_free(sh);             /* frees value memory + shape tree */
}

int main(void) {
    TREE_SCHEMA = HEGEL_STRUCT(Tree,
        HEGEL_INT      (-1000, 1000),
        HEGEL_OPTIONAL (hegel_schema_text(0, 8)),
        HEGEL_SELF     (),
        HEGEL_SELF     ());
    hegel_run_test(test_tree);
    hegel_schema_free(TREE_SCHEMA);
    return 0;
}
```

### Wrapper type

`hegel_schema_t` is a thin newtype wrapper around an internal `hegel_schema *` — distinct C type from raw pointers, zero runtime cost. You never touch the internal pointer; pass `hegel_schema_t` values around by value.

### Refcount-transfer ownership

Schemas are reference-counted. The rules:

- A schema returned by any constructor (`HEGEL_INT`, `hegel_schema_text`, `HEGEL_STRUCT`, etc.) starts with refcount = 1.
- **Passing a schema as an argument to a parent transfers the reference** — no bump, no copy. Don't free it after.
- To use the same schema in multiple parents (or in two slots of the same parent), call `hegel_schema_ref(s)` to bump the refcount before each extra use.
- `hegel_schema_free(s)` decrements; the actual free happens at zero.

```c
hegel_schema_t rgb = HEGEL_STRUCT(RGB, HEGEL_U8(), HEGEL_U8(), HEGEL_U8());
hegel_schema_ref(rgb);                 /* bump for the second use */
hegel_schema_t pal = HEGEL_STRUCT(Palette,
    HEGEL_INLINE_REF(RGB, rgb),
    HEGEL_INLINE_REF(RGB, rgb));
/* rgb is now held by pal twice; do not free it separately. */
hegel_schema_free(pal);                /* drops both refs to rgb */
```

This contrasts with most other PBT libraries where generators are values you can copy freely. The motivation is to keep heap allocations bounded for deeply-nested schemas.

### Positional macro layout

`HEGEL_STRUCT(T, entry1, entry2, ...)` takes a list of field schemas in **declaration order** — one entry per struct field, in the same order the C compiler lays them out. The macro:

1. Computes byte offsets the way the C compiler does (size + alignment per kind).
2. Asserts at runtime that `sizeof(T) == computed_total`. If a field is reordered or its type changes, the assert fires at schema-build time with a clear diagnostic.

Some entries occupy more than one slot:

- `HEGEL_ARRAY_INLINE` — pointer slot + count slot.
- `HEGEL_UNION` / `HEGEL_VARIANT` — one cluster slot (tag + body / tag + ptr).

The non-positional `HEGEL_LET` / `HEGEL_LET_ARR` entries occupy zero slots — they declare bindings without consuming a struct field.

## Schema Reference

### Scalar schemas

Bounded ranges:

| Macro | C type | Inferred from |
|---|---|---|
| `HEGEL_INT(lo, hi)` / `HEGEL_INT()` | `int` | full `int` range when no args |
| `HEGEL_I8(lo, hi)` / `HEGEL_I8()` | `int8_t` | |
| `HEGEL_I16(lo, hi)` / `HEGEL_I16()` | `int16_t` | |
| `HEGEL_I32(lo, hi)` / `HEGEL_I32()` | `int32_t` | |
| `HEGEL_I64(lo, hi)` / `HEGEL_I64()` | `int64_t` | |
| `HEGEL_U8(lo, hi)` / `HEGEL_U8()` | `uint8_t` | |
| `HEGEL_U16(lo, hi)` / `HEGEL_U16()` | `uint16_t` | |
| `HEGEL_U32(lo, hi)` / `HEGEL_U32()` | `uint32_t` | |
| `HEGEL_U64(lo, hi)` / `HEGEL_U64()` | `uint64_t` | |
| `HEGEL_LONG(lo, hi)` / `HEGEL_LONG()` | `long` | |
| `HEGEL_FLOAT(lo, hi)` / `HEGEL_FLOAT()` | `float` | |
| `HEGEL_DOUBLE(lo, hi)` / `HEGEL_DOUBLE()` | `double` | |
| `HEGEL_BOOL()` | `bool` | |

The zero-arg form generates the full representable range of the type. Prefer it — broad generators find more bugs (see `SKILL.md` § Generator Discipline).

Pure-value scalar constructors (no positional context — useful when composing into combinators, optionals, and one-of):

```c
hegel_schema_t s_int   = hegel_schema_int_range(0, 100);
hegel_schema_t s_text  = hegel_schema_text(0, 64);
hegel_schema_t s_float = hegel_schema_float_range(-1.0, 1.0);
```

The `hegel_schema_*` functions cover every primitive (`i8`/`i16`/`i32`/`i64`/`u8`/`u16`/`u32`/`u64`/`int`/`long`, `float`/`double`, plus full-range variants without `_range`).

### Text and regex

```c
HEGEL_TEXT(min_len, max_len)   /* char * field, pointer-sized slot */
HEGEL_REGEX(pattern, capacity)
```

`HEGEL_REGEX` allocates a `capacity`-byte buffer and writes a null-terminated string matching `pattern`. The pattern is a Hypothesis-flavored regex (most ERE / PCRE features supported); see https://hypothesis.readthedocs.io/en/latest/strategies.html#hypothesis.strategies.from_regex.

### Composing structs

```c
typedef struct { int x, y; } Point;
hegel_schema_t pt = HEGEL_STRUCT(Point, HEGEL_INT(), HEGEL_INT());
```

Nested-by-value sub-structs use `HEGEL_INLINE` (fresh) or `HEGEL_INLINE_REF` (shared):

```c
typedef struct { uint8_t r, g, b; } RGB;
typedef struct { RGB fg; RGB bg; } Palette;

hegel_schema_t pal = HEGEL_STRUCT(Palette,
    HEGEL_INLINE(RGB, HEGEL_U8(), HEGEL_U8(), HEGEL_U8()),
    HEGEL_INLINE(RGB, HEGEL_U8(), HEGEL_U8(), HEGEL_U8()));
```

Sub-struct storage is carved out of the parent's allocation — no extra `malloc` per field. Each `HEGEL_INLINE` runs its own `sizeof(T)` assert at schema-build time.

### Optionals and recursion

```c
HEGEL_OPTIONAL(inner)   /* 50/50 nullable pointer; 1 slot */
HEGEL_SELF()            /* optional recursive pointer; 1 slot */
```

`HEGEL_SELF` is allowed only inside a `HEGEL_STRUCT` and refers to that struct's own schema. Recursive draws are bounded by `HEGEL_DEFAULT_MAX_DEPTH` (50). Exceeding the depth calls `hegel_health_fail` — the schema is malformed, not the data. Override the depth via `hegel_schema_draw_n(tc, schema, &out, max_depth)`.

```c
typedef struct Tree {
    int val;
    char *label;
    struct Tree *left, *right;
} Tree;

hegel_schema_t tree = HEGEL_STRUCT(Tree,
    HEGEL_INT      (-1000, 1000),
    HEGEL_OPTIONAL (hegel_schema_text(0, 8)),
    HEGEL_SELF     (),
    HEGEL_SELF     ());
```

### Arrays

Four array shapes, each occupying different slot patterns in the parent struct:

| Macro | Slots | Layout |
|---|---|---|
| `HEGEL_ARR_OF(length, elem)` | 1 (pointer) | `T *items` — length stored elsewhere via `HEGEL_LET` |
| `HEGEL_LEN_PREFIXED_ARRAY(length, elem)` | 1 (pointer) | Pascal-style: `[n, elem_0, …, elem_{n-1}]`, slot 0 holds `n` cast to `elem`'s int type |
| `HEGEL_TERMINATED_ARRAY(length, elem, sentinel)` | 1 (pointer) | C-string-style: `[elem_0, …, elem_{n-1}, sentinel]` |
| `HEGEL_ARRAY_INLINE(elem, elem_sz, lo, hi)` | 2 (`void *` + `int`) | Pointer field + count field, in that order |

The length parameter of `HEGEL_ARR_OF` / `_LEN_PREFIXED_ARRAY` / `_TERMINATED_ARRAY` must be **either a binding read** (`HEGEL_USE` / `HEGEL_USE_AT` / `HEGEL_USE_PATH`) **or a literal** (`HEGEL_CONST(N)`). Raw `HEGEL_INT(lo, hi)` is rejected at schema-build time — wrap it in `HEGEL_LET` first. This is enforced because the length must be readable from outside the array (e.g., to fill in a sibling count field).

`HEGEL_ARR_OF` element kinds: `INTEGER`, `STRUCT`, `OPTIONAL_PTR` (enables `HEGEL_SELF()` inside arrays for recursive trees), `ONE_OF_STRUCT`, `MAP_INT` / `FILTER_INT` / `FLAT_MAP_INT`.

`HEGEL_LEN_PREFIXED_ARRAY` and `HEGEL_TERMINATED_ARRAY` element kind: `INTEGER` only (any width). For `_TERMINATED_ARRAY`, if the element is a bounded integer, the constructor checks at schema-build time that the sentinel could not collide with a drawn element; for derived element schemas (uses, maps), the check fires at runtime on first collision.

### Bindings — the array-with-count pattern

The canonical "pointer field + count field" idiom uses bindings to keep the two coherent:

```c
typedef struct { int *items; int n; } Bag;

HEGEL_BINDING(n);   /* declare the binding id (function-local scope) */

hegel_schema_t bag = HEGEL_STRUCT(Bag,
    HEGEL_LET    (n, HEGEL_INT(0, 10)),                /* draws + caches */
    HEGEL_ARR_OF (HEGEL_USE(n), HEGEL_INT(0, 100)),    /* int *items */
    HEGEL_USE    (n));                                  /* int n */
```

The pieces:

| Macro | Purpose |
|---|---|
| `HEGEL_BINDING(name)` | Declare a compile-time binding id. Function-local scope is the pit of success. |
| `HEGEL_LET(name, inner)` | Non-positional: draw `inner` and cache under `name`. Inner must be a scalar integer (4 or 8 bytes) or float. |
| `HEGEL_USE(name)` | Read cached scalar; writes to the current slot. Width / sign / float-ness must match the LET inner. |
| `HEGEL_USE_I64(name)` / `_U64` / `_FLOAT` / `_DOUBLE` | Same but for non-`int` widths/types. |
| `HEGEL_LET_ARR(name, length, elem)` | Non-positional: draw an `int[]` and cache. |
| `HEGEL_USE_AT(name)` | Read `arr[current_index]` of the scope where `name` was declared (for "sizes drives groups" patterns inside `HEGEL_ARR_OF`). |
| `HEGEL_USE_PATH(<HEGEL_PARENT>*, name [, HEGEL_INDEX_HERE])` | Variadic explicit-path resolver. Each `HEGEL_PARENT` skips one scope outward; trailing `HEGEL_INDEX_HERE` indexes into a `LET_ARR`. |
| `HEGEL_CONST(N)` | Compile-time integer constant, no byte-stream draw. Usable as an `HEGEL_ARR_OF` length. |

Because `HEGEL_LET` is non-positional, the `HEGEL_USE` that writes to the count field can appear anywhere in the layout — before, between, or after the array. Per-struct-instance scoping means nested arrays-of-structs each draw their own value; see `tests/selftest/test_binding_jagged_2d.c` for jagged 2D arrays.

`HEGEL_USE_PATH` exists for the rare case where you need to bypass a same-named binding in an inner scope to reach an outer one — it's the only case `HEGEL_USE_AT` / `HEGEL_USE` can't express.

### Unions and variants

Three flavors for tagged sums:

```c
HEGEL_UNION         (HEGEL_CASE(...), HEGEL_CASE(...), ...)  /* int tag + union body */
HEGEL_UNION_UNTAGGED(HEGEL_CASE(...), HEGEL_CASE(...), ...)  /* union body only; tag in shape tree */
HEGEL_VARIANT       (case_struct_schema, case_struct_schema, ...) /* int tag + void * ptr */
```

`HEGEL_CASE(field_entries...)` is used inside `HEGEL_UNION` / `_UNTAGGED`. It contains layout entries (scalars, inlines, etc.), **not bindings**.

For an "or" choice of pointer-typed elements (e.g., the element type of an array of variants):

```c
HEGEL_ONE_OF_STRUCT(struct_schema_a, struct_schema_b, ...)  /* pointer-producing */
HEGEL_ONE_OF       (scalar_schema_a, scalar_schema_b, ...)  /* 1 slot, size from first case */
```

`HEGEL_ONE_OF_STRUCT` is what you put inside `HEGEL_OPTIONAL` or as the element of `HEGEL_ARR_OF` to get heterogeneous lists.

To pull a `HEGEL_UNION` / `HEGEL_VARIANT` layout entry out as a standalone schema (e.g., to use it as the element type of a `HEGEL_ARRAY_INLINE`), wrap it in `hegel_schema_of(entry)`.

### Combinators — map / filter / flat_map

```c
HEGEL_MAP_INT     (source, fn, ctx)   /* int (*fn)(int, void *) */
HEGEL_FILTER_INT  (source, pred, ctx) /* int (*pred)(int, void *) */
HEGEL_FLAT_MAP_INT(source, fn, ctx)   /* hegel_schema_t (*fn)(int, void *) */
```

Same for `_I64` and `_DOUBLE` suffixes. Each takes a source schema (ownership transferred), a callback, and a `void *` context that the caller manages — `ctx` must outlive the schema.

Use `map` to transform a drawn primitive, `filter` to reject (retries up to 3 times, then discards), and `flat_map` for dependent generation. Prefer narrowing through bounds rather than `filter` when you can — high rejection rates trip the `FilterTooMuch` health check.

`map` / `filter` / `flat_map` do **not** degrade shrinking quality — hegel uses integrated shrinking on the underlying byte stream, so the shrinker works the same regardless of how the value is transformed.

## Drawing and Lifecycle

There are two entry points for getting a value out of a schema. Pick by use case.

### `HEGEL_DRAW(&addr, schema)` — write at address

Unified draw for any schema kind. Writes the produced value at `addr`:

- `STRUCT` kind allocates and writes the pointer at `*addr`.
- Scalar / text / optional / union / variant kinds write the value directly at `addr`.
- `ARRAY` / `ARRAY_INLINE` / `SELF` / `ONE_OF_STRUCT` abort at the top level (they only compose inside a struct).

Returns a `hegel_shape *` you must free. The schema is **not** consumed — build it once and keep it for the whole run:

```c
/* Built once in main(): */
static hegel_schema_t SMALL_INT;   /* = hegel_schema_int_range(0, 100); */

static void test_foo(hegel_testcase *tc) {
    int x;
    hegel_shape *sh = HEGEL_DRAW(&x, SMALL_INT);
    /* use x */
    hegel_shape_free(sh);
}
/* hegel_schema_free(SMALL_INT) at the end of main() */
```

`HEGEL_DRAW` shines for **composed scalars** (`HEGEL_MAP_INT`, `HEGEL_FILTER_INT`, `HEGEL_FLAT_MAP_INT`) and for top-level draws of optionals / unions / variants. For a plain bounded int, the macros below are simpler:

```c
int    x  = HEGEL_DRAW_INT();              /* full int range */
int    y  = HEGEL_DRAW_INT(0, 100);        /* bounded */
int64_t z = HEGEL_DRAW_I64();
double  d = HEGEL_DRAW_DOUBLE(-1.0, 1.0);
bool    b = HEGEL_DRAW_BOOL();
```

(Same pattern for `_U64` / `_FLOAT`.) These dispatch directly to the underlying primitives — there is no schema or shape to free. Use them for any plain bounded scalar draw.

There is intentionally **no** `HEGEL_DRAW_ARRAY` — top-level arrays don't have a meaningful place to live without a containing struct. Wrap arrays in a struct.

### `hegel_schema_draw(tc, schema, &ptr)` — allocate a struct

For struct schemas. Allocates the struct, fills it in, returns a `hegel_shape *`:

```c
Tree *t;
hegel_shape *sh = hegel_schema_draw(tc, TREE_SCHEMA, (void **)&t);
/* use t */
hegel_shape_free(sh);
```

`hegel_schema_draw_n` and `hegel_schema_draw_at_n` accept a `max_depth` override for `HEGEL_SELF`-recursive schemas:

```c
hegel_shape *sh = hegel_schema_draw_n(tc, TREE_SCHEMA, (void **)&t, 100);
```

### Lifecycle

| Function | Frees |
|---|---|
| `hegel_shape_free(sh)` | Walks shape tree, frees value memory + shape. NULL-safe. |
| `hegel_schema_free(s)` | Refcount-decrement; actual free at zero. NULL-safe. |
| `hegel_schema_ref(s)` | Refcount-increment. Use before sharing across multiple parents. |

Build the schema once (in `main`), draw inside the test function, free the shape at the end of the test, and free the schema after `hegel_run_test*` returns.

### Shape accessors

For data-driven assertions or untagged-union dispatch:

| Function | Purpose |
|---|---|
| `hegel_shape_tag(sh)` | Variant index (which case was drawn) |
| `hegel_shape_array_len(sh)` | Drawn array length |
| `hegel_shape_is_some(sh)` | `true` if optional was present |
| `hegel_shape_field(sh, i)` | Positional access to a struct field's sub-shape |

Named field accessors are not yet available — use positional indexes matching the `HEGEL_STRUCT` entry order.

## C-Specific Examples

### Dependent generation with sequential draws

Like all hegel bindings, hegel-c is imperative — dependent generation is just sequential code:

```c
typedef struct { int *items; int n; } Bag;

static hegel_schema_t BAG;     /* built in main with the binding pattern */

static void test_index_in_range(hegel_testcase *tc) {
    Bag *bag;
    hegel_shape *sh = hegel_schema_draw(tc, BAG, (void **)&bag);
    if (bag->n > 0) {
        int idx = HEGEL_DRAW_INT(0, bag->n - 1);
        (void)bag->items[idx];   /* always valid */
    }
    hegel_shape_free(sh);
}
```

### Custom struct with the schema API

```c
typedef struct {
    uint32_t max_retries;
    uint64_t timeout_ms;
    char    *name;
} Config;

static hegel_schema_t CONFIG;

static void test_config_merge(hegel_testcase *tc) {
    Config *base, *override;
    hegel_shape *s1 = hegel_schema_draw(tc, CONFIG, (void **)&base);
    hegel_shape *s2 = hegel_schema_draw(tc, CONFIG, (void **)&override);
    Config merged = config_merge(base, override);
    HEGEL_ASSERT(strcmp(merged.name, override->name) == 0,
                 "merged.name should equal override->name");
    hegel_shape_free(s1);
    hegel_shape_free(s2);
}

int main(void) {
    CONFIG = HEGEL_STRUCT(Config,
        HEGEL_U32 (),
        HEGEL_U64 (),
        HEGEL_TEXT(0, 64));
    hegel_run_test(test_config_merge);
    hegel_schema_free(CONFIG);
    return 0;
}
```

### Large collections

The default array length distribution is small. To exercise deep tree paths or multi-level structures, draw the size separately and use it as the array length:

```c
typedef struct { int *keys; int n; } KeySet;
static hegel_schema_t KEYSET;

static hegel_schema_t build_keyset(void) {
    HEGEL_BINDING(n);
    return HEGEL_STRUCT(KeySet,
        HEGEL_LET   (n, HEGEL_INT(0, 300)),                  /* large upper bound */
        HEGEL_ARR_OF(HEGEL_USE(n), HEGEL_INT(0, 1000000)),
        HEGEL_USE   (n));
}

static void test_keyset(hegel_testcase *tc) {
    KeySet *k;
    hegel_shape *sh = hegel_schema_draw(tc, KEYSET, (void **)&k);
    /* ... call function under test on k->keys, k->n ... */
    hegel_shape_free(sh);
}

int main(void) {
    KEYSET = build_keyset();
    hegel_run_test(test_keyset);
    hegel_schema_free(KEYSET);
    return 0;
}
```

Hegel can shrink `n` to the minimal failing size — the broad range doesn't hurt the counterexample. Wrapping the `HEGEL_STRUCT` call in a helper is the idiomatic place for `HEGEL_BINDING` because the binding id has function-local scope.

### Roundtrip on a JSON-like tree

A canonical PBT property: `parse(format(x)) == x`. The full example lives in `tests/selftest/test_schema_recursive_tree_json_roundtrip.c`:

```c
static void test_json_roundtrip(hegel_testcase *tc) {
    Tree *t;
    hegel_shape *sh = hegel_schema_draw(tc, TREE_SCHEMA, (void **)&t);
    char buf[8192];
    json_format(t, buf, sizeof buf);
    Tree *parsed = json_parse(buf);
    HEGEL_ASSERT(tree_equal(t, parsed), "roundtrip mismatch");
    tree_free(parsed);
    hegel_shape_free(sh);
}
```

### Fork-mode caveats

In fork mode the **parent** process holds the Hegel server connection. For each test case it forks a child, the child runs your test function, and draw calls travel back to the parent over a pipe. Implications:

- **Child crashes are caught** (SIGSEGV, SIGABRT) and reported to hegel as failures, which then shrink the input. This is what makes hegel-c crash-safe by default.
- **Global state changes in the child are not visible to the next case** — the child exits after each case. If the library under test expects to be initialized once per process, register a setup callback with `hegel_set_case_setup`.
- **stdout from inside the test function is per-child** — order may be interleaved across cases. Use `hegel_note` for per-case debug output that should show up only on the final replay.
- **MPI** code that calls `MPI_Init` in the test body works only with `MPI_Comm_spawn`-style singletons; see `docs/mpi-testing.md` in the hegel-c repo.

If you need to run without forking (e.g., the library forks internally), use `hegel_run_test_nofork*` — but understand that a SIGSEGV will kill the test runner with no shrinking.

## Gotchas

1. **The schema layout assert fires at schema-build time.** If `HEGEL_STRUCT(T, ...)` aborts with a size mismatch, you've reordered a field, changed a type, or forgotten an entry. The diagnostic prints the declared `sizeof(T)` and the computed total — diff them to find the mismatch.

2. **Refcount-transfer is the default.** When you pass a schema into a parent (`HEGEL_STRUCT`, `HEGEL_OPTIONAL`, `HEGEL_INLINE_REF`, etc.), the reference is moved, not copied. To share a schema across two parents, call `hegel_schema_ref(s)` first. Forgetting this causes a use-after-free; double-bumping leaks.

3. **`HEGEL_BINDING(name)` is function-local.** It expands to `enum { name = __COUNTER__ };`. Per-translation-unit `__COUNTER__` values don't match across files, so don't put `HEGEL_BINDING` declarations in headers.

4. **Length schemas for arrays must be bindings or constants.** Raw `HEGEL_INT(0, 10)` as the length of `HEGEL_ARR_OF` is rejected at schema-build time. Wrap drawn lengths in `HEGEL_LET` so the count is reachable from a sibling field.

5. **`hegel_note` only prints on the final replay.** Don't use it for progress logging.

6. **Width / sign / float-ness mismatches in `HEGEL_USE` are hard aborts.** If you `HEGEL_LET(n, HEGEL_INT(...))` and write `HEGEL_USE_I64(n)`, the runtime aborts. The kinds must match.

7. **Recursive draws are bounded by `HEGEL_DEFAULT_MAX_DEPTH = 50`.** If your schema recurses deeper, switch to `hegel_schema_draw_n` with a higher cap. If every draw bottoms out at the cap, your schema's recursion probability is too high — restructure rather than raise the cap, or you'll get a `hegel_health_fail`.

8. **`HEGEL_ASSERT` formats into a 512-byte buffer.** Long format strings get truncated. For longer messages, format yourself and call `hegel_fail` directly.

9. **Top-level `HEGEL_DRAW` of `ARRAY` / `ARRAY_INLINE` / `SELF` / `ONE_OF_STRUCT` aborts.** These compose only inside a struct. Wrap them.

10. **`target()` is not yet available** in hegel-c. The underlying `hegeltest` 0.4.3 crate doesn't expose Hypothesis's `target()` primitive yet; this is blocked upstream.

11. **No `Settings` struct yet.** Case count is the only configurable knob in v0 (via `_n` runner variants). Seed control, verbosity, and database control are not exposed on the C side.

12. **Add `.hegel/` to `.gitignore`.** First run downloads the server here.

13. **The legacy `hegel_gen_*` combinator API is slated for removal.** Don't write new tests against it. If you find existing tests that use it, migrate them to the schema API.

## Stateful Testing

**hegel-c does not yet have a stateful testing primitive.** Sister bindings (hegel-rust) ship a `#[hegel::state_machine]` macro with `#[rule]` and `#[invariant]` annotations; hegel-c has no equivalent. There is also no shape-tree-aware shrinking of operation sequences as a coherent unit.

This matters because for any stateful data structure (map, BTree, freelist, allocator, transactional store, etc.), the highest-value PBT is a **model test**: run a sequence of operations against both the subject and a known-good reference, assert they agree after every step. The skill's main `SKILL.md` ranks this **first** in the Tier 1 patterns.

### What to do instead

You can hand-roll the operation loop inside a regular hegel-c test. Pick a rule, dispatch, optionally check invariants, repeat. The test's draw calls give you the rule choice and any rule arguments; integrated shrinking will minimize the rule sequence as a flat byte stream (one byte per rule pick, plus rule arguments).

A sketch for a freelist:

```c
enum Op { OP_ALLOC = 0, OP_FREE = 1, OP_USE = 2, OP_END = 3 };

static void test_freelist_model(hegel_testcase *tc) {
    Freelist *fl = freelist_new(64);
    int alloc_log[64];
    int alloc_n = 0;

    int n_steps = HEGEL_DRAW_INT(0, 100);
    for (int step = 0; step < n_steps; step++) {
        int op = HEGEL_DRAW_INT(0, OP_END - 1);
        switch (op) {
        case OP_ALLOC: {
            int h = freelist_alloc(fl);
            if (h >= 0 && alloc_n < 64) alloc_log[alloc_n++] = h;
            break;
        }
        case OP_FREE: {
            if (alloc_n == 0) continue;            /* skip rule, don't discard case */
            int idx = HEGEL_DRAW_INT(0, alloc_n - 1);
            freelist_free(fl, alloc_log[idx]);
            alloc_log[idx] = alloc_log[--alloc_n];
            break;
        }
        case OP_USE: {
            if (alloc_n == 0) continue;
            int idx = HEGEL_DRAW_INT(0, alloc_n - 1);
            HEGEL_ASSERT(freelist_is_live(fl, alloc_log[idx]),
                         "alloc_log[%d]=%d should be live", idx, alloc_log[idx]);
            break;
        }
        }
    }
    freelist_free_all(fl);
}
```

This works, but has limitations vs. a real stateful framework:

- **No invariants are run between steps automatically.** You have to call them by hand.
- **Shrinking is byte-level**, not operation-aware. Hegel will shrink the byte stream, which collapses to "delete a rule" most of the time, but it can't swap or re-order rules cleanly.
- **No "Variables" pool** for tracking dynamically created handles — you write the bookkeeping yourself (`alloc_log` above).

For a non-stateful subject (encoder/decoder, parser, normalizer, pure function), a regular schema-based hegel-c test is often enough — sequential `hegel_draw_*` / `hegel_schema_draw` calls give you everything you need without a state machine. Reserve the hand-rolled approach for genuinely stateful subjects, and reach for hegel-rust if a heavy stateful test is the highest-value next step for the project.

When stateful testing lands in hegel-c, this section will be updated with the rule / invariant API.
