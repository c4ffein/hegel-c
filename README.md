# Hegel C binding

---

**WARNING: this lib is still in an exploratory phase, don't lose time with this unless I sent you this link**

---

A property-based testing library for C, built on top of [Hegel](https://hegel.dev).

**Property-based testing** (PBT) = fuzzer + reducer. You declare a property your code should hold; a generator fuzzes random inputs until one violates it, then a reducer shrinks that failing input down to a minimal counterexample.

**[Hegel](https://hegel.dev)** is a universal PBT protocol with per-language frontends, all backed by a shared Python [Hypothesis](https://hypothesis.works) core — so every language binding inherits Hypothesis' mature integrated shrinker and data generators. Background: [*Hypothesis, Antithesis, Synthesis*](https://antithesis.com/blog/2026/hegel/) on the Antithesis blog.

hegel-c is the C frontend: your tests include a header, link a `.a`, and get integrated shrinking + rich structured-data generation with no Python / Rust visible at the source level.

## Two API layers

**Primitive API** (`hegel_c.h`) — scalar draws, assertions, spans, test runners. For simple tests that hand-roll everything.

Small example — addition commutes:
```c
static void test_add_commutes (hegel_testcase *tc) {
  int a = HEGEL_DRAW_INT (-1000, 1000);
  int b = HEGEL_DRAW_INT (-1000, 1000);
  HEGEL_ASSERT (a + b == b + a, "a=%d b=%d", a, b);
}

int main (void) { hegel_run_test (test_add_commutes); return 0; }
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

- **Pure C public API, no C++.** A separate C++ Hegel binding is WIP by the Hegel team. This lib stays pure C at the surface.
- **Rust bridge under the hood for now.** We build on the [hegeltest](https://crates.io/crates/hegeltest) crate to inherit integrated shrinking and protocol tracking for free. A pure C build (reimplementing the Hegel wire protocol directly) is planned as a second option — see [`docs/design_rust_bridge.md`](docs/design_rust_bridge.md) for the why, the fork-per-case + parent-proxied IPC reasoning, and the `catch_unwind` orphan-leak fix rationale.
  - `rust-version/build.rs` also compiles the pure-C `hegel_gen.c` into the same `libhegel_c.a`.
- **A pure C version will be built.** This version will have no dependancy to the existing Rust lib.
  - This will only be done once there is a sufficient test suite to verify consistency between these 2 options.
  - The Rust bridge will be kept, and both implementations will still be tested one against another.
  - If the Hegel team ever releases a low-level C header for FFI bindings, we'll adapt to it, still providing a nice standard layer to make it as adapted to a C codebase as possible.

## Documentation

- **[docs/schema-api.md](docs/schema-api.md)** — schema system reference (constructors, macros, refcounting, draw/free)
- **[docs/patterns.md](docs/patterns.md)** — pattern catalog mapping C memory layouts to schema tests
- **[docs/shrinking.md](docs/shrinking.md)** — integrated shrinking explained, with a worked example finding a real bug in Scotch and shrinking to the theoretical minimum
- **[docs/testing.md](docs/testing.md)** — selftest suite three-layer pattern and test categories
- **[docs/mpi-testing.md](docs/mpi-testing.md)** — MPI_Comm_spawn integration guide
- **[docs/benchmarking.md](docs/benchmarking.md)** — fork vs nofork overhead, methodology and measured numbers
- **[docs/design_rust_bridge.md](docs/design_rust_bridge.md)** — design decisions for the current Rust-bridge build (FFI boundary, IPC protocol, orphan-leak fix)
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

MIT — see [LICENSE](LICENSE).
