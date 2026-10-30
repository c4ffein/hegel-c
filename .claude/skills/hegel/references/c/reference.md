# Hegel C Reference

## Table of Contents

- [Setup](#setup) — vendor, static link, `gcc` flags, the engine library
- [Test Structure](#test-structure) — runners, suites, per-case setup, run options, replaying a failure
- [TestCase Functions](#testcase-functions) — `hegel_draw_*`, `HEGEL_DRAW_*`, `hegel_assume`, `hegel_note`, `hegel_fail`, `HEGEL_ASSERT`, `hegel_target`
- [Writing Generators](#writing-generators) — dependent fields, arrays, spans, optionals, one-of, recursion, map / filter / flat_map
- [C-Specific Examples](#c-specific-examples)
- [Crash Isolation](#crash-isolation)
- [Gotchas](#gotchas)
- [Stateful Testing](#stateful-testing)

## Setup

hegel-c is **not yet published as a package**. Vendor it as a submodule and build the static library and the engine:

```bash
git submodule add https://github.com/c4ffein/hegel-c third_party/hegel-c
make -C third_party/hegel-c lib libhegel PREBUILT=1
```

`make lib` builds `build/libhegel_c.a` (pure C, seconds). `make libhegel PREBUILT=1` downloads Hegel's native engine, `libhegel.so`, as a checksum-verified release artifact into `build/` (without `PREBUILT=1` it builds it from source with cargo). The static library loads the engine at runtime with `dlopen`.

```make
CFLAGS  += -I third_party/hegel-c
LDFLAGS += -L third_party/hegel-c/build
LDLIBS  += -lhegel_c -lpthread -lm -ldl
```

When linking against additional libraries (e.g., the C library you're testing), append their flags **after** `-lhegel_c`. Standard linker rules apply: a library must come before the libraries it depends on. A Scotch test, for example, links `-lhegel_c -lscotch -lscotcherr -lz -lm -lpthread -ldl -lrt`.

A standalone compile of one test:

```bash
gcc -O2 -I third_party/hegel-c -o test_foo test_foo.c \
    -L third_party/hegel-c/build -lhegel_c -lpthread -lm -ldl
```

#### Finding the engine at runtime

The runtime looks for `libhegel.so` in this order: `$HEGEL_LIBHEGEL_PATH`, `./build/libhegel.so`, `./.hegel/libhegel/libhegel.so`, then the system loader path. The `./` paths are relative to the **current working directory**, so either export `HEGEL_LIBHEGEL_PATH`, or run test binaries from the directory that holds `build/libhegel.so`:

```make
REPO_ROOT := $(abspath ../..)

test: $(BIN)
	cd $(REPO_ROOT) && $(CURDIR)/$(BIN)
```

**One header:** `#include "hegel_c.h"` — draws, the `HEGEL_DRAW_*` shorthands, spans, the engine protocols (collections, recursion, pools), runners, assertions, stateful testing.

## Test Structure

A hegel-c test is a regular C function with the signature `void test_fn(hegel_testcase *tc)`. You hand it to one of the test runners:

```c
/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 Your Name
** Part of yourproject — see LICENSE for terms. */

#include "hegel_c.h"

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
| `hegel_run_test_with(fn, &opts)` | fork | from `opts` | Returns `0`/`1` |
| `hegel_run_test_nofork(fn)` / `_n(fn, n)` / `_with(fn, &opts)` | nofork | 100 / `n` / `opts` | No per-case isolation — use `HEGEL_ISOLATED` (below) |

**Fork mode is the default and what you want.** Each test case runs in a forked child; the parent owns the engine, serves the child's draws over a pipe, and turns a child crash (SIGSEGV, SIGABRT) into a failure that shrinks. **Nofork mode** runs the body in the runner's own process — for benchmarking, code that can't be forked, or when you want crash isolation only around the code under test (see [Crash Isolation](#crash-isolation)).

The `_result_*` and `_with` variants don't `exit()` on failure, so a binary can run several tests and return its own status.

> **Success is silent.** A passing run prints nothing. Output appears only for the minimal failing case (its `hegel_note` lines and the assertion message). If you're worried "is the test actually exercising my code?", **temporarily invert the property** (e.g., flip `==` to `!=`) and re-run — a failing inversion confirms the test runs *and* shows what a counterexample report looks like.

### Suites

For multiple tests in one binary, use a suite (each test is its own engine run):

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

If the library under test has **process-wide global state** that must be reset between cases — internal RNG seeds, allocator arenas, cached lookup tables — register a setup callback:

```c
static void reset_library(void) {
    mylib_set_seed(42);
    mylib_clear_caches();
}

int main(void) {
    hegel_set_case_setup(reset_library);
    hegel_run_test(test_something);
    return 0;
}
```

Pass `NULL` to clear. The callback runs before each case, in fork and nofork mode.

You usually don't need this in fork mode: each case is a fresh child, so in-process state (heap, statics) starts from the parent's every time. It matters for a library RNG you want deterministic, or for state that would leak between cases in nofork mode.

### Run options

```c
hegel_run_options o = { .test_cases = 1000,
                        .suppress_health = HEGEL_SUPPRESS_FILTER_TOO_MUCH };
int rc = hegel_run_test_with(test_foo, &o);
```

Fields: `test_cases`, `has_seed` + `seed`, `suppress_health` (`HEGEL_SUPPRESS_*` bits), `skip_phases` (`HEGEL_SKIP_*` bits, e.g. `HEGEL_SKIP_SHRINK`). Zero fields keep the engine default. The engine also reads `HEGEL_*` environment variables (`HEGEL_TEST_CASES`, `HEGEL_SEED`, `HEGEL_DATABASE`, ...); a field set in code wins.

### Replaying a failure

Every failure report ends with a line like `reproduce: HEGEL_C_REPRODUCE='1:AAEAAAAACgEAAABl'`. Rerun the same binary with that variable set and it replays exactly that failing case (the number is the run within the binary, in call order; the binary's other runs are skipped). Once the bug is fixed, the replay prints "did not reproduce" and passes — the quick way to check a fix.

## TestCase Functions

The opaque `hegel_testcase *tc` handle is passed into your test function. Don't store it past the end of the test callback.

> **Name the parameter `tc`.** The `HEGEL_DRAW_*` shorthands expand to `hegel_draw_*((tc), …)` — they use a variable literally spelled `tc` in scope. Name helper functions' parameter `tc` too, or call the `hegel_draw_*` functions directly.

| Macro / function | Purpose |
|---|---|
| `HEGEL_DRAW_INT()` / `HEGEL_DRAW_INT(lo, hi)` | Draw `int` (full range, or `[lo, hi]`) — `hegel_draw_int(tc, lo, hi)` |
| `HEGEL_DRAW_I64()` / `HEGEL_DRAW_I64(lo, hi)` | Draw `int64_t` — `hegel_draw_i64` |
| `HEGEL_DRAW_U64()` / `HEGEL_DRAW_U64(lo, hi)` | Draw `uint64_t` — `hegel_draw_u64` |
| `HEGEL_DRAW_FLOAT()` / `HEGEL_DRAW_FLOAT(lo, hi)` | Draw `float` — `hegel_draw_float` |
| `HEGEL_DRAW_DOUBLE()` / `HEGEL_DRAW_DOUBLE(lo, hi)` | Draw `double` — `hegel_draw_double` |
| `HEGEL_DRAW_BOOL()` | Draw `bool` |
| `hegel_draw_usize(tc, lo, hi)` | Draw `size_t` |
| `hegel_draw_text(tc, min, max, buf, cap)` | Write a null-terminated string of length in `[min, max]` into `buf`; returns its length |
| `hegel_draw_regex(tc, pattern, buf, cap)` | Write a null-terminated string that fully matches `pattern` into `buf` |
| `hegel_draw_bytes(tc, min, max, buf, cap)` | Write arbitrary bytes (may include `0x00`) into `buf`; returns the length — one span, so the shrinker can drop the whole buffer |
| `hegel_assume(tc, cond)` | Discard this case if `cond == 0` (not a failure) |
| `hegel_note(tc, msg)` | Attach `msg` to the report of the minimal failing case |
| `hegel_target(tc, score, label)` | Steer later cases toward higher `score` (needs ~1000 cases to get going) |
| `hegel_fail(msg)` | Fail the test with `msg`; triggers shrinking |
| `hegel_health_fail(msg)` | Fail with `"Health check failure: " + msg` — the test setup is broken, not the code under test; no shrinking |
| `HEGEL_ASSERT(cond, fmt, ...)` | If `!cond`, format (512-byte buffer) and fail; the origin is the assert's `file:line`, so distinct asserts are distinct bugs |

Float draws with both bounds finite never produce NaN; infinities only come from an unbounded end. Text draws never contain NUL.

```c
static void test_division(hegel_testcase *tc) {
    int64_t a = HEGEL_DRAW_I64();
    int64_t b = HEGEL_DRAW_I64();
    hegel_assume(tc, b != 0);
    hegel_assume(tc, !(a == INT64_MIN && b == -1));   /* overflow, not the property */
    char buf[128];
    snprintf(buf, sizeof buf, "a=%lld b=%lld", (long long)a, (long long)b);
    hegel_note(tc, buf);
    int64_t q = a / b;
    int64_t r = a % b;
    HEGEL_ASSERT(a == q * b + r, "a != q*b + r");
}
```

`hegel_note` is dropped everywhere except the report of the minimal counterexample. Don't use it for progress logging.

`hegel_fail` and `hegel_health_fail` differ in intent:

- **`hegel_fail`** — the **code under test** is broken. Shrink, show the user a minimal counterexample.
- **`hegel_health_fail`** — the **test setup** is broken (a fixture can't be built, an allocation for the harness failed). The user needs to fix the test, not the code.

## Writing Generators

A generator is a plain C function that fills a value from draws. Every draw is recorded by the engine, so the value shrinks without any shrink code from you. There is no separate "strategy" object: dependent generation, filtering and mapping are ordinary C.

### Structs and dependent fields

Draw fields in order; a field that depends on another is a bound computed from what was already drawn:

```c
typedef struct {
    int  nvert;
    int  u, v;          /* an edge: u < v < nvert */
} Edge;

static void gen_edge(hegel_testcase *tc, Edge *e) {
    e->nvert = HEGEL_DRAW_INT(2, 50);
    e->u     = HEGEL_DRAW_INT(0, e->nvert - 2);
    e->v     = HEGEL_DRAW_INT(e->u + 1, e->nvert - 1);
}
```

Prefer this to drawing wide and repairing afterwards (`u % nvert`, swapping, stripping): the repair hides from the shrinker which choice mattered, and the counterexample comes out larger.

### Arrays: a drawn length and list spans

```c
typedef struct { int *items; int n; } Bag;

static void gen_bag(hegel_testcase *tc, Bag *b) {
    hegel_start_span(tc, HEGEL_SPAN_LIST);
    b->n = HEGEL_DRAW_INT(0, 100);
    b->items = malloc((size_t)(b->n ? b->n : 1) * sizeof *b->items);
    for (int i = 0; i < b->n; i++) {
        hegel_start_span(tc, HEGEL_SPAN_LIST_ELEMENT);
        b->items[i] = HEGEL_DRAW_INT();
        hegel_stop_span(tc, 0);
    }
    hegel_stop_span(tc, 0);
}
```

The spans tell the shrinker where each element starts and ends, so it can delete whole elements instead of nudging values. A drawn length covers the whole range — use it when large inputs matter.

### Arrays: engine-owned length (collections)

```c
hegel_collection c = hegel_collection_new(tc, 0, 100);    /* min, max */
while (hegel_collection_more(tc, c)) {
    int x = HEGEL_DRAW_INT(0, 1000);
    if (seen(set, x)) { hegel_collection_reject(tc, c); continue; }   /* unique elements */
    add(set, x);
}
hegel_collection_free(tc, c);
```

The engine decides when to stop and shrinks by deleting elements; `hegel_collection_reject` says the last element was invalid (a duplicate) so it doesn't count. The engine aims for **about 5 elements whatever the max** — fine for "a few things", wrong when the code under test only misbehaves on large inputs. Use a drawn length then.

### Optionals and one-of

An optional is a coin and then the value; a one-of is a drawn tag and a `switch`. A span around each keeps the alternatives apart for the shrinker:

```c
static char *gen_maybe_name(hegel_testcase *tc, char *buf, int cap) {
    char *name = NULL;
    hegel_start_span(tc, HEGEL_SPAN_OPTIONAL);
    if (HEGEL_DRAW_BOOL()) {
        hegel_draw_text(tc, 0, 16, buf, cap);
        name = buf;
    }
    hegel_stop_span(tc, 0);
    return name;
}

typedef struct { int kind; union { int i; double d; } as; } Value;

static void gen_value(hegel_testcase *tc, Value *v) {
    hegel_start_span(tc, HEGEL_SPAN_ONE_OF);
    v->kind = HEGEL_DRAW_INT(0, 1);
    switch (v->kind) {
    case 0:  v->as.i = HEGEL_DRAW_INT();             break;
    default: v->as.d = HEGEL_DRAW_DOUBLE(-1e9, 1e9); break;
    }
    hegel_stop_span(tc, 0);
}
```

Order the cases simplest first: the shrinker moves the tag toward 0.

### Recursive values

Trees and expressions go through the engine's recursion protocol, which owns the branch probabilities, the depth cap and a leaf budget. Each sub-value asks `hegel_recursion_branch`; a leaf calls `hegel_recursion_leaf` first, and a non-zero return means the attempt outgrew its budget — unwind without drawing anything more, free the half-built value, and start again from the root:

```c
typedef struct Node { struct Node *left, *right; int value; } Node;   /* leaf: both NULL */

static void node_free(Node *n) {
    if (!n) return;
    node_free(n->left); node_free(n->right); free(n);
}

/* NULL means "over the leaf budget: unwind". */
static Node *gen_node(hegel_testcase *tc, hegel_recursion rec, uint64_t depth) {
    Node *n = calloc(1, sizeof *n);
    if (hegel_recursion_branch(tc, rec, depth)) {
        n->left = gen_node(tc, rec, depth + 1);
        if (n->left)
            n->right = gen_node(tc, rec, depth + 1);
        if (!n->left || !n->right) { node_free(n); return NULL; }
        return n;
    }
    if (hegel_recursion_leaf(tc, rec)) { free(n); return NULL; }
    n->value = HEGEL_DRAW_INT(0, 9);
    return n;
}

static Node *gen_tree(hegel_testcase *tc) {
    hegel_recursion rec = hegel_recursion_new(tc, 6, 20);   /* max depth, max leaves */
    Node *root;
    for (;;) {
        root = gen_node(tc, rec, 0);
        if (!root) { hegel_recursion_retry(tc, rec); continue; }   /* discards the case when exhausted */
        if (hegel_recursion_finish(tc, rec) == 0) break;
        node_free(root);                                          /* rejected: start again */
    }
    hegel_recursion_free(tc, rec);
    return root;
}
```

A coin flip per child with a hand-written depth cap also works, but makes much smaller trees (around 5 nodes on average) than the protocol (dozens), and the protocol's values shrink the way every other Hegel frontend's do.

### map / filter / flat_map

There are no combinator objects — write the C:

- **map:** transform after drawing — `int even = 2 * HEGEL_DRAW_INT(0, 50);`
- **filter:** draw, then `hegel_assume(tc, pred(x));` — but narrow the range instead whenever you can. A predicate that rejects most draws trips the `FilterTooMuch` health check.
- **flat_map:** draw, then draw again with bounds or a shape taken from the first value — the dependent-fields pattern above.

None of these degrade shrinking: the engine shrinks the recorded choices and replays your code.

### Spans

Spans group the draws that make one logical thing — a list element, a struct, a variant — so the shrinker can treat them as a unit. Use the `HEGEL_SPAN_*` constants for the usual shapes, or `hegel_label("mylib.thing")` for your own (distinct generators should get distinct labels). Spans nest; always pair `hegel_start_span` / `hegel_stop_span`. `hegel_stop_span(tc, 1)` marks the span a dead end (e.g. an element you rejected).

## C-Specific Examples

### Wrapping a library that needs setup, teardown, and valid input

Most real C libraries need more than a draw. They have multi-step setup (init handle, configure, allocate buffers), input invariants (graph symmetry, tree acyclicity, sorted arrays, no duplicate keys), and matching teardown. The pattern: **draw, construct a valid value, assert, clean up with `goto cleanup`.**

```c
/* Hypothetical hash-set library:
**
**   typedef struct MySet MySet;
**   MySet *myset_new(int capacity);          // capacity must be >= 1
**   int    myset_insert(MySet *s, int key);  // 0 if new, 1 if dup
**   int    myset_contains(const MySet *s, int key);
**   void   myset_free(MySet *s);             // NULL-safe
*/

static void test_inserted_keys_are_contained(hegel_testcase *tc) {
    int    cap  = HEGEL_DRAW_INT(1, 64);          /* respect library precondition */
    int    n    = HEGEL_DRAW_INT(0, 100);
    int   *keys = NULL;
    MySet *s    = NULL;

    if (n > 0) {
        keys = malloc((size_t)n * sizeof *keys);
        if (keys == NULL) { hegel_health_fail("keys malloc"); goto cleanup; }
    }
    for (int i = 0; i < n; i++) keys[i] = HEGEL_DRAW_INT();

    s = myset_new(cap);
    if (s == NULL) { hegel_health_fail("myset_new returned NULL"); goto cleanup; }

    for (int i = 0; i < n; i++) myset_insert(s, keys[i]);

    for (int i = 0; i < n; i++) {
        HEGEL_ASSERT(myset_contains(s, keys[i]),
                     "key %d not contained after insert", keys[i]);
    }

cleanup:
    myset_free(s);          /* NULL-safe */
    free(keys);             /* NULL-safe per C standard */
}
```

Key patterns:

- **`goto cleanup`** is the C-idiomatic answer to nested early-returns. Every fail path lands at the same label; the cleanup block frees in reverse order of acquisition.
  - For libraries that initialize multiple objects each with their own destructor (handle + strategy + buffer), keep an `_inited` int per object and destroy in reverse order:
    ```c
    int g_init = 0, s_init = 0;
    if (mylib_graph_init(&g) != 0) { ...; goto cleanup; }
    g_init = 1;
    if (mylib_strat_init(&s) != 0) { ...; goto cleanup; }
    s_init = 1;
    /* property */
    cleanup:
        if (s_init) mylib_strat_exit(&s);
        if (g_init) mylib_graph_exit(&g);
    ```
- **`hegel_health_fail` for setup failures**, `HEGEL_ASSERT` for the actual property check — "the harness is broken" stays distinguishable from "the library has a bug."
- **NULL-safe destructors** (`myset_free`, `free`) keep the cleanup block branch-free.
- **Document preconditions in comments** at the top of the test, not in the assertion message.

In fork mode a failing case's child exits, so leaks on failure paths don't matter; free on the passing path so nofork runs and sanitizers stay quiet.

#### When the *input* itself has a non-local invariant

For structured input with cross-element constraints — symmetric adjacency lists, sorted arrays, balanced trees, valid UTF-8, file-format magic numbers — **don't `hegel_assume` the invariant over random input**: the rejection rate kills the test. Draw choices and *construct* a valid value:

```c
/* Symmetric-adjacency construction sketch:
**   for each unordered pair (i, j):
**     if HEGEL_DRAW_BOOL(): add edge i-j
**   build CSR / adjacency list from the resulting matrix.
**
** Every output is a valid graph — no rejection.
*/
```

One shrinkable choice per potential edge lets the shrinker delete each edge independently to reach the minimal counterexample.

### A config struct

```c
typedef struct {
    uint32_t max_retries;
    uint64_t timeout_ms;
    char     name[65];
} Config;

static void gen_config(hegel_testcase *tc, Config *c) {
    hegel_start_span(tc, hegel_label("myapp.config"));
    c->max_retries = (uint32_t)HEGEL_DRAW_U64(0, UINT32_MAX);
    c->timeout_ms  = HEGEL_DRAW_U64();
    hegel_draw_text(tc, 0, 64, c->name, sizeof c->name);
    hegel_stop_span(tc, 0);
}

static void test_config_merge(hegel_testcase *tc) {
    Config base, override;
    gen_config(tc, &base);
    gen_config(tc, &override);
    Config merged = config_merge(&base, &override);
    HEGEL_ASSERT(strcmp(merged.name, override.name) == 0,
                 "merged.name should equal override.name");
}
```

### Roundtrip on a JSON-like tree

A canonical PBT property: `parse(format(x)) == x`. A complete example, with a recursive generator and spans per node and per optional field, is `tests/selftest/test_span_tree_json_passes.c` in the hegel-c repo; its deliberately buggy twin `test_span_tree_json_shrinks.c` shows the shrinker reducing a failure to one node with two different leaf children.

## Crash Isolation

In **fork mode** every case already runs in its own child: a crash anywhere in the test is a shrinkable failure.

In **nofork mode**, isolate just the code under test with `HEGEL_ISOLATED`. Generate first, then call the code in the block:

```c
static void test_build(hegel_testcase *tc) {
    Graph g;
    gen_graph(tc, &g);                 /* draws: outside the block */
    HEGEL_ISOLATED(tc) {               /* forked; sees g copy-on-write */
        HEGEL_ASSERT(graph_build(&g) == 0, "build failed");
    }
}

int main(void) { hegel_run_test_nofork(test_build); return 0; }
```

Rules: no draws, spans or collection calls inside the block (a health failure says so), no nesting, and don't leave it with `break` / `return` / `goto`. Every failure inside one block — crash, abort, `HEGEL_ASSERT` — counts as one bug, so code whose bug sometimes crashes and sometimes returns garbage shrinks as a single failure.

Fork-mode notes:

- **Global state changes in the child are not visible to the next case** — the child exits after each case. Use `hegel_set_case_setup` for per-case initialization.
- **stdout from inside the test is per-child** and may interleave across cases. Use `hegel_note` for output that belongs to the counterexample.
- **Code that reads uninitialized or freed memory** can behave differently from case to case, because each child inherits the parent's heap as it is at fork time. `hegel_set_fork_mode(HEGEL_FORK_CLEAN)` (or `HEGEL_C_FORK=clean`) re-executes the binary for each case so the same input sees the same memory — slower, but replays and shrinking become deterministic.

## Gotchas

1. **Name the test-case parameter `tc`** — the `HEGEL_DRAW_*` shorthands rely on it.
2. **`hegel_note` only appears with the minimal counterexample.** Don't use it for progress logging.
3. **`HEGEL_ASSERT` formats into a 512-byte buffer.** Longer messages are truncated; format yourself and call `hegel_fail` for more.
4. **Don't put drawn values in an origin.** `HEGEL_ASSERT` uses `file:line` as the bug's identity; if you call `hegel_fail_with_origin`, derive the origin from the location, never from a message containing drawn values — the shrinker would see every case as a new bug.
5. **Collections stay small** (about 5 elements whatever the max). Draw the length yourself when size matters.
6. **Repair-after-drawing costs shrink quality.** Draw within the right bounds instead of clamping or `%`-folding.
7. **No draws inside `HEGEL_ISOLATED`.** Generate first.
8. **`hegel_target` needs a budget**: the targeting phase starts late, so ask for ~1000 cases.
9. **A case may make about a million choices** before the engine stops it (overrun); generators that loop on draws should have bounds.

## Stateful Testing

For stateful subjects (maps, B-trees, allocators, freelists, caches, transactional stores), the highest-value test is a **model test**: run a sequence of operations against both the subject and a known-good reference, and check they agree after every step. hegel-c drives this with the engine's state machine — it picks the rules, applies up to ~50 per case, and shrinks the sequence by dropping rules and simplifying their operands:

```c
typedef struct { Stack *subject; int model[256]; int n; } State;

static void *setup(void)      { State *s = calloc(1, sizeof *s); s->subject = stack_new(); return s; }
static void teardown(void *p) { State *s = p; stack_free(s->subject); free(s); }

static bool can_push(const void *p) { return ((const State *)p)->n < 256; }
static bool can_pop (const void *p) { return ((const State *)p)->n > 0; }

static void rule_push(void *p, hegel_testcase *tc) {
    State *s = p;
    int x = HEGEL_DRAW_INT();
    stack_push(s->subject, x);
    s->model[s->n++] = x;
}

static void rule_pop(void *p, hegel_testcase *tc) {
    (void)tc;                                  /* no draws in this rule */
    State *s = p;
    int got = stack_pop(s->subject);
    int want = s->model[--s->n];
    HEGEL_ASSERT(got == want, "pop returned %d, expected %d", got, want);
}

static void inv_size(const void *p, hegel_testcase *tc) {
    (void)tc;
    const State *s = p;
    HEGEL_ASSERT(stack_size(s->subject) == s->n, "size %d, model %d",
                 stack_size(s->subject), s->n);
}

int main(void) {
    hegel_state_machine *sm = hegel_state_machine_new(setup, teardown);
    hegel_state_machine_add_rule_with_precondition(sm, "push", can_push, rule_push);
    hegel_state_machine_add_rule_with_precondition(sm, "pop",  can_pop,  rule_pop);
    hegel_state_machine_add_invariant(sm, "size", inv_size);
    hegel_state_machine_run_n(sm, 200);        /* fork per case; _run_nofork_n without */
    hegel_state_machine_free(sm);
    return 0;
}
```

- **Invariants run after every rule.**
- **Preconditions are pure observations** — no state changes, no draws. A rule whose precondition is false, or that calls `hegel_assume(tc, 0)`, is rejected: it doesn't count as a step.
- **Up to 64 rules and 64 invariants** per state machine.
- **Handles created by earlier rules:** a variable pool (`hegel_pool_new` / `_add` / `_draw(tc, pool, consume)` / `_restore` / `_free`) lets the engine pick — and shrink — which earlier handle a rule acts on. Pool ids are opaque `int64_t`s chosen by the engine: map them to your handles, don't index arrays with them.
