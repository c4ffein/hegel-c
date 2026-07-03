# CLAUDE.md

## Project overview

hegel-c is a C binding for Hegel, a property-based testing framework. It is pure C end to end: tests include the headers and link a static `libhegel_c.a`; at runtime the library dlopens `libhegel.so` — Hegel's official native engine (a Rust cdylib from the hegel-rust repo, prebuilt per platform on its GitHub releases). There is no server process, no Python, and no Rust in our build. (History: until 2026-06 this binding was a Rust FFI bridge to the Python `hegel-core` server; upstream 0.17.0 deleted the server and shipped the in-process engine + C ABI we now sit on.)

**Two layers of public API:**
- `hegel_c.h` — primitive draws, spans, asserts, runners. The transport boundary.
- `hegel_gen.h` — higher-level schema/shape system for describing and generating C structs declaratively. Pure C, uses `hegel_c.h` as its backend.

Most new code should use the schema API — it handles allocation, span annotation, ownership tracking, and cleanup automatically. The primitive API is still there for simple scalar tests or when you need finer control.

**Status**: Work in progress. V0 schema API done. Author: c4ffein. License: MIT.

## Architecture

- `hegel_c.h` — Public C API (opaque types, draw functions, old combinator generators, assertions, suite API, spans)
- `hegel_gen.h` / `hegel_gen.c` — Schema system (declarations + pure-C implementation). See `docs/schema-api.md` and `docs/patterns.md`.
- `core/` — The pure-C runtime under `hegel_c.h`:
  - `hegel_engine.{c,h}` — dlopen binding to `libhegel.so` (search order: `$HEGEL_LIBHEGEL_PATH`, `./build/libhegel.so`, `./.hegel/libhegel/libhegel.so`, system). dlsym keeps our `hegel_start_span`/`hegel_stop_span` from colliding with libhegel's same-named exports.
  - `hegel_cbor.{c,h}` — minimal CBOR codec (descended from `purec/`) for `hegel_generate`'s schema-in / value-out exchange. Strings arrive as tag 91 (WTF-8) byte strings; floats at the smallest lossless width (incl. float16).
  - `hegel_runtime.c` — draws (schema encode → generate → decode), spans, notes, assume/fail/health, target. Each draw has an INPROC path (call engine) and a CHILD path (framed pipe request to the fork parent).
  - `hegel_runner.c` — run drivers (fork + nofork), the fork parent's serve loop, failure reporting, suite API.
  - `hegel_stateful.c` — stateful run loop (preconditions, rule-level escapes), port of the old `stateful.rs`.
  - `hegel_internal.h` — test-case struct, wire-protocol message defs (designed to extend to a remote/board transport later).
- `tests/selftest/` — 76 self-tests (PASS/FAIL/CRASH/HEALTH-CHECK) including schema pattern, binding, and stateful tests
- `tests/from-hegel-rust/` — 11 tests ported from hegel-rust (5 PASS, 6 SHRINK)
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

Prerequisites: GCC. That's it for using the library — the engine cdylib (`libhegel.so`) can be a prebuilt artifact from hegel-rust's GitHub releases. `make libhegel` builds it from source instead, from the `deps/hegel-rust` submodule (pinned to the engine version the runtime is verified against), which needs cargo (the only place Rust appears, and only at build-from-source time).

Optional: `mpicc`/OpenMPI for MPI tests, Scotch/PT-Scotch for IRL tests.

```bash
make help                       # all targets and proxy commands

make lib                        # build build/libhegel_c.a (pure C, seconds)
make libhegel                   # place the engine at build/libhegel.so (cargo or prebuilt)

make selftest-test              # 76 tests (PASS/FAIL/CRASH/HEALTH-CHECK)
make from-hegel-rust-test       # 11 tests (5 PASS, 6 SHRINK)
make mpi-test                   # 3 tests (needs mpicc)
make scotch-test                # 2 tests (needs Scotch — clone via make inspiration)

make inspiration                # clone third-party repos into inspiration/{hegel,existing-pbt-in-c,targets}/
```

All test Makefiles use `REPO_ROOT = $(abspath ../..)` (or `../../..` for irl/scotch). Tests `cd` to `REPO_ROOT` before executing so the runtime loader finds `./build/libhegel.so`; `HEGEL_LIBHEGEL_PATH` overrides the search.

For standalone compilation: `gcc -O2 -I. -o test test.c -Lbuild -lhegel_c -lpthread -lm -ldl` (run with cwd at the repo root, or export `HEGEL_LIBHEGEL_PATH`). `-funwind-tables -fexceptions` are no longer required — nothing unwinds through C frames anymore.

## C API summary

**Runners (from `hegel_c.h`):**
- `hegel_run_test(fn)` / `_n(fn, n)` — fork mode, `exit(1)` on failure
- `hegel_run_test_result(fn)` / `_n(fn, n)` — fork mode, returns 0/1 (no exit)
- `hegel_run_test_nofork(fn)` / `_n(fn, n)` — no fork, no crash isolation
- `hegel_suite_new/add/run/free` — multi-test runner (one binary, several tests; each test is its own engine run)

**Primitive draws:** `hegel_draw_int`, `_i64`, `_u64`, `_usize`, `_float`, `_double`, `_text`, `_regex`

**Spans:** `hegel_start_span(tc, label)`, `hegel_stop_span(tc, discard)` — structural shrinking hints. Users rarely call these directly; the schema API emits them automatically.

**Legacy combinator generators:** `hegel_gen_int`, `_i64`, `_u64`, `_float`, `_double`, `_bool`, `_text`, `_regex`, `_one_of`, `_sampled_from`, `_optional`, `_map_*`, `_filter_*`, `_flat_map_*`. These predate the schema API and overlap with it; prefer `hegel_gen.h` for new code.

**Other:** `hegel_note(tc, msg)` (debug on final replay), `hegel_assume(tc, cond)`, `hegel_fail(msg)` (code-under-test broken), `hegel_health_fail(msg)` (test-setup broken — emits "Health check failure:" prefix), `HEGEL_ASSERT(cond, fmt, ...)`

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
- `hegel_schema_optional_ptr(inner)`, `hegel_schema_arr_of(length, elem)`, etc.

**Macros (the positional user-facing surface):**
- `HEGEL_STRUCT(T, field_entries...)` — computes offsets from the struct type at runtime, asserts `sizeof(T) == computed_total`. Top-level composition primitive.
- `HEGEL_INLINE(T, field_entries...)` / `HEGEL_INLINE_REF(T, schema)` — inline-by-value sub-struct field. Lays out `sizeof(T)` bytes in the parent slot; fields are drawn into that region (no separate allocation). Nests recursively; inner `sizeof(T)` assert fires at schema-build time. `_REF` form plugs in a pre-built struct schema; the `(sch, sizeof(T))` match is asserted.
- `HEGEL_INT(lo, hi)` / `HEGEL_INT()` (full range) — 0-vs-2-arg overloaded via `__VA_OPT__`; same for `_I8`/`_U8`/`_I16`/…/`_DOUBLE` / `_LONG` / `_FLOAT`
- `HEGEL_TEXT(lo, hi)` — `char *` field, pointer-sized slot
- `HEGEL_OPTIONAL(inner)` — 50/50 nullable pointer; 1 slot
- `HEGEL_SELF()` — optional recursive pointer; 1 slot
- `HEGEL_BINDING(name)` — declares a compile-time binding id (expands to `enum { name = __COUNTER__ }`). Typo → compile error. Function-local scope is the pit of success; file-scope works; headers discouraged (per-TU `__COUNTER__` values don't match across translation units).
- `HEGEL_LET(name, inner)` — non-positional: draws `inner` and caches the value under `name` in the enclosing struct's draw ctx. No slot consumed. Inner must be a scalar INTEGER (width 4 or 8) or FLOAT (single or double); composed scalar schemas (`HEGEL_MAP_INT`, etc.) are also accepted.
- `HEGEL_USE(name)` / `_I64` / `_U64` / `_FLOAT` / `_DOUBLE` — reads the cached value and writes it to the current slot (1 slot, width matching the variant). Also usable as a length parameter to `HEGEL_ARR_OF` (int variant only). Walks the scope chain outward, so a USE in an inner struct resolves a LET in any enclosing struct. Width / sign / float-ness must match the LET inner kind — mismatch is a hard abort. Unresolved → hard abort with actionable message (schema authoring error, not a discardable filter).
- `HEGEL_LET_ARR(name, length, elem)` — non-positional: draws a length, then `length` ints from `elem`, and caches the resulting `int[]` under `name`. No struct slot. Read elements via `HEGEL_USE_AT(name)`.
- `HEGEL_USE_AT(name)` — int slot. Looks up the named array binding (walks scope chain), reads the `current_index` of the scope where the binding was declared, and writes `arr[current_index]` to the slot. The `current_index` is set by the enclosing `HEGEL_ARR_OF` while it iterates. Canonical pattern: parent struct does `HEGEL_LET_ARR(sizes, ...)`, parent's `HEGEL_ARR_OF` iterates over child structs, each child uses `HEGEL_USE_AT(sizes)` to get the i-th size — exactly the "sizes drives groups" pattern.
- `HEGEL_USE_PATH(<HEGEL_PARENT>*, name [, HEGEL_INDEX_HERE])` — int slot. Variadic explicit-path resolver: zero or more `HEGEL_PARENT` steps SKIP that many scopes outward before lookup, then resolves the named binding. Optional trailing `HEGEL_INDEX_HERE` indexes into a `HEGEL_LET_ARR` binding (same semantics as `HEGEL_USE_AT`). Use when you need to bypass a closer same-named binding to reach an outer one (the only case `HEGEL_USE_AT` / `HEGEL_USE` can't express). Path sentinels are negative ints; binding ids are non-negative — runtime parser disambiguates by sign.
- `HEGEL_ARR_OF(length_schema, elem_schema)` — 1 slot (pointer). Length must be **either a named binding** (`HEGEL_USE` / `HEGEL_USE_AT` / `HEGEL_USE_PATH`) **or a literal** (`HEGEL_CONST(N)`); raw `HEGEL_INT(lo,hi)` and composed scalars (`HEGEL_MAP_INT`, etc.) are rejected at schema-build time with an actionable abort. Wrap drawn lengths in `HEGEL_LET` first. Element kinds supported today: INTEGER, STRUCT, OPTIONAL_PTR (enables `HEGEL_SELF()` for recursive trees), ONE_OF_STRUCT, MAP_INT / FILTER_INT / FLAT_MAP_INT.
- `HEGEL_LEN_PREFIXED_ARRAY(length_schema, elem_schema)` — 1 slot (pointer). Pascal-string-style: buffer of (n+1) elements where slot 0 is the drawn length n cast to elem's int type and slots 1..n are drawn from elem. Length constraint same as `HEGEL_ARR_OF`. Element must be `HEGEL_SCH_INTEGER` (any width). Drawn n must fit in elem type's representable range — exceeded values abort at draw time.
- `HEGEL_TERMINATED_ARRAY(length_schema, elem_schema, sentinel)` — 1 slot (pointer). Null-terminated-style: buffer of (n+1) elements where slots 0..n-1 are drawn from elem and slot n is the literal sentinel. Same length/element constraints as LEN_PREFIXED. Schema-build-time check: if elem is a bounded HEGEL_INT or HEGEL_CONST and the sentinel could collide with a drawn element, the constructor aborts. For derived elem schemas (USE / MAP / etc.), runtime check fires on first collision.
- `HEGEL_ARRAY_INLINE(elem, elem_sz, lo, hi)` — 2 slots: `void *` pointer + `int` count. Contiguous elements; user's struct must put ptr before count.
- `HEGEL_UNION(cases...)` — cluster slot: int tag + union body (sized/aligned to widest case)
- `HEGEL_UNION_UNTAGGED(cases...)` — cluster slot: union body only, tag in shape tree
- `HEGEL_VARIANT(case_struct_schemas...)` — cluster slot: int tag + `void *` ptr
- `HEGEL_ONE_OF_STRUCT(cases...)` — pointer-producing schema; used as `HEGEL_ARR_OF` elem or inside `HEGEL_OPTIONAL`
- `HEGEL_CASE(field_entries...)` — used inside `HEGEL_UNION*`; contains layout entries, NOT bindings
- `HEGEL_MAP_INT(source, fn, ctx)` / `HEGEL_FILTER_INT(source, pred, ctx)` / `HEGEL_FLAT_MAP_INT(source, fn, ctx)` — 1 slot (int-sized); same for `_I64` and `_DOUBLE`
- `HEGEL_ONE_OF(scalar_schemas...)` — 1 slot, size/align inferred from the first case's kind
- `HEGEL_BOOL()` — 1-byte `bool` slot
- `HEGEL_REGEX(pattern, capacity)` — `char *` slot
- `hegel_schema_of(layout_entry)` — unwrap a `HEGEL_UNION` / `HEGEL_VARIANT` layout entry to a raw `hegel_schema_t` for standalone use (e.g. as an `ARRAY_INLINE` element type)

**Idiomatic array + count pattern.** A struct with a pointer and a count field uses the binding system to keep them coherent:
```c
typedef struct { int *items; int n; } Bag;
HEGEL_BINDING (n);
schema = HEGEL_STRUCT (Bag,
    HEGEL_LET    (n, HEGEL_INT (0, 10)),                 /* non-positional */
    HEGEL_ARR_OF (HEGEL_USE (n), HEGEL_INT (0, 100)),    /* int *items */
    HEGEL_USE    (n));                                    /* int n */
```
Because LET is non-positional, the USE that writes to the count field can appear in any layout position — before, between, or after the ARR_OF. Per-struct-instance scoping means nested arrays-of-structs each draw their own value (e.g. jagged 2D: see `tests/selftest/test_binding_jagged_2d.c`).

The positional form means **the user writes a flat list of generators in the same order as the struct fields**, with matching types. The layout pass computes byte offsets the same way the C compiler does and asserts `sizeof(T)` matches. If a field is reordered or its type changes, the assert fires at schema-build time.

**Draw / free:**
- `hegel_shape *hegel_schema_draw(tc, schema, (void**)&ptr)` — struct-oriented: allocates, fills, returns shape
- `HEGEL_DRAW(&addr, sch)` — unified write-at-address entry point. STRUCT kind allocates and writes the pointer at `*addr`; scalar / text / optional / union / variant write the value directly at `addr`. Returns a `hegel_shape *` (leaf for scalars, real tree for composites) — always free it. ARRAY / ARRAY_INLINE / SELF / ONE_OF_STRUCT abort at the top level (they only compose inside a struct). Schema is not consumed.
- `HEGEL_DRAW_INT(lo, hi)` / `HEGEL_DRAW_INT()` (full range) — dispatches directly to the `hegel_draw_int` primitive. Same `(lo, hi)` / `()` overloading for `_I64` / `_U64` / `_DOUBLE` / `_FLOAT`. `HEGEL_DRAW_BOOL()` takes no args. No schema allocation. For composed scalar schemas (`HEGEL_MAP_INT`, `HEGEL_FILTER_INT`, `HEGEL_FLAT_MAP_INT`, `HEGEL_ONE_OF`), hoist and use `HEGEL_DRAW(&x, sch)`. No `HEGEL_DRAW_ARRAY` by design.
- `hegel_shape_free(sh)` — walks shape tree, frees value memory + shape (NULL-safe)
- `hegel_schema_free(schema)` — refcount-decrement, free schema at zero
- `HEGEL_DEFAULT_MAX_DEPTH = 50` — recursion cap for `HEGEL_SELF`. Exhaustion calls `hegel_health_fail`. Override via `hegel_schema_draw_n` / `hegel_schema_draw_at_n`.

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

- **Runtime**: `libhegel.so` ≥ 0.17 (Hegel's native engine, loaded via dlopen). No Rust or Python dependency in our build.
- **C linking**: `-lhegel_c -lpthread -lm -ldl` (add `-lscotch -lscotcherr -lz -lrt` for Scotch tests)

## libhegel (the engine)

We sit on the official C ABI from the hegel-rust repo (`hegel-c/include/hegel.h` there, crate `hegeltest-c`, prebuilt `libhegel-<goos>-<goarch>.so` on GitHub releases). Run loop: `hegel_run_start` → `hegel_next_test_case` (engine on a worker thread) → draws via `hegel_generate(schema_cbor) → value_cbor` → `hegel_mark_complete(status, origin)` → `hegel_run_result`.

Engine facts that shaped the runtime (verified against 0.17.4):
- Schema dialect is the old hegel-core "library API" spec: `{"type": "integer", "min_value", "max_value"}` etc. Spec reference: `inspiration/hegel/hegel-core/docs/library-api.md`.
- Strings come back as CBOR tag 91 (WTF-8) wrapping a byte string; floats at the smallest lossless width (float16/32/64). Our codec handles all of these.
- Float schemas default `allow_nan`/`allow_infinity` to TRUE — the runtime sets them explicitly for bounded draws (Hypothesis's "bounds imply no NaN" is NOT inferred engine-side).
- Text schemas get `min_codepoint=1` so drawn strings never contain NUL (they're C strings).
- `hegel_generate` can return `HEGEL_E_ASSUME` (engine-side rejection, e.g. a discarded filter span exhausting retries) — treated as "discard the case" (INVALID), alongside `HEGEL_E_STOP_TEST` → OVERRUN.
- `origin` strings passed to `mark_complete` identify *which bug* a failure is — `HEGEL_ASSERT` passes its file:line so distinct asserts shrink as distinct bugs; messages embedding drawn values must never be origins.
- Engine health checks surface as failures whose panic message starts with `FailedHealthCheck` — the runner prints them with our `Health check failure:` prefix (same prefix as `hegel_health_fail`).
- `hegel_target()` is now exposed (the 0.4.3-era "blocked upstream" note is obsolete).
- "Draw N:" traces are printed by OUR runtime, gated on `hegel_test_case_is_final_replay` — the engine no longer prints them.

## Fork mode architecture

In fork mode, the **parent** process owns the engine (in-process, via libhegel). For each test case:
1. Parent calls `hegel_next_test_case`, creates request/response pipes, then `fork()`s
2. Child runs the C test function; `hegel_draw_*()` calls write framed requests to the pipe
3. Parent serves requests by calling the engine, writes responses back
4. Child ends with a terminal message (DONE / STOPPED / ASSUME / FAIL / HEALTH); if it crashes instead (SIGSEGV, SIGABRT), parent catches it via `waitpid()` and marks the case INTERESTING so the engine shrinks it

The child never touches the engine — the parent proxies all draws. The message protocol (in `core/hegel_internal.h`) is deliberately transport-shaped: the same message set is meant to later run over a serial/TCP link to a remote target (embedded board), with the board taking the child's role and watchdog-reset taking fork's role as crash isolation.

In nofork mode the body runs in the parent itself; failures longjmp back to the run loop (no crash isolation).

## Shrinking

Hegel uses **integrated shrinking** (from Hypothesis), not type-based shrinking (QuickCheck). All generation consumes from a shared choice stream. Shrinking operates on that stream — making it shorter or lexicographically smaller — then replays generation.

- `map()`, `filter()`, `flat_map()` do NOT degrade shrinking quality
- Simplicity ordering: `false` < `true`, `0` < `1` < `-1` < `2` < `-2`...
- All shrinking logic lives in libhegel's native engine (a Rust port of Hypothesis's conjecture engine, audited against it in 0.17.3)

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

The one build-critical dependency, **hegel-rust** (the engine source), is a pinned git submodule at **`deps/hegel-rust`** — see Build and test. Everything else is reference material: `make inspiration` clones sister bindings and study repos into three categorized subdirs of `inspiration/`, and symlinks `deps/hegel-rust` under `inspiration/hegel/hegel-rust` for discoverability (the symlink lives in the gitignored `inspiration/`, so it is never committed).

Bug-hunt targets (Linux subsets, Scotch) are not vendored either: `scripts/fetch-target.sh` reads a committed `target.conf` (repo + pinned fix-SHA + sparse paths) and materialises a minimal, gitignored checkout at `--state buggy|fixed` — the pin is committed, the GPL source never is.

**`deps/hegel-rust`** (submodule) — home of the native engine AND the `hegel-c/` crate whose `libhegel` cdylib we sit on. `hegel-c/include/hegel.h` is the ABI contract; `hegel-c/examples/*.c` are canonical usage; `tests/common/utils.rs` has the `find_any`/`minimal()` test helpers our ported tests mirror.

**`inspiration/hegel/`** — sister hegel bindings + the canonical Agent Skill:
- **hegel-java / hegel-ocaml**: the other in-process bindings over libhegel (Java 22 FFM; OCaml ctypes with checksum-verified cdylib download). Closest design relatives to our core; OCaml's `lib/ffi/ffi.ml` + `lib/client.ml` is the cleanest small consumer.
- **hegel-go / hegel-typescript / hegel-cpp**: still on the retired Python-server model as of 2026-06; expect them to migrate to libhegel.
- **hegel-core**: the retired Python server. Kept because `docs/library-api.md` is still the schema-dialect spec `hegel_generate` accepts.
- **hegel-skill**: the canonical Agent Skill (rust/go/cpp/ts language-specific references). Adding C is the documented extension path; see CLAUDE.md inside that repo.

**`inspiration/existing-pbt-in-c/`** — competitive landscape, study these for design comparison:
- **theft** (silentbicycle): pure-C99 PBT lib. Closest prior art to hegel-c. Forking, autoshrinking from a bit pool, multi-arg properties (propfun1..propfun7), instance hashing for duplicate-trial dedup. Useful read: `doc/usage.md`, `doc/forking.md`.
- **deepstate** (Trail of Bits): C/C++ multi-backend framework — same harness runs against AFL, libFuzzer, Manticore, Angr, Eclipser. Google Test-like API with `TEST(Suite, Name)`, fixtures, `OneOf` for stateful testing, swarm testing. Useful read: `docs/test_harness.md`, `docs/swarm_testing.md`.

**`inspiration/targets/`** — real-world libraries we point hegel-c at to find bugs:
- **Scotch**: built from source with `cd inspiration/targets/scotch/src && make scotch` (and `make ptscotch` for MPI). Requires `Makefile.inc` — copy from `Make.inc/Makefile.inc.x86-64_pc_linux2` and set `CCS=CCP=CCD=mpicc` for PT-Scotch.

## Important notes

- Generator combinators take ownership of sub-generators — don't free sub-generators after passing them to a combinator
- The `from-hegel-rust` test suite uses `RUST_SOURCE:` comments to map each C test to its Rust original. `make verify` uses Claude (opus) to semantically compare them.
- The `from-hegel-rust` integer/float tests use `hegel_run_test_result_n(..., 5000)` for `find_any` edge cases — upstream's `find_any` raised `max_attempts` from 1000 to 5000 when the 0.17.3 distribution fixes capped the interesting-constants pool (boundary values are rarer per draw now).
