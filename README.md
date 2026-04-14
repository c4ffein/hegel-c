# Hegel C binding

---

**WARNING: this lib is still in an exploratory phase, don't lose time with this unless I sent you this link**

---

A property-based testing library for C, built on top of [Hegel](https://github.com/hegeldev/hegel-cpre) — your tests include a header, link a `.a`, and get integrated shrinking + rich structured-data generation with no Python / Rust visible at the source level.

## Two API layers

**Primitive API** (`hegel_c.h`) — scalar draws, assertions, spans, test runners. For simple tests that hand-roll everything.

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
- **[docs/mpi-testing.md](docs/mpi-testing.md)** — MPI_Comm_spawn integration guide
- **[docs/benchmarking.md](docs/benchmarking.md)** — fork vs nofork overhead, methodology and measured numbers
- **[docs/design_rust_bridge.md](docs/design_rust_bridge.md)** — design decisions for the current Rust-bridge build (FFI boundary, IPC protocol, orphan-leak fix)
- **[CLAUDE.md](CLAUDE.md)** — project overview and code conventions
- **[TODO.md](TODO.md)** — deferred items

## Test suites

| Suite | Tests | Command |
|-------|-------|---------|
| selftest | 44 (28 PASS, 9 FAIL, 3 CRASH, 4 HEALTH) | `make selftest-test` |
| from-hegel-rust | 19 binaries covering 26 Rust tests (13 PASS, 6 SHRINK) | `make from-hegel-rust-test` |
| MPI | 3 (1 mpiexec, 2 spawn) | `make mpi-test` |
| Scotch IRL | 4 (2 sequential, 1 reducer demo, 1 PT-Scotch MPI) | `make scotch-test` |

MPI tests use `MPI_Comm_spawn` — no `mpiexec` required for spawn tests. See [docs/mpi-testing.md](docs/mpi-testing.md).

The selftest suite doubles as example code — 11 of the 44 tests are focused schema-pattern demonstrations (`test_gen_schema_*.c`). See [docs/patterns.md](docs/patterns.md) for the index.

The Scotch IRL suite is the real-world proof. [`tests/irl/scotch/test_graph_part_schema.c`](tests/irl/scotch/test_graph_part_schema.c) shows the schema API generating graphs for `SCOTCH_graphPart`; [`tests/irl/scotch/test_graph_order_shrink.c`](tests/irl/scotch/test_graph_order_shrink.c) rediscovers a real bug in `SCOTCH_graphOrder` from a random schema and shrinks to a 3-vertex minimum. See [`docs/shrinking.md`](docs/shrinking.md) for the walkthrough.

## TODO
- [ ] Port more hegel-rust tests — see `tests/from-hegel-rust/manifest.md`. Remaining need features (exclude_min, NaN/inf) or are Rust-specific.
- [ ] `hegel_target(tc, value, label)` — property-directed testing. Not in hegeltest 0.1.18 or 0.4.3, blocked upstream.
- [ ] Pool hegel server across test binaries — hegeltest pools within a process, but separate binaries each pay ~1s. Suite API partially addresses this.
- [ ] Parallel test execution
- [ ] Verify Hegel's database replay of failing cases across runs with fork mode
- [ ] More Scotch IRL tests — strategy string fuzzing against real parser, mesh partitioning
- [ ] More IRL targets beyond Scotch in `tests/irl/`
- [ ] Real C implementation (pure C wire protocol, no Rust bridge)
  - [ ] Compare with Rust bridge using PBT
- [ ] Clean up `graph_gen.h` / `scotch_helpers.h` — currently in the Scotch test harness, contain general patterns (CSR builders, strategy generators) worth extracting or generalizing. See `TODO.md`.

## Selftest pattern

The selftest suite (`tests/selftest/`) tests hegel-c itself. Each test has **three layers**:

1. **A C function under test** — a standalone function with a *known* bug, crash, or edge case on specific inputs. This is the "code someone wrote." It exists independently of hegel.
2. **A hegel test** — a property test that exercises that function using `hegel_draw_*` and `HEGEL_ASSERT`. Hegel should find the bug and shrink to a minimal counterexample.
3. **The outer runner** — the Makefile target that runs the binary and checks the *exit code* (and optionally stderr). This is the real test: it verifies that hegel-c did its job.

Example structure:
```c
/* Layer 1: function under test — has a bug when x overflows */
int square(int x) {
    return (int)((unsigned)x * (unsigned)x);  /* wraps */
}

/* Layer 2: hegel test that exercises it */
void testSquare(hegel_testcase *tc) {
    int x = hegel_draw_int(tc, 40000, 100000);
    int result = square(x);
    HEGEL_ASSERT(result >= 0, "square(%d) = %d", x, result);
}

/* main just hands it to hegel */
int main() { hegel_run_test(testSquare); return 0; }
```

Layer 3 (the Makefile) knows this test should exit non-zero — hegel should catch the overflow.

Five categories:
- **PASS tests**: function is correct, hegel should find no bug, exit 0
- **FAIL tests**: function has a known bug, hegel should find it, exit non-zero
- **CRASH tests**: function segfaults/aborts on specific inputs, fork isolation should catch it, exit non-zero
- **CRASH+FAIL tests**: function either crash or fail depending on the inputs, the reducer should still work
- **HEALTH tests**: schema is deliberately pathological (e.g. `HEGEL_FILTER_INT` with a ~99.9999% rejection rate, or `HEGEL_ARRAY_INLINE` with a minimum size that overflows the byte budget). The Hypothesis-side health checks must fire and reach the C user as a non-zero exit plus a recognizable `"Health check failure"` message in stderr. Covers both `filter_too_much` and `large_base_example`, single + suite variants.

## Benchmarking

Run `make bench-bench` for fork-vs-nofork numbers on the author's machine.
See [`docs/benchmarking.md`](docs/benchmarking.md) for the methodology,
current measurements, and instructions for benchmarking your own property.
