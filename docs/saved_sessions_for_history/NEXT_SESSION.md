<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Next Session — Findings from the test porting & generator investigation

## What we did

Ported 7 new hegel-rust tests to C (i8, i16, u8, u16, u32, sampled_from,
compose_dependent), then consolidated the 8 individual integer test files
into one `test_integers_bounds.c` using macros. Along the way, investigated
the Rust generator architecture in depth to understand what hegel-c is
missing.

## The `find_any` pattern

### What Rust does

`find_any` is a **test helper** (not library API), defined in
`tests/common/utils.rs`. It works by:

1. Drawing a value from the generator
2. Checking the condition
3. If true: **panic** — hegel sees a failure and actively searches for it
4. Catching the panic, extracting the found value

```rust
Hegel::new(move |tc| {
    let value = tc.draw(&self.generator);
    if (self.condition)(&value) {
        *found_clone.lock().unwrap() = Some(value);
        panic!("HEGEL_FOUND");
    }
})
.settings(Settings::new().test_cases(max_attempts))
.run();
```

It is NOT filtering (`assume`). It deliberately fails so hegel's
failure-finding engine hunts for the condition.

### What we built in C

Two pieces that hide the negated-assertion trick:

```c
// Macro: express the positive condition, hegel_fail internally
#define HEGEL_FIND(cond, ...) \
  do { if (cond) { \
    char _hegel_buf[512]; \
    snprintf(_hegel_buf, sizeof(_hegel_buf), __VA_ARGS__); \
    hegel_fail(_hegel_buf); \
  } } while (0)

// Runner: interpret rc=1 ("test failed") as "found one"
static int find_any(void (*fn)(hegel_testcase *), uint64_t n) {
  return hegel_run_test_result_n(fn, n) == 1 ? 0 : 1;
}
```

### Why `hegel_assume` doesn't work for `find_any`

We tried option 2 (assume-based filtering). Hypothesis's health check kills
the test when too many inputs are discarded — `assume(n == INT8_MIN)`
filters 255/256 of cases, triggering "filter_too_much" after 50 discards.

`assume` is for mild filtering ("skip if denominator is 0"), not for
finding needles in haystacks. The negated-assertion trick works because
it uses hegel's failure-finding engine, which actively targets the
condition rather than passively filtering.

## Why C tests are verbose — the closure gap

### The Rust version (5 lines)

```rust
fn test_u8() {
    assert_all_examples(gs::integers::<u8>(), |&n| n >= u8::MIN && n <= u8::MAX);
    find_any(gs::integers::<u8>(), |&n| n > u8::MAX / 2);
    find_any(gs::integers::<u8>(), |&n| n == u8::MIN);
    find_any(gs::integers::<u8>(), |&n| n == u8::MAX);
}
```

Each line passes two things to a helper: a generator and an inline
predicate (closure/lambda — `|&n| n > 127`).

### Why C can't match this

C has no closures. Each predicate must be a separate named function:

```c
static void u8_find_upper(hegel_testcase *tc) {
    int n = hegel_draw_int(tc, 0, UINT8_MAX);
    HEGEL_FIND(n > UINT8_MAX / 2, "n=%d", n);
}
```

The generator and predicate are baked into each function. You can't
separate them and pass them to a helper like Rust does. That's why
5 Rust lines become 5 C functions + a main() that wires them.

### How we reduced the boilerplate

Macros generate the functions from a one-liner:

```c
UNSIGNED_TESTS(u8, hegel_draw_int, int, UINT8_MAX, "%d")
```

This expands to 4 functions (bounds, find_upper, find_min, find_max).
The `RUN_UNSIGNED` macro generates the runner. 8 files (~700 lines)
collapsed into 1 file (~130 lines).

### Alternative: global-config approach

Instead of macros generating functions, use one generic function
parameterized by a global struct:

```c
static int find_any_int(int min, int max, int op, int64_t val) {
    SPEC = (find_spec){ min, max, op, val };
    return find_any(generic_find, 1000);
}

// Usage — 4 lines per type, no macro-generated functions:
errors += find_any_int(0, UINT8_MAX, GT, UINT8_MAX / 2);
errors += find_any_int(0, UINT8_MAX, EQ, 0);
errors += find_any_int(0, UINT8_MAX, EQ, UINT8_MAX);
```

Safe because tests run sequentially. Not yet implemented.

### The real fix: `hegel_run_test_result_ctx`

If the API accepted a `void *ctx` parameter (like `pthread_create`):

```c
int hegel_run_test_result_ctx(
    void (*fn)(hegel_testcase *, void *), void *ctx, uint64_t n);
```

Then you could write one generic function and pass different parameters.
This is an FFI change in the Rust bridge — the long-term solution.

## Draw ordering and shrinking

### The byte stream model

Hegel (via Hypothesis) generates a single byte stream. Every `draw` call
consumes from it in order. Shrinking works by making the byte stream
shorter or lexicographically smaller, then replaying all draws in the
same order.

Same principle as React hooks — called in the same order every run,
identified by position, not by name.

### Conditional draws degrade shrinking

```c
int tag = hegel_draw_int(tc, 0, 2);
if (tag == 0) {
    int x = hegel_draw_int(tc, 0, 100);  // 2 draws total
} else {
    int x = hegel_draw_int(tc, 0, 100);
    int y = hegel_draw_int(tc, 0, 100);  // 3 draws total
}
int z = hegel_draw_int(tc, 0, 100);  // position depends on tag
```

When hegel shrinks by tweaking early bytes, `tag` might change, shifting
every subsequent draw position. Shrinking one variable accidentally
mutates all later ones. It still finds bugs, but the shrunk
counterexample might not be minimal.

### How Rust handles this: spans

Rust doesn't magically solve conditional draws. `FlatMapped::do_draw`:

```rust
fn do_draw(&self, tc: &TestCase) -> U {
    tc.start_span(labels::FLAT_MAP);
    let intermediate = self.source.do_draw(tc);
    let next_gen = (self.f)(intermediate);
    let result = next_gen.do_draw(tc);
    tc.stop_span(false);
    result
}
```

`start_span` / `stop_span` tell the Hegel server "these draws belong
together." Hypothesis can shrink the span as a unit — it knows which
bytes correspond to this block, so when early bytes change and the
branch shifts, it can discard the whole span instead of blindly
replaying bytes into different draws.

**We don't have `start_span` / `stop_span` in the C API.** That's the
real missing piece for structural shrinking quality.

## Generator architecture — what hegel-c has vs what it needs

### What exists today

hegel-c has composable generator objects (`hegel_gen`):

- **Leaf generators**: `hegel_gen_int`, `_i64`, `_u64`, `_float`,
  `_double`, `_bool`, `_text`, `_regex`
- **Combinators**: `hegel_gen_one_of`, `_sampled_from`, `_optional`,
  `_map_*`, `_filter_*`, `_flat_map_*`
- **Draw functions**: `hegel_gen_draw_int`, `_draw_list_int`, etc.

These work and compose, but only produce **scalars and flat lists of
primitives**.

### What's missing for real compositional PBT

1. **Lists of arbitrary generators** — `hegel_gen_draw_list` only handles
   primitives. Can't do "list of structs" or "list of lists."

2. **Record/struct generators** — No `hegel_gen_record` to describe a
   struct with named fields of different types.

3. **Spans** — No `start_span`/`stop_span` to group draws for better
   shrinking of conditional/recursive generation.

4. **Context-passing API** — `hegel_run_test_result_n` takes a bare
   function pointer. No `void *ctx` parameter, so each test function
   must hardcode its parameters or use globals.

### Two possible directions

**Option A: Generic value tree (`hegel_val`)**

Generators produce an opaque result tree. Navigate with accessors:

```c
hegel_gen *g = hegel_gen_record(
    "name", hegel_gen_text(1, 50),
    "age",  hegel_gen_int(0, 150),
    NULL);
hegel_val *v = hegel_gen_draw(tc, g);
const char *name = hegel_val_text(hegel_val_field(v, "name"));
hegel_val_free(v);
```

Pro: composable, handles unions naturally (one_of between records).
Con: no type safety, verbose accessor API, big implementation effort.

**Option B: Typed callbacks + arena**

User defines their own structs, provides a callback to fill them in.
Arena-allocated memory freed at test end:

```c
typedef struct { char *name; int age; } person;

void gen_person(hegel_testcase *tc, void *out) {
    person *p = out;
    p->name = hegel_draw_text_alloc(tc, 1, 50);
    p->age  = hegel_draw_int(tc, 0, 150);
}

person people[20];
int n = hegel_draw_composed(tc, gen_person, sizeof(person),
                            1, 20, people, 20);
```

Pro: type-safe, idiomatic C, less library code.
Con: conditional draws inside callbacks have the shrinking problem
(no spans), manual union definition for sum types.

**Option A is better for shrinking** because the generator tree declares
the structure upfront — hegel can see the full schema before drawing.
Option B relies on runtime draw sequences, which the server can't
introspect.

### The real missing primitive: spans

Regardless of Option A vs B, adding `hegel_start_span` / `hegel_stop_span`
to the C API would immediately improve shrinking quality for any
structured generation — recursive strategies, conditional draws,
flat_map. This is lower effort than a full generator redesign and
addresses the core architectural gap.

## Priority ranking for next work

1. **Spans** (`hegel_start_span` / `hegel_stop_span`) — small FFI
   addition, big shrinking improvement for existing patterns
2. **`hegel_run_test_result_ctx`** — add `void *ctx` to the test runner,
   enables global-config-free `find_any_int` and parameterized tests
3. **Arena allocator** on `HegelTestCase` — auto-free generated strings
   and buffers at test case end
4. **`hegel_gen_record` + `hegel_val`** (Option A) or
   **`hegel_draw_composed`** (Option B) — full compositional generation
5. **Lists of arbitrary generators** — `hegel_gen_draw_list` that takes
   a callback or a record generator

## Files changed in this session

- `tests/from-hegel-rust/test_integers_bounds.c` — NEW, consolidated
  8 integer tests into 1 file with `HEGEL_FIND` + `find_any` helpers
- `tests/from-hegel-rust/test_combinators_sampled_from.c` — NEW
- `tests/from-hegel-rust/test_compose_dependent.c` — NEW
- `tests/from-hegel-rust/Makefile` — updated test lists, SPDX in
  generated reports
- `tests/from-hegel-rust/manifest.md` — updated ported/not-ported lists
- `CLAUDE.md` — updated test counts
- `README.md` — updated test counts and TODO
- Deleted: 8 individual `test_integers_*_bounds.c` files

---

what I think we should look for: a system that lets the user define intricated structs by calling helpers
generates any kind of data on draw:
a single integer, a pointer to a malloced struct, that could contain malloced arrays...
also, a separate struct tree that describes the actual form of the generated data?
  so that you can pass arguments like the size of arrays to the function you try to test?
    random tired ideas lol

---

## Why the "tired note" is actually the right direction

The instinct above lands on something the rest of this document only circles
around: **two parallel trees**, not one.

- **Tree 1 — the value.** A `malloc`'d struct, possibly containing pointers to
  more `malloc`'d structs, arrays, strings. This is what gets passed to the
  function under test. It's whatever shape the user actually wants to fuzz.
- **Tree 2 — the shape.** A description of what was generated: "this is a
  list of length 7", "this field is an int in [0, 100]", "this pointer is
  optional and was Some". Same skeleton as the value, but it carries
  *metadata* — sizes, tags, ranges, provenance.

Why both trees, instead of just the value?

1. **C lacks self-describing data.** In Rust, `Vec<T>` carries its length.
   In Python, `list` knows `len()`. In C, an array is a pointer and you
   *must* pass the length separately. So when you generate `int *arr` of
   length 7, the function under test needs `(arr, 7)` — the 7 has to come
   from somewhere. The shape tree is where it lives. This is a real
   C-specific need that doesn't exist in any other PBT library, because no
   other PBT library targets a language where length is out-of-band.

2. **The shape tree is what the framework needs for shrinking.** Hegel can't
   shrink a `void *` — it has no idea what's inside. But it *can* shrink
   "the integer at shape_tree.fields[2].fields[0]" or "drop element 4 of
   shape_tree.fields[1]" if the shape tree exposes that structure. The
   shape tree is the bridge between user-defined C memory layouts and the
   framework's shrinking engine.

3. **Memory ownership becomes free.** If the shape tree owns the value tree
   (each node holds a pointer to the bytes it described), then
   `hegel_free_generated(shape)` walks the tree and frees everything. No
   arena, no manual cleanup, no leaks. The user never writes a `free()`.

4. **Helpers compose into trees naturally.** Each helper
   (`hegel_gen_int`, `hegel_gen_struct`, `hegel_gen_array_of`) returns a
   node. Pass nodes to other helpers, you get a sub-tree. The user is
   *literally building the shape tree* by calling helpers — they don't
   think about it as "describing a schema", they think about it as
   "generating my struct".

5. **It mirrors how Hypothesis actually works internally.** Hypothesis has
   the byte stream *and* an "IR" (intermediate representation) that
   describes the draws as a tree of "examples". Every successful PBT
   library converges on this two-layer design eventually. Your gut is
   pointing at it without having read the Hypothesis internals — that's
   the engineer's-gut-feeling part.

The reason this is better than Option A (`hegel_val` accessor API) is that
in Option A the user *queries* the value tree to get their data out, which
is verbose and untyped. With the two-tree approach, the user gets *their
own struct* directly — they defined it, they own the layout, the framework
just hands it to them along with a shape handle for the metadata.

The reason it's better than Option B (typed callbacks + arena) is that
Option B's callbacks are opaque to the framework — once you're inside
`gen_person`, the framework has no idea you're building a person, it just
sees a sequence of draws. The shape tree fixes that by making the
structure explicit *as a side effect of using the helpers*.

So the priority list at the top of this doc should really be:

1. **Spans** (still first — they're a prerequisite for everything below)
2. **A two-tree generator API** — helpers build a shape tree; the shape
   tree owns the value tree; the user gets a typed pointer to their struct
   plus a shape handle for sizes/tags
3. Everything else (`ctx` parameter, arena, lists of generators) becomes
   either subsumed or trivial once the two-tree model exists

---

## What spans are, concretely

A **span** is a pair of markers — `start_span(label)` and `stop_span()` —
injected into the byte stream that hegel generates from. The bytes
themselves are unchanged; the span is just metadata layered on top that
says: *"the bytes between these two markers belong to one logical unit."*

### The byte stream, without spans

Hegel generates a single linear sequence of bytes. Every `hegel_draw_*`
call consumes some bytes from the front:

```
bytes:    [0x07][0x42][0xa1][0x05][0xff][0x00][0x13]...
draws:     ^len  ^elem ^elem ^elem ^elem ^elem ^elem
           "list of length 7, then 7 ints"
```

When hegel wants to shrink a failing input, it tries operations on the
byte stream: shorten it, swap bytes for smaller ones, delete a byte, etc.
Then it *replays* generation from the modified bytes and checks if the
test still fails.

The problem: the shrinker doesn't know which bytes mean what. If it
deletes one byte from the middle, the length prefix is now 7 but only 6
elements follow — and worse, every subsequent draw shifts position. A
"shorten by one element" operation, which is what you actually want, is
not something the shrinker can express.

### The byte stream, with spans

Now wrap each logical group:

```
bytes:    [0x07][0x42][0xa1][0x05][0xff][0x00][0x13]...
spans:    └─list───────────────────────────────────┘
                └elem┘└elem┘└elem┘└elem┘└elem┘└elem┘
```

The shrinker now has a *tree* of byte ranges. It can perform structural
operations on that tree:

- **Delete a span** — drop one `elem` span entirely; the remaining bytes
  re-replay as a length-6 list. Clean shrink.
- **Minimize a span** — reduce the bytes inside one `elem` span without
  touching the others. Shrinks one field at a time.
- **Reorder spans** — try sorting sibling spans. Useful for "find the
  smallest element that triggers the bug".
- **Duplicate a span** — sometimes the bug only triggers on repeated
  values; copy a span over its sibling.

In Hypothesis these are literally implemented as shrink passes:
`pass_to_shrinker.delete`, `minimize_individual_blocks`, etc. They each
walk the span tree and try one structural operation at every node.

### Spans nest, which is the whole point

```
spans:    └────────list of structs──────────┘
                └─struct─┘  └─struct─┘
                 └f1┘└f2┘    └f1┘└f2┘
```

The shrinker tries operations at every level. At the outer level: "drop a
struct". At the middle level: "minimize this struct". At the leaf level:
"shrink this int". Each level is a different *kind* of simplification, and
they all compose.

### Why spans are the prerequisite for the two-tree API

Look back at the two-tree section. The shape tree has a structure: list
of length 7 → 7 elements → each element a struct → each struct has fields.
For the framework to shrink that intelligently, it needs to know which
bytes in the stream correspond to which node in the shape tree. **Spans
are exactly that mapping.**

Concretely: when a helper like `hegel_gen_array_of(elem_gen, 0, 100)` runs,
it would:

```
hegel_start_span("array");
  hegel_start_span("length");
    int n = draw_int(0, 100);  // bytes for the length
  hegel_stop_span();
  for (int i = 0; i < n; i++) {
    hegel_start_span("elem");
      elem_gen->draw();        // bytes for this element
    hegel_stop_span();
  }
hegel_stop_span();
```

Now the shape tree built by the helpers and the span tree in the byte
stream have the *same shape*. The shrinker can map operations on the shape
tree ("drop element 4") onto operations on the byte stream ("delete the
4th `elem` span"), and the replay produces a valid, shorter input.

Without spans, the shape tree is just a memory layout — the shrinker can
see the structure but can't act on it. With spans, the shape tree becomes
a control surface for shrinking. **That's why spans are item 1 on the
priority list and why everything else hinges on them.**
