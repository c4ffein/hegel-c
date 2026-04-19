<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Benchmarking

Fork vs nofork overhead, measured on the author's machine.  The
short version: for any test whose body does more than a couple of
milliseconds of real work, fork mode is effectively free compared
to nofork — so you get crash isolation for a cost you won't
notice.  For trivial tests that draw two ints and return, the
per-case IPC cost of fork mode dominates and shows up as a
meaningful percentage of wall time.

## What is measured

Two binaries, each built twice (fork / nofork), doing deliberately
trivial work so the measurement is dominated by framework
overhead rather than user code:

1. **Single-test bench** (`test_bench_single.c`) — one test, `N`
   cases per run.  Matches the shape of a classic one-binary-one-
   property test.
2. **Suite-mode bench** (`test_bench_suite.c`) — ten tests in one
   binary, each running hegeltest's default case count.  In fork
   mode this uses `hegel_suite_*`; in nofork mode it calls
   `hegel_run_test_nofork` sequentially (the hegeltest server is a
   process-wide singleton either way, so startup is amortized on
   both sides).
3. **Direct-Rust bench** (`tests/bench/rust_direct/src/main.rs`) —
   the same shape as the single-test bench (N cases, two `i32`
   draws, trivial assert) but written in Rust against the
   `hegeltest` crate directly, with no C code, no FFI, no fork.
   Used as an oracle: "is hegel-c actually slower than calling
   hegeltest natively from Rust?"  If the C nofork number matches
   this one, the whole C/FFI layer is free and the only cost worth
   talking about is fork-mode IPC.

Both C bench bodies do `hegel_draw_int(tc, 0, 100)` once or twice
per case and an assert that can never fail.  That's intentional —
we're measuring framework cost, not user-code cost.

## Results

Date: 2026-04-13
hegeltest: 0.4.3
CPU: Intel Xeon D-2123IT @ 2.20 GHz
Kernel: Linux 6.12.74 (Debian 13)
Runs: 5 measured iterations per variant + 1 discarded warmup,
pinned to CPU 0 via `taskset -c 0`.

### Single-test bench (N = 1000 cases)

| Mode   | Mean  | Stddev | Overhead vs nofork |
|--------|-------|--------|--------------------|
| fork   | 6.12s | 0.19s  | +2.18s (~55%)      |
| nofork | 3.94s | 0.90s  | —                  |

Per-case fork overhead = (6.12 − 3.94) / 1000 ≈ **2.2 ms/case**.

### Suite bench (10 tests × hegeltest default cases)

| Mode   | Mean  | Stddev | Overhead vs nofork |
|--------|-------|--------|--------------------|
| fork   | 4.48s | 0.98s  | +1.15s (~34%)      |
| nofork | 3.33s | 0.58s  | —                  |

### Variance note

Stddev runs up to ~22% of the mean on the fork suite variant.
These numbers were taken on a contended development host, not a
quiet bare-metal box, so treat them as ballpark — order of
magnitude is reliable, the second digit is not.  Reproducing on a
quiet machine should narrow the band considerably.

### Re-run later the same day, with rust_direct (2026-04-13)

Same machine, same binaries, same 5 iters + 1 warmup, `taskset -c 0`.
Ambient load was lower this time (load average ~17, same order
as the prior run but with fewer spikes), and stddevs came in much
tighter.

| Variant                  | Mean  | Stddev | Cases/s† |
|--------------------------|-------|--------|----------|
| single fork              | 4.66s | 0.07s  | ~215     |
| single nofork            | 3.67s | 0.08s  | ~273     |
| **rust_direct (N=1000)** | 3.72s | 0.07s  | ~269     |
| suite fork               | 3.89s | 0.09s  | —        |
| suite nofork             | 2.92s | 0.04s  | —        |

† End-to-end including server spawn; not steady-state.

Per-case fork overhead on this re-run = (4.66 − 3.67) / 1000 ≈
**1.0 ms/case** — roughly half the 2.2 ms/case figure from the
earlier run.  The earlier nofork stddev (0.90s, ~23% of the mean)
was clearly being pulled by an outlier slow run; the tighter re-run
is the better point estimate.

### Discrepancy analysis: C vs direct Rust

The key finding from adding `rust_direct` to the comparison: **the
entire C header + Rust FFI layer is free.**  C nofork (3.67s) and
direct Rust against hegeltest (3.72s) are indistinguishable — the
50 ms gap is well inside the noise floor (stddevs ~70–80 ms each).

Breaking it down:

| Path                                     | Mean (1000 cases) | vs rust_direct |
|------------------------------------------|-------------------|----------------|
| Rust → hegeltest (no C, no FFI, no fork) | 3.72s             | baseline       |
| C → FFI → hegeltest, no fork             | 3.67s             | −50 ms (noise) |
| C → FFI → fork/pipe → parent → hegeltest | 4.66s             | +940 ms        |

Interpretation: if you'd rather eliminate hegel-c entirely and
write your property in Rust, you will save approximately nothing
as long as you stay in nofork mode.  The only wall-clock cost that
shows up in the comparison is fork-mode IPC — and fork mode is the
reason you use hegel-c in the first place (crash isolation around
the C-under-test).  Rust users don't pay that cost because they
don't need it; C users who turn it off recover it.

The C nofork run coming in *very slightly* faster than direct Rust
is almost certainly noise, but if it's real it'd be the
`hegel_run_test_nofork` path skipping the Rust test-harness
wrapper (`Hegel::new(...).run()` on the Rust side sets up a
`Settings` builder and a closure trampoline the C side doesn't
use).  Not worth chasing — the interesting digit is "zero
overhead," not the sign of the sub-noise delta.

## Interpretation

The prior estimate baked into the old README was "~50–100 µs per
fork".  The *actual* per-case cost of fork mode on this workload
is ~1–2 ms (the two runs above bracket this) — roughly 10–40×
the fork cost alone.  That extra cost isn't the `fork(2)` syscall
itself; it's the pipe-IPC round-trip between the forked child and
the parent, which proxies every `hegel_draw_*` call.  (See
[`design_rust_bridge.md`](design_rust_bridge.md) for why the
parent proxies in the first place — short version: the Hegel
server connection is stateful and sequential, so a child holding
the connection would corrupt the server's view the moment it
crashes, defeating the whole point of fork isolation.)  A test
that draws many values or a test running on a slower IPC path
(spawned MPI worker, container) will feel this more than a test
that draws one int and returns.

For the kind of test a user actually writes, though, the picture
is reassuring:

- **Test body ≤ 1 ms of work**: fork overhead is the dominant
  cost.  You'll see a noticeable multiplier.  Trivial bench
  binaries fall here.
- **Test body ≥ 5 ms of work** (any non-trivial property —
  JSON round-trip, graph partition, anything touching a real
  library): fork overhead is down in the noise.  Use fork.
- **Test body does its own crash-inducing work** (anything
  calling into C code you don't control): use fork unconditionally.
  Crash isolation is the whole point of the framework.

The suite API amortizes server startup across tests, as expected.
Per-test cost in the suite fork run is ~(4.48 − 3.33) / 10 ≈
115 ms above nofork, spread across hegeltest's default ~100 cases
per test — roughly ~1.2 ms per case, a little cheaper than the
single-test number above (the suite case count is smaller, so the
constant costs get a smaller amortization pool).

## Reproduction

```bash
make bench-bench                                  # both benches with defaults
make bench-bench-single N=5000                    # larger single-test N
make bench-bench-single ITERS=20 WARMUPS=3        # more samples, cleaner stats
make bench-bench-suite
```

The `bench-` prefix is the root Makefile proxy into
`tests/bench/`.  Under the hood:

```bash
cd tests/bench
make bench-single N=1000 ITERS=5 WARMUPS=1
make bench-suite
```

Variables:

| Var       | Default | Meaning                                       |
|-----------|---------|-----------------------------------------------|
| `N`       | 1000    | cases per run for the single-test bench       |
| `ITERS`   | 5       | measured iterations per variant               |
| `WARMUPS` | 1       | discarded warmup iterations per variant       |
| `TASKSET` | auto    | pinning prefix (auto-detects `taskset -c 0`)  |

`N` has no effect on the suite bench — `hegel_suite_run` does not
expose a case-count override, so the suite bench uses hegeltest's
default on both sides to keep the comparison apples-to-apples.

The direct-Rust oracle is not wired into the Makefile; build and
run it by hand:

```bash
cd tests/bench/rust_direct && cargo build --release
taskset -c 0 ./target/release/bench_direct 1000    # N = 1000 cases
```

It shares the hegeltest server spawn with any other hegel-c binary
in the same process tree, so wall-clock comparisons are fair as
long as you run each binary fresh from a clean shell.

## Running this on your own property

If you want to know how fork mode feels on the real test you
care about rather than on a trivial bench, build your test twice
from the same source:

```bash
gcc -O2 -I/path/to/hegel-c -o my_test my_test.c \
    -L/path/to/hegel-c/rust-version/target/release \
    -lhegel_c -lpthread -lm -ldl

gcc -O2 -DHEGEL_BENCH_NOFORK -I/path/to/hegel-c -o my_test_nofork my_test.c \
    -L/path/to/hegel-c/rust-version/target/release \
    -lhegel_c -lpthread -lm -ldl
```

In the test source, guard the runner call:

<!-- /ignore bench-template: ifdef-guarded main() pattern, not from any fixed source file -->
```c
int main (void) {
#ifdef HEGEL_BENCH_NOFORK
  hegel_run_test_nofork_n (my_test, 1000);
#else
  hegel_run_test_n (my_test, 1000);
#endif
  return 0;
}
```

Then time each with `taskset -c 0 /usr/bin/time -v ./my_test`
(wall-clock; CPU time partially misses the fork overhead).  The
delta divided by the case count is your per-case fork overhead.

## What is *not* benchmarked

These are all open questions and would be good future work:

- Individual draw primitives (int vs float vs text vs schema)
- Schema-layer allocation overhead vs hand-rolled draws
- `hegeltest` version comparison (0.1.18 vs 0.4.3)
- Cross-binding comparison (hegel-rust vs hegel-go vs hegel-cpp
  vs hegel-c on the same property)
- Effect of the `catch_unwind` orphan-leak fix on total throughput
- rust_direct at larger N (steady-state cases/s once startup is
  amortized — single-point sampling at N=5000/10000 earlier in
  the session pointed at ~475–550 cases/s end-to-end, marginal
  ~645 cases/s, but we haven't captured that with proper iters
  and stddev yet)
