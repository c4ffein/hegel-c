# CLAUDE.md

## Project overview

hegel-c is a C binding for Hegel, a property-based testing framework. It provides a pure C API backed by a Rust FFI bridge that connects to the Hegel server (a Rust/Python service). C tests include the headers, link against a static `.a`, and never see Rust.

**Two layers of public API:**
- `hegel_c.h` — primitive draws, spans, asserts. The FFI boundary.
- `hegel_gen.h` — higher-level schema/shape system for describing and generating C structs declaratively. Pure C, uses `hegel_c.h` as its backend. Compiled via the `cc` crate in `rust-version/build.rs` and linked into `libhegel_c.a`.

Most new code should use the schema API — it handles allocation, span annotation, ownership tracking, and cleanup automatically. The primitive API is still there for simple scalar tests or when you need finer control.

**Status**: Work in progress. V0 schema API done. Author: c4ffein. License: MIT.

## Architecture

- `hegel_c.h` — Public C API (opaque types, draw functions, old combinator generators, assertions, suite API, spans)
- `hegel_gen.h` / `hegel_gen.c` — Schema system (declarations + pure-C implementation). See `docs/schema-api.md` and `docs/patterns.md`.
- `rust-version/src/lib.rs` — Rust FFI implementation (primitives), compiled to `libhegel_c.a`
- `rust-version/build.rs` — cc-crate build script that also compiles `hegel_gen.c` into `libhegel_c.a`
- `tests/selftest/` — 32 self-tests (PASS/FAIL/CRASH) including 9 schema pattern tests
- `tests/from-hegel-rust/` — 26 tests ported from hegel-rust
- `tests/mpi/` — 3 MPI tests (mpiexec + MPI_Comm_spawn patterns)
- `tests/irl/scotch/` — 2 Scotch integration tests (sequential + PT-Scotch MPI)
- `docs/schema-api.md` — schema system reference
- `docs/patterns.md` — pattern catalog (tests as documentation)
- `docs/mpi-testing.md` — MPI integration guide
- `TODO.md` — deferred items

Two execution modes:
- **Fork mode** (default): each test case runs in a forked child; parent serves draw requests via pipe IPC. Crash-safe.
- **Nofork mode**: single process, no crash isolation. For benchmarking or MPI_Comm_spawn.

## Build and test

Prerequisites: GCC, Rust toolchain (cargo), `uv` (Python package manager — hegeltest auto-installs the Hegel server on first run).

Optional: `mpicc`/OpenMPI for MPI tests, Scotch/PT-Scotch for IRL tests.

```bash
make help                       # all targets and proxy commands

make selftest-test              # 32 tests (24 PASS, 5 FAIL, 3 CRASH)
make from-hegel-rust-test       # 19 binaries covering 26 Rust tests (13 PASS, 6 SHRINK)
make mpi-test                   # 3 tests (needs mpicc)
make scotch-test                # 2 tests (needs Scotch — clone via make inspiration)

make inspiration                # clone hegel-rust, hegel-go, hegel-cpp, scotch into inspiration/
```

All test Makefiles use `REPO_ROOT = $(abspath ../..)` (or `../../..` for irl/scotch). Tests `cd` to `REPO_ROOT` before executing so the Hegel server path resolves.

For standalone compilation: `gcc -O2 -I. -funwind-tables -fexceptions -o test test.c -Lrust-version/target/release -lhegel_c -lpthread -lm -ldl`

## C API summary

**Runners (from `hegel_c.h`):**
- `hegel_run_test(fn)` / `_n(fn, n)` — fork mode, `exit(1)` on failure
- `hegel_run_test_result(fn)` / `_n(fn, n)` — fork mode, returns 0/1 (no exit)
- `hegel_run_test_nofork(fn)` / `_n(fn, n)` — no fork, no crash isolation
- `hegel_suite_new/add/run/free` — multi-test runner sharing one server

**Primitive draws:** `hegel_draw_int`, `_i64`, `_u64`, `_usize`, `_float`, `_double`, `_text`, `_regex`

**Spans:** `hegel_start_span(tc, label)`, `hegel_stop_span(tc, discard)` — structural shrinking hints. Users rarely call these directly; the schema API emits them automatically.

**Legacy combinator generators:** `hegel_gen_int`, `_i64`, `_u64`, `_float`, `_double`, `_bool`, `_text`, `_regex`, `_one_of`, `_sampled_from`, `_optional`, `_map_*`, `_filter_*`, `_flat_map_*`. These predate the schema API and overlap with it; prefer `hegel_gen.h` for new code.

**Other:** `hegel_note(tc, msg)` (debug on final replay), `hegel_assume(tc, cond)`, `hegel_fail(msg)`, `HEGEL_ASSERT(cond, fmt, ...)`

## Schema API (`hegel_gen.h`)

The schema system lets tests describe C structs declaratively and get generation/allocation/cleanup/spans for free.

**Three-layer architecture:**
1. **Schema tree** (`hegel_schema_t`) — user-built description of the type, reference-counted
2. **Shape tree** (`hegel_shape *`) — per-run metadata, built on draw, owns the value memory
3. **Value memory** — the actual C struct passed to the tested function

**Wrapper type:** `hegel_schema_t` is a newtype struct `{ hegel_schema * _raw; }` — distinct C type from raw pointers, zero runtime cost. Users never touch `_raw`. Detailed rationale is in the `hegel_gen.h` header comment.

**Ownership:** starts at refcount=1, passing to a parent transfers the reference (no bump). For sharing across multiple parents, explicitly call `hegel_schema_ref(s)` before each extra use. `hegel_schema_free` decrements; actual free at zero.

**Low-level schema constructors (pure values, no positions):**
- Integers: `hegel_schema_i8` through `u64`, plus `int` / `long` / `_range` variants
- Floats: `hegel_schema_float` / `_range`, `hegel_schema_double` / `_range`
- Text: `hegel_schema_text(min_len, max_len)`
- `hegel_schema_self()` — recursive reference
- `hegel_schema_optional_ptr(inner)`, `hegel_schema_array(elem, lo, hi)`, etc.

**Macros (the positional user-facing surface):**
- `HEGEL_STRUCT(T, field_entries...)` — computes offsets from the struct type at runtime, asserts `sizeof(T) == computed_total`. Top-level composition primitive.
- `HEGEL_INLINE(T, field_entries...)` / `HEGEL_INLINE_REF(T, schema)` — inline-by-value sub-struct field. Lays out `sizeof(T)` bytes in the parent slot; fields are drawn into that region (no separate allocation). Nests recursively; inner `sizeof(T)` assert fires at schema-build time. `_REF` form plugs in a pre-built struct schema; the `(sch, sizeof(T))` match is asserted.
- `HEGEL_INT(lo, hi)` / `HEGEL_INT()` (full range) — 0-vs-2-arg overloaded via `__VA_OPT__`; same for `_I8`/`_U8`/`_I16`/…/`_DOUBLE` / `_LONG` / `_FLOAT`
- `HEGEL_TEXT(lo, hi)` — `char *` field, pointer-sized slot
- `HEGEL_OPTIONAL(inner)` — 50/50 nullable pointer; 1 slot
- `HEGEL_SELF()` — optional recursive pointer; 1 slot
- `HEGEL_ARRAY(elem, lo, hi)` — builds an array schema; not a direct layout entry. Project into the parent struct via `HEGEL_FACET(hat, value)` + `HEGEL_FACET(hat, size)`. Facets may be non-adjacent, in either order. Caller must `hegel_schema_free(hat)` after building.
- `HEGEL_FACET(hat, value|size)` — project a facet of a composite schema (e.g. `HEGEL_ARRAY`) into a single parent slot. Bumps source refcount per use. Per-struct-instance scoping: two facets of the same `hat` in one struct share; across struct instances (e.g. array elements) each gets its own draw.
- `HEGEL_ARRAY_INLINE(elem, elem_sz, lo, hi)` — 2 slots: `void *` pointer + `int` count. Contiguous elements; user's struct must put ptr before count.
- `HEGEL_UNION(cases...)` — cluster slot: int tag + union body (sized/aligned to widest case)
- `HEGEL_UNION_UNTAGGED(cases...)` — cluster slot: union body only, tag in shape tree
- `HEGEL_VARIANT(case_struct_schemas...)` — cluster slot: int tag + `void *` ptr
- `HEGEL_ONE_OF_STRUCT(cases...)` — pointer-producing schema; used as `HEGEL_ARRAY` elem or inside `HEGEL_OPTIONAL`
- `HEGEL_CASE(field_entries...)` — used inside `HEGEL_UNION*`; contains layout entries, NOT bindings
- `HEGEL_MAP_INT(source, fn, ctx)` / `HEGEL_FILTER_INT(source, pred, ctx)` / `HEGEL_FLAT_MAP_INT(source, fn, ctx)` — 1 slot (int-sized); same for `_I64` and `_DOUBLE`
- `HEGEL_ONE_OF(scalar_schemas...)` — 1 slot, size/align inferred from the first case's kind
- `HEGEL_BOOL()` — 1-byte `bool` slot
- `HEGEL_REGEX(pattern, capacity)` — `char *` slot
- `hegel_schema_of(layout_entry)` — unwrap a `HEGEL_UNION` / `HEGEL_VARIANT` layout entry to a raw `hegel_schema_t` for standalone use (e.g. as an `ARRAY_INLINE` element type)

The positional form means **the user writes a flat list of generators in the same order as the struct fields**, with matching types. The layout pass computes byte offsets the same way the C compiler does and asserts `sizeof(T)` matches. If a field is reordered or its type changes, the assert fires at schema-build time.

**Draw / free:**
- `hegel_shape *hegel_schema_draw(tc, schema, (void**)&ptr)` — allocates, fills, returns shape
- `hegel_shape_free(sh)` — walks shape tree, frees value memory + shape
- `hegel_schema_free(schema)` — refcount-decrement, free schema at zero

**Shape accessors** (for untagged unions, parallel-length patterns):
- `hegel_shape_tag(sh)` — variant index
- `hegel_shape_array_len(sh)` — array length
- `hegel_shape_is_some(sh)` — optional present/absent
- `hegel_shape_field(sh, i)` — positional struct field access (TODO: named accessors)

**See:** `docs/schema-api.md` for the full reference, `docs/patterns.md` for a catalog of C memory layouts with links to the test files that demonstrate each.

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

- **Rust**: `hegeltest = "0.4"` (resolves to 0.4.3), `libc = "0.2"`, `cc = "1.0"` (build-dependency for compiling `hegel_gen.c`)
- **C linking**: `-lhegel_c -lpthread -lm -ldl` (add `-lscotch -lscotcherr -lz -lrt` for Scotch tests)
- Rust lib compiled as `staticlib` with `panic = "unwind"`; `hegel_gen.c` is compiled via `cc::Build` in `rust-version/build.rs` and archived into the same `libhegel_c.a`

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

`make inspiration` clones hegel-rust, hegel-go, hegel-cpp, and scotch into `inspiration/`.

- **hegel-rust/hegel-go**: integrate with native test runners, lazy singleton server, expose `note()` and `target()`
- **hegel-rust shrink quality tests** use a `minimal()` helper: intentionally fail when a condition is met, assert the shrunk result equals the expected minimal value
- **hegel-cpp**: C++20 / CMake `FetchContent` / reflect-cpp-based automatic schema generation. Upstream self-declares as "not blessed" — rough, expected to lag the mature bindings. Transport is a binary packet protocol with CBOR payloads over a Unix socket (20-byte header: magic `HEGL`, CRC32, stream ID, message ID, length), not stdio. Good contrast point for hegel-c's manual positional-macro schema API and stdio transport.
- **Scotch**: built from source with `cd inspiration/scotch/src && make scotch` (and `make ptscotch` for MPI). Requires `Makefile.inc` — copy from `Make.inc/Makefile.inc.x86-64_pc_linux2` and set `CCS=CCP=CCD=mpicc` for PT-Scotch.

## Important notes

- Generator combinators take ownership of sub-generators — don't free sub-generators after passing them to a combinator
- The `from-hegel-rust` test suite uses `RUST_SOURCE:` comments to map each C test to its Rust original. `make verify` uses Claude (haiku) to semantically compare them.
- The `from-hegel-rust` integer tests use `hegel_run_test_result_n(..., 1000)` for `find_any` edge cases — Rust's `find_any` uses `max_attempts=1000`.
