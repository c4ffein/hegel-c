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

Each test should have three layers (NOTE: current tests don't all follow this yet):

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

## Important notes

- The Rust bridge is the current implementation; a future pure-C implementation connecting via socket is planned
- Generator combinators take ownership of sub-generators — don't free sub-generators after passing them to a combinator
- Scotch-specific helpers (`graph_gen.h`, `scotch_helpers.h`) belong in the Scotch test harness, not in hegel-c
