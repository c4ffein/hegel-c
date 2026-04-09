# CLAUDE.md

## Project overview

hegel-c is a C binding for Hegel, a property-based testing framework. It provides a pure C API (`hegel_c.h`) backed by a Rust FFI bridge (`rust-version/src/lib.rs`) that connects to the Hegel server (a Rust/Python service). C tests include the header, link against a static `.a`, and never see Rust.

**Status**: Work in progress. Author: c4ffein. License: MIT.

## Architecture

- `hegel_c.h` — Public C API (opaque types, draw functions, generators, assertions, suite API)
- `rust-version/src/lib.rs` — Rust FFI implementation, compiled to `libhegel_c.a`
- `tests/selftest/` — 20 self-tests (PASS/FAIL/CRASH)
- `tests/from-hegel-rust/` — 26 tests ported from hegel-rust
- `tests/mpi/` — 3 MPI tests (mpiexec + MPI_Comm_spawn patterns)
- `tests/irl/scotch/` — 2 Scotch integration tests (sequential + PT-Scotch MPI)
- `docs/mpi-testing.md` — MPI integration guide

Two execution modes:
- **Fork mode** (default): each test case runs in a forked child; parent serves draw requests via pipe IPC. Crash-safe.
- **Nofork mode**: single process, no crash isolation. For benchmarking or MPI_Comm_spawn.

## Build and test

Prerequisites: GCC, Rust toolchain (cargo), `uv` (Python package manager — hegeltest auto-installs the Hegel server on first run).

Optional: `mpicc`/OpenMPI for MPI tests, Scotch/PT-Scotch for IRL tests.

```bash
make help                       # all targets and proxy commands

make selftest-test              # 20 tests (13 PASS, 4 FAIL, 3 CRASH)
make from-hegel-rust-test       # 19 binaries covering 26 Rust tests (13 PASS, 6 SHRINK)
make mpi-test                   # 3 tests (needs mpicc)
make scotch-test                # 2 tests (needs Scotch — clone via make inspiration)

make inspiration                # clone hegel-rust, hegel-go, scotch into inspiration/
```

All test Makefiles use `REPO_ROOT = $(abspath ../..)` (or `../../..` for irl/scotch). Tests `cd` to `REPO_ROOT` before executing so the Hegel server path resolves.

For standalone compilation: `gcc -O2 -I. -funwind-tables -fexceptions -o test test.c -Lrust-version/target/release -lhegel_c -lpthread -lm -ldl`

## C API summary

**Runners:**
- `hegel_run_test(fn)` / `_n(fn, n)` — fork mode, `exit(1)` on failure
- `hegel_run_test_result(fn)` / `_n(fn, n)` — fork mode, returns 0/1 (no exit)
- `hegel_run_test_nofork(fn)` / `_n(fn, n)` — no fork, no crash isolation
- `hegel_suite_new/add/run/free` — multi-test runner sharing one server

**Draws:** `hegel_draw_int`, `_i64`, `_u64`, `_usize`, `_float`, `_double`, `_text`, `_regex`

**Generators:** `hegel_gen_int`, `_i64`, `_u64`, `_float`, `_double`, `_bool`, `_text`, `_regex`, `_one_of`, `_sampled_from`, `_optional`, `_map_*`, `_filter_*`, `_flat_map_*`

**Other:** `hegel_note(tc, msg)` (debug on final replay), `hegel_assume(tc, cond)`, `hegel_fail(msg)`, `HEGEL_ASSERT(cond, fmt, ...)`

## Selftest three-layer pattern

Each test has three layers:

1. **Function under test** — standalone C function with a known bug/edge case
2. **Hegel test** — property test using `hegel_draw_*` and `HEGEL_ASSERT` to exercise that function
3. **Makefile runner** — runs the binary and checks exit code

Do NOT inline the "function under test" into the assertion — layer 1 must be a separate, independently meaningful function.

## Code style

- **C**: Pure C (no C++), K&R brace style, 2-space indent, ~100 col lines, `snake_case` functions, `UPPER_CASE` macros
- **Rust**: Standard Rust conventions, `#[unsafe(no_mangle)]` + `extern "C-unwind"` for FFI exports
- **Naming**: All public symbols prefixed `hegel_` / `HEGEL_`

## License headers

All source files (.c, .h, .rs, .md, Makefile) must start with an SPDX header:
```
/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
```
Markdown files use `<!-- SPDX-License-Identifier: MIT ... -->`. Enforced by `check-license` target in each Makefile.

## Key dependencies

- **Rust**: `hegeltest = "0.4"` (resolves to 0.4.3), `libc = "0.2"`
- **C linking**: `-lhegel_c -lpthread -lm -ldl` (add `-lscotch -lscotcherr -lz -lrt` for Scotch tests)
- Rust lib compiled as `staticlib` with `panic = "unwind"`

## hegeltest version and API gaps

Using hegeltest 0.4.3 (upgraded from 0.1.18 — zero code changes needed, API is compatible).

- `TestCase::note()` — available, exposed as `hegel_note()`
- `TestCase::target()` — **NOT available** in 0.4.3. The underlying Hypothesis Python library has `target()` for property-directed testing, but hegeltest hasn't exposed it. Blocked upstream.
- `TestCase::assume()` — available, exposed as `hegel_assume()`
- "Draw N:" trace output — only printed when `is_last_run` is true (final replay), controlled by `on_draw` callback in `test_case.rs`. This is intentional, not noise.

## Server lifecycle

The `hegeltest` crate uses a process-wide singleton (`OnceLock<HegelSession>`) to manage the Hegel server. Server subprocess is spawned once on first `Hegel::new(...).run()` call and reused for all subsequent calls in the same process. Communication is via stdin/stdout pipes (`--stdio` mode) with a multiplexed CBOR protocol.

The server is auto-installed via `uv` into `.hegel/venv/` — `hegel-core` Python package.

## Fork mode architecture

In fork mode, the **parent** process owns the Hegel server connection. For each test case:
1. Parent creates request/response pipes, then `fork()`s
2. Child runs the C test function; `hegel_draw_*()` calls write requests to the pipe
3. Parent reads requests, forwards them to the Hegel server, writes responses back
4. If child crashes (SIGSEGV, SIGABRT), parent catches it via `waitpid()` and reports failure to Hegel for shrinking

The child never talks to the Hegel server directly — the parent proxies all communication.

## Shrinking

Hegel uses **integrated shrinking** (from Hypothesis), not type-based shrinking (QuickCheck). All generation consumes from a shared byte stream. Shrinking operates on that byte stream — making it shorter or lexicographically smaller — then replays generation.

- `map()`, `filter()`, `flat_map()` do NOT degrade shrinking quality
- Simplicity ordering: `false` < `true`, `0` < `1` < `-1` < `2` < `-2`...
- All shrinking logic lives in the Hegel server (Python/Hypothesis)

## MPI integration

MPI_Comm_spawn in singleton mode works with OpenMPI 5.x inside hegel fork children. Full guide: `docs/mpi-testing.md`.

**Critical details:**
- **`MPI_Intercomm_merge` is required** after `MPI_Comm_spawn` — OpenMPI 5.x has bugs with collectives on raw intercommunicators from singleton spawn. Merge into intracommunicator first.
- **Draw ALL hegel parameters BEFORE `MPI_Comm_spawn`** — keeps draw sequence independent of MPI, allows hegel to discard cases without wasting a spawn.
- **Draw one value per rank** for heterogeneous inputs — don't draw one value and multiply by rank.
- **`SCOTCH_Num` is `int`** (not `int64_t`) — use `MPI_INT`, not `MPI_LONG_LONG_INT`.
- **`OMPI_MCA_btl=tcp,self`** prevents `/dev/shm` exhaustion from repeated spawns.
- **Worker detection**: spawned processes have `OMPI_COMM_WORLD_SIZE` env var set (OpenMPI-specific).
- Only tested with OpenMPI 5.0.7 — MPICH and others are unverified.

## Reference implementations

`make inspiration` clones hegel-rust, hegel-go, and scotch into `inspiration/`.

- **hegel-rust/hegel-go**: integrate with native test runners, lazy singleton server, expose `note()` and `target()`
- **hegel-rust shrink quality tests** use a `minimal()` helper: intentionally fail when a condition is met, assert the shrunk result equals the expected minimal value
- **Scotch**: built from source with `cd inspiration/scotch/src && make scotch` (and `make ptscotch` for MPI). Requires `Makefile.inc` — copy from `Make.inc/Makefile.inc.x86-64_pc_linux2` and set `CCS=CCP=CCD=mpicc` for PT-Scotch.

## Important notes

- Generator combinators take ownership of sub-generators — don't free sub-generators after passing them to a combinator
- The `from-hegel-rust` test suite uses `RUST_SOURCE:` comments to map each C test to its Rust original. `make verify` uses Claude (haiku) to semantically compare them.
- The `from-hegel-rust` integer tests use `hegel_run_test_result_n(..., 1000)` for `find_any` edge cases — Rust's `find_any` uses `max_attempts=1000`.
