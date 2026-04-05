# CLAUDE.md

## Project overview

hegel-c is a C binding for Hegel, a property-based testing framework. It provides a pure C API (`hegel_c.h`) backed by a Rust FFI bridge (`rust-version/src/lib.rs`) that connects to the Hegel server (a Rust/Python service). C tests include the header, link against a static `.a`, and never see Rust.

**Status**: Work in progress. Author: c4ffein. License: MIT.

## Architecture

- `hegel_c.h` — Public C API (opaque types, draw functions, generators, assertions)
- `rust-version/src/lib.rs` — Rust FFI implementation (~1600 lines), compiled to `libhegel_c.a`
- `tests/selftest/` — 16 self-tests + Makefile harness

Two execution modes:
- **Fork mode** (default): each test case runs in a forked child; parent serves draw requests via pipe IPC. Crash-safe.
- **Nofork mode**: single process, no crash isolation. For benchmarking only.

## Build and test

Prerequisites: GCC, Rust toolchain (cargo), Hegel server at `../../.hegel/venv/bin/hegel`, Scotch libraries.

```bash
cd tests/selftest

# Build Rust library only
make hegel-c-lib

# Build all test binaries
make all

# Run full test suite (builds, runs 16 tests, checks SPDX headers)
make test

# Clean test binaries
make clean
```

Tests must be run from the repo root (the Makefile `cd`s to `REPO_ROOT` before executing each binary so the Hegel server path resolves).

## Test categories

The Makefile defines three categories — each test binary's expected exit code depends on its category:

- **TESTS_PASS** (10 tests): should exit 0 — property holds
- **TESTS_FAIL** (3 tests): should exit non-zero — Hegel detects a known bug and shrinks
- **TESTS_CRASH** (3 tests): should exit non-zero — fork isolation catches SIGSEGV/SIGABRT/stack overflow

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

All source files (.c, .h, .rs, Makefile) must start with an SPDX header:
```
/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
```
This is enforced by `make test` via the `check-license` target.

## Key dependencies

- **Rust**: `hegeltest = "0.1"` (Hegel framework), `libc = "0.2"` (POSIX FFI)
- **C linking**: `-lhegel_c -lscotch -lscotcherr -lpthread -lm -ldl`
- Rust lib compiled as `staticlib` with `panic = "unwind"`

## Server lifecycle and pooling

The `hegeltest` crate uses a process-wide singleton (`OnceLock<HegelSession>`) to manage the Hegel server. The server subprocess is spawned once on the first `Hegel::new(...).run()` call and reused for all subsequent calls in the same process. Communication is via stdin/stdout pipes (`--stdio` mode) with a multiplexed CBOR protocol.

This means multiple `hegel_run_test()` calls in a single binary automatically share one server — no extra pooling needed. The current ~1s-per-test startup cost exists only because we run 16 separate binaries. Bundling PASS tests into one binary would eliminate most of that overhead.

## Fork mode architecture

In fork mode, the **parent** process owns the Hegel server connection. For each test case:
1. Parent creates request/response pipes, then `fork()`s
2. Child runs the C test function; `hegel_draw_*()` calls write requests to the pipe
3. Parent reads requests, forwards them to the Hegel server, writes responses back
4. If child crashes (SIGSEGV, SIGABRT), parent catches it via `waitpid()` and reports failure to Hegel for shrinking

The child never talks to the Hegel server directly — the parent proxies all communication. This is what makes crash isolation work without losing shrinking.

## Shrinking

Hegel uses **integrated shrinking** (from Hypothesis), not type-based shrinking (QuickCheck). All generation consumes from a shared byte stream. Shrinking operates on that byte stream — making it shorter or lexicographically smaller — then replays generation. This has important consequences:

- `map()`, `filter()`, `flat_map()` do NOT degrade shrinking quality. The server shrinks the underlying bytes, then the combinator chain re-runs.
- Simplicity ordering is built into the byte representation: `false` < `true`, `0` < `1` < `-1` < `2` < `-2`...
- You can invert the simplicity ordering with `map()`: `booleans().map(|b| !b)` makes `true` the minimal value.
- All shrinking logic lives in the Hegel server (Python/Hypothesis). Client libraries don't implement shrinking.

## Failure semantics: hegel-c vs hegel-rust/go

In hegel-rust, `Hegel::run()` **panics** on property failure — Rust's test harness catches the panic, other tests keep running, server stays alive. In hegel-go, `hegel.Run()` **returns an error**.

In hegel-c, `hegel_run_test()` calls `exit(1)` on failure — the process dies. This means:
- Each test must be a separate binary (can't chain FAIL/CRASH tests)
- Each binary spawns its own server (~1s overhead each)
- A future `hegel_run_test_result()` returning 0/1 instead of exiting would enable test suites that share a single server

## Reference implementations

`make inspiration` clones hegel-rust and hegel-go into a gitignored `inspiration/` directory for reference. Key design observations from those libraries:

- **No explicit test suite API** — both integrate with native test runners (`cargo test`, `go test`). Hegel provides the property test primitive, not the orchestration.
- **Server is invisible to users** — lazy singleton, no manual lifecycle management.
- **Both expose `note()` and `target()`** — debug output during replay and property-directed testing. hegel-c has neither yet (tracked in README TODO).
- **hegel-rust has shrink quality tests** — in `tests/test_shrink_quality/`, using a `minimal()` helper that exploits Hegel's shrinking by intentionally failing when a condition is met, then asserting the shrunk result equals the expected minimal value.

## Important notes

- The Rust bridge is the current implementation; a future pure-C implementation connecting via socket is planned
- Generator combinators take ownership of sub-generators — don't free sub-generators after passing them to a combinator
- Scotch-specific helpers (`graph_gen.h`, `scotch_helpers.h`) belong in the Scotch test harness, not in hegel-c
- The selftest Makefile uses `HEGEL_DIR = ..` — it expects to live inside a parent repo (e.g., Scotch) where `hegel/` is a subdirectory. For standalone development, compile manually: `gcc -O2 -I. -funwind-tables -fexceptions -o test test.c -Lrust-version/target/release -lhegel_c -lpthread -lm -ldl`
