# Hegel C binding

Currently a mostly vibe-coded WiP duct-taped C library — your tests include a .h, link a .a, never see Rust.
- The current implementation is Rust because we rely on the main lib that talks to the Hegel server (which is a Rust/Python service).
- The alternative would be reimplementing the Hegel wire protocol in pure C.
- This will only be done once there is a sufficient test suite to verify consistency between these 2 options.
- While the Rust bridge was the fast path to get everything working, it will be kept - we want both:
  - a future pure C implem connecting to the worker through the socket
  - a version as close to the the official Rust implementation as possible
    - if the Hegel team ever release a low-level C header for FFI bindings, we'll adapt to it, still providing a nice standard layer to make it as adapted to a C codebase as possible

## Test suites

| Suite | Tests | Command |
|-------|-------|---------|
| selftest | 20 (13 PASS, 4 FAIL, 3 CRASH) | `make selftest-test` |
| from-hegel-rust | 19 binaries covering 26 Rust tests (13 PASS, 6 SHRINK) | `make from-hegel-rust-test` |
| MPI | 3 (1 mpiexec, 2 spawn) | `make mpi-test` |
| Scotch IRL | 2 (1 sequential, 1 PT-Scotch MPI) | `make scotch-test` |

MPI tests use `MPI_Comm_spawn` — no `mpiexec` required for spawn tests. See [docs/mpi-testing.md](docs/mpi-testing.md).

## Done
- [x] `hegel_run_test_result()` / `_n()` — return 0/1 instead of `exit(1)`, enables multi-test binaries
- [x] Test suite API (`hegel_suite_new/add/run/free`) — run multiple tests sharing one server
- [x] `hegel_note(tc, msg)` — debug output on final replay only
- [x] Fix from-hegel-rust test mismatches — `find_any` edge cases, shrink boundary 101
- [x] Port 26 hegel-rust tests — integers (i8–u64), floats, lists, combinators, compose, sampled_from, shrink quality
- [x] Grammar-based strategy fuzzer — `test_strategy_gen` + `test_strategy_shrink` (shrinks to `/vert>0?b:b;`)
- [x] CI integration — `.github/workflows/ci.yml` (selftest, from-hegel-rust, Scotch)
- [x] PT-Scotch MPI tests — fork mode + `MPI_Comm_spawn` + `MPI_Intercomm_merge`, no mpiexec. [Full guide](docs/mpi-testing.md).
- [x] Scotch IRL tests — sequential `SCOTCH_graphPart` + distributed `SCOTCH_dgraphPart`
- [x] Selftest three-layer pattern rewrite (all 20 tests follow it)
- [x] "Draw N:" trace — not a bug, only on final replay (`is_last_run`)

## TODO
- [ ] Port more hegel-rust tests — see `tests/from-hegel-rust/manifest.md`. Remaining need features (exclude_min, NaN/inf) or are Rust-specific.
- [ ] `hegel_target(tc, value, label)` — property-directed testing. Not in hegeltest 0.1.18 or 0.4.3, blocked upstream.
- [ ] Pool hegel server across test binaries — hegeltest pools within a process, but separate binaries each pay ~1s. Suite API partially addresses this.
- [ ] Parallel test execution
- [ ] Verify Hegel's database replay of failing cases across runs with fork mode
- [ ] More Scotch IRL tests — strategy string fuzzing against real parser, graph ordering, mesh partitioning
- [ ] More IRL targets beyond Scotch in `tests/irl/`
- [ ] Real C implementation (pure C wire protocol, no Rust bridge)
  - [ ] Compare with Rust bridge using PBT

## Design decisions
- **Pure C, no C++.** A separate C++ Hegel binding is WIP by the Hegel team. This lib stays pure C.
- **`graph_gen.h` / `scotch_helpers.h` stay in the Scotch test harness**, not in hegel-c. They're Scotch-specific. But they contain patterns (CSR builders, strategy generators) worth generalizing later.

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

Four categories:
- **PASS tests**: function is correct, hegel should find no bug, exit 0
- **FAIL tests**: function has a known bug, hegel should find it, exit non-zero
- **CRASH tests**: function segfaults/aborts on specific inputs, fork isolation should catch it, exit non-zero
- **CRASH+FAIL tests**: function either crash or fail depending on the inputs, the reducer should still work

## Benchmarking

### Fork vs nofork (this project)

```
make bench
```

Runs each of `test_graph_part` (simple) and `test_stress_10k` (heavy) in
both fork and nofork modes, 5 times each. Typical results:

| Test           | Fork    | Nofork  | Overhead |
|----------------|---------|---------|----------|
| graph_part     | ~6.7s   | ~5.6s   | ~18%     |
| stress_10k     | ~2.6s   | ~2.5s   | ~2%      |

The overhead is dominated by Hegel's server startup (~1s), not fork itself.
For tests where the actual work is non-trivial, fork overhead is negligible.

### How to add your own bench tests

Add the test name to `BENCH_TESTS` in the Makefile:

```makefile
BENCH_TESTS = test_graph_part test_stress_10k test_your_new_test
```

### Benchmarking against other codebases

To benchmark Hegel's C binding on a different C library:

1. **Write a test** that follows the pattern in `test_graph_part.c`:
   - `#include "hegel_c.h"`
   - A test function `void myTest(hegel_testcase *tc)` that uses `hegel_draw_*` and `HEGEL_ASSERT`
   - `main()` calls `hegel_run_test(myTest)`

2. **Link against** `-lhegel_c` (from `hegel/rust-version/target/release/`) plus your library

3. **Run both modes:**
   ```bash
   # Fork mode (default)
   gcc -O2 -I/path/to/hegel -o test_fork my_test.c -L/path/to/hegel_c -lhegel_c -lpthread -lm -ldl
   time ./test_fork

   # Nofork mode
   gcc -O2 -DHEGEL_BENCH_NOFORK -I/path/to/hegel -o test_nofork my_test.c -L/path/to/hegel_c -lhegel_c -lpthread -lm -ldl
   time ./test_nofork
   ```

4. **What to compare:**
   - Per-run wall time (includes Hegel server startup, ~1s fixed cost)
   - For apples-to-apples, use `hegel_run_test_n` with large N (e.g., 1000) so
     the server startup is amortized and you're measuring per-case fork overhead
   - The fork overhead per case is ~50-100µs on Linux (COW page table copy).
     If your test case does >1ms of work, fork is free.
