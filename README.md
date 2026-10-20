# Hegel C binding

---

**WARNING: this lib is still in an exploratory phase, don't lose time with this unless I sent you this link**

---

A property-based testing library for C, built on top of [Hegel](https://hegel.dev).

**Property-based testing** (PBT) = fuzzer + reducer. You declare a property your code should hold; a generator fuzzes random inputs until one violates it, then a reducer shrinks that failing input down to a minimal counterexample.

**[Hegel](https://hegel.dev)** is a universal PBT engine with per-language frontends, all backed by one native engine (a Rust port of [Hypothesis](https://hypothesis.works)' conjecture engine, shipped as a C-ABI shared library) — so every language binding inherits Hypothesis' mature integrated shrinker and data generators. Background: [*Hypothesis, Antithesis, Synthesis*](https://antithesis.com/blog/2026/hegel/) on the Antithesis blog.

hegel-c is the C frontend: your tests include a header, link a `.a`, and get integrated shrinking + rich structured-data generation with no Python / Rust visible at the source level.

## Two API layers

**Primitive API** (`hegel_c.h`) — scalar draws, assertions, spans, test runners, plus thin wrappers over the engine's own protocols: engine-sized collections (shrink by deleting elements), recursion, variable pools, and stateful state machines. For simple tests that hand-roll everything.

Small example — addition commutes:
```c
static void test_add_commutes (hegel_testcase *tc) {
  int a = HEGEL_DRAW_INT (-1000, 1000);
  int b = HEGEL_DRAW_INT (-1000, 1000);
  HEGEL_ASSERT (a + b == b + a, "a=%d b=%d", a, b);
}

int main (void) { hegel_run_test (test_add_commutes); return 0; }
```

Crash isolation where it matters — generate in-process, isolate only the code under test (a crash in the block is a shrinkable failure naming the block):
```c
static void test_build (hegel_testcase *tc) {
  Graph *g = gen_graph (tc);        /* draws: plain calls into the engine */
  HEGEL_ISOLATED (tc) {             /* forked; sees g copy-on-write */
    HEGEL_ASSERT (graph_build (g) == 0, "build failed");
  }
  graph_free (g);
}

int main (void) { hegel_run_test_nofork (test_build); return 0; }
```

**Schema API** (`hegel_gen.h`) — describe your C struct declaratively and get generation, allocation, span-based structural shrinking, and cleanup for free. Typed in terms of `hegel_schema_t` (a zero-cost newtype wrapper). See [docs/schema-api.md](docs/schema-api.md) for the reference and [docs/patterns.md](docs/patterns.md) for a catalog mapping common C memory layouts to the test files that demonstrate each one.

Small example:
```c
typedef struct Tree {
  int val;
  char *label;
  struct Tree *left, *right;
} Tree;

static hegel_schema_t tree_schema;

static void test_roundtrip (hegel_testcase *tc) {
  Tree *t;
  hegel_shape *sh = hegel_schema_draw (tc, tree_schema, (void **) &t);
  HEGEL_ASSERT (tree_to_json (t) && json_to_tree (tree_to_json (t)), "...");
  hegel_shape_free (sh);
}

int main (void) {
  tree_schema = HEGEL_STRUCT (Tree,
      HEGEL_INT      (-1000, 1000),
      HEGEL_OPTIONAL (hegel_schema_text (0, 8)),
      HEGEL_SELF     (),
      HEGEL_SELF     ());
  hegel_run_test (test_roundtrip);
  hegel_schema_free (tree_schema);
  return 0;
}
```

## Design decisions

- **Pure C public API, no C++.** A separate C++ Hegel binding is provided by the Hegel team. This lib stays pure C at the surface.
- **Based on the official minimal hegel-c.** The main hegel now includes a low-level C header for FFI bindings, also called hegel-c. This repo is only a wrapper enabling more features.

## Documentation

- **[docs/schema-api.md](docs/schema-api.md)** — schema system reference (constructors, macros, refcounting, draw/free)
- **[docs/patterns.md](docs/patterns.md)** — pattern catalog mapping C memory layouts to schema tests
- **[docs/shrinking.md](docs/shrinking.md)** — integrated shrinking explained, with a worked example finding a real bug in Scotch and shrinking to the theoretical minimum
- **[docs/testing.md](docs/testing.md)** — selftest suite three-layer pattern and test categories
- **[docs/mpi-testing.md](docs/mpi-testing.md)** — MPI_Comm_spawn integration guide
- **[docs/benchmarking.md](docs/benchmarking.md)** — fork vs nofork overhead, methodology and measured numbers
- **[docs/fuzzing-comparison.md](docs/fuzzing-comparison.md)** — property-based testing vs. AFL/libFuzzer: where each tool is strong, where each hits a wall, and how to choose
- **[CLAUDE.md](CLAUDE.md)** — project overview and code conventions
- **[TODO.md](TODO.md)** — deferred items

## Test suites

| Suite | Tests | Command |
|-------|-------|---------|
| selftest | 38 (26 PASS, 5 FAIL, 3 CRASH, 4 HEALTH) | `make selftest-test` |
| from-hegel-rust | 19 binaries covering 26 Rust tests (13 PASS, 6 SHRINK) | `make from-hegel-rust-test` |
| MPI | 3 (1 mpiexec, 2 spawn) | `make mpi-test` |
| Scotch IRL | 4 (2 sequential, 1 reducer demo, 1 PT-Scotch MPI) | `make scotch-test` |

MPI tests use `MPI_Comm_spawn` — no `mpiexec` required for spawn tests. See [docs/mpi-testing.md](docs/mpi-testing.md).

The selftest suite doubles as example code — 11 of the 38 tests are focused schema-pattern demonstrations (`test_schema_*.c`). See [docs/patterns.md](docs/patterns.md) for the index.

The Scotch IRL suite is the real-world proof. [`tests/irl/scotch/test_graph_part_schema.c`](tests/irl/scotch/test_graph_part_schema.c) shows the schema API generating graphs for `SCOTCH_graphPart`; [`tests/irl/scotch/test_graph_order_shrink.c`](tests/irl/scotch/test_graph_order_shrink.c) rediscovers a real bug in `SCOTCH_graphOrder` from a random schema and shrinks to a 3-vertex minimum. See [`docs/shrinking.md`](docs/shrinking.md) for the walkthrough.

## License

hegel-c itself — the library (`hegel_c.h`, `hegel_gen.*`, `core/`), its
tests, tools and docs — is MIT, see [LICENSE](LICENSE). Nothing under any
other license is compiled into `libhegel_c.a`.

The bug-hunting harnesses under `tests/irl/` don't vendor their targets: each
one names an upstream commit (a *pin*), and the Linux / Scotch sources are
fetched and copied in at build time, never committed. The only third-party
text in the repo is the fix patches under `tests/irl/*/pins/` — their diff
context is upstream source, so they keep the upstream license (GPL-2.0-only
for Linux, CeCILL-C for Scotch). Test binaries built from fetched sources are
covered by those sources' licenses. Licensing is tracked per file following
[REUSE](https://reuse.software): license texts are in [LICENSES/](LICENSES/),
files that can't carry a header are covered by [REUSE.toml](REUSE.toml), and
`make check-reuse` lints it all.
