# Hegel C binding

---

**WARNING: this lib is still in an exploratory phase, don't lose time with this unless I sent you this link**

---

A property-based testing library for C, built on top of [Hegel](https://hegel.dev).

**Property-based testing** (PBT) = fuzzer + reducer. You declare a property your code should hold; a generator fuzzes random inputs until one violates it, then a reducer shrinks that failing input down to a minimal counterexample.

**[Hegel](https://hegel.dev)** is a universal PBT engine with per-language frontends, all backed by one native engine (a Rust port of [Hypothesis](https://hypothesis.works)' conjecture engine, shipped as a C-ABI shared library) — so every language binding inherits Hypothesis' mature integrated shrinker and data generators. Background: [*Hypothesis, Antithesis, Synthesis*](https://antithesis.com/blog/2026/hegel/) on the Antithesis blog.

hegel-c is the C frontend: your tests include a header, link a `.a`, and get integrated shrinking + rich structured-data generation with no Python / Rust visible at the source level.

## The API

`hegel_c.h` — draws, assertions, spans, test runners, plus thin wrappers over the engine's own protocols: engine-sized collections (shrink by deleting elements), recursion, variable pools, and stateful state machines. Generators are plain C functions over these draws.

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

A generator for a structured value is a function that fills it, drawing each field in turn — a field that depends on another is just a variable, and spans tell the shrinker where an element starts and ends:
```c
static void gen_graph (hegel_testcase *tc, Graph *g) {
  g->nvert = hegel_draw_int (tc, 3, MAX_VERT);
  hegel_start_span (tc, HEGEL_SPAN_LIST);
  g->nedges = hegel_draw_int (tc, 0, MAX_EDGES);
  for (int i = 0; i < g->nedges; i++) {
    hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);
    g->edges[i].u = hegel_draw_int (tc, 0, g->nvert - 1);   /* bounded by nvert */
    g->edges[i].v = hegel_draw_int (tc, 0, g->nvert - 1);
    hegel_stop_span (tc, 0);
  }
  hegel_stop_span (tc, 0);
}
```

Until 2026-10 hegel-c also had a declarative schema layer (`hegel_gen.h`: describe a C struct, get generation and cleanup for free). It moved to [experiments/](experiments/README.md) with the measurements that retired it: for the harnesses this repository writes, plain generators were shorter and shrank as well.

## Design decisions

- **Pure C public API, no C++.** A separate C++ Hegel binding is provided by the Hegel team. This lib stays pure C at the surface.
- **Based on the official minimal hegel-c.** The main hegel now includes a low-level C header for FFI bindings, also called hegel-c. This repo is only a wrapper enabling more features.

## Documentation

- **[docs/shrinking.md](docs/shrinking.md)** — integrated shrinking explained, with a worked example finding a real bug in Scotch and shrinking to the theoretical minimum
- **[docs/testing.md](docs/testing.md)** — selftest suite three-layer pattern and test categories
- **[docs/mpi-testing.md](docs/mpi-testing.md)** — MPI_Comm_spawn integration guide
- **[docs/benchmarking.md](docs/benchmarking.md)** — fork vs nofork overhead, methodology and measured numbers
- **[docs/fuzzing-comparison.md](docs/fuzzing-comparison.md)** — property-based testing vs. AFL/libFuzzer: where each tool is strong, where each hits a wall, and how to choose
- **[CLAUDE.md](CLAUDE.md)** — project overview and code conventions
- **[TODO.md](TODO.md)** — deferred items
- **[experiments/](experiments/README.md)** — designs we built, measured and did not keep (the schema layer, view callbacks)

## Test suites

| Suite | Tests | Command |
|-------|-------|---------|
| selftest | 34 (19 PASS, 8 FAIL, 4 CRASH, 3 HEALTH) | `make selftest-test` |
| from-hegel-rust | 11 (5 PASS, 6 SHRINK) | `make from-hegel-rust-test` |
| MPI | 3 (1 mpiexec, 2 spawn) | `make mpi-test` |
| Scotch IRL | 3 (1 sequential, 1 reducer demo, 1 PT-Scotch MPI) + a 29-test property suite | `make scotch-test` |

MPI tests use `MPI_Comm_spawn` — no `mpiexec` required for spawn tests. See [docs/mpi-testing.md](docs/mpi-testing.md).

The Scotch IRL suite is the real-world proof. [`tests/irl/scotch/props/`](tests/irl/scotch/props/) holds one property per Scotch API area over shared graph generators; [`tests/irl/scotch/test_graph_order_shrink.c`](tests/irl/scotch/test_graph_order_shrink.c) rediscovers a real bug in `SCOTCH_graphOrder` from random graphs and shrinks to the 3-vertex minimum. See [`docs/shrinking.md`](docs/shrinking.md) for the walkthrough.

## License

hegel-c itself — the library (`hegel_c.h`, `core/`), its tests,
experiments, tools and docs — is MIT, see [LICENSE](LICENSE). Nothing under any
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
