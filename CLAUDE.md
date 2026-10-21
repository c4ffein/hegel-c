# CLAUDE.md

## Project overview

hegel-c is a C binding for Hegel, a property-based testing framework. It is pure C end to end: tests include the headers and link a static `libhegel_c.a`; at runtime the library dlopens `libhegel.so` — Hegel's official native engine (a Rust cdylib from the hegel-rust repo, prebuilt per platform on its GitHub releases). There is no server process, no Python, and no Rust in our build. (History: until 2026-06 this binding was a Rust FFI bridge to the Python `hegel-core` server; upstream 0.17.0 deleted the server and shipped the in-process engine + C ABI we now sit on.)

**Two layers of public API:**
- `hegel_c.h` — primitive draws, spans, engine collections / recursion / pools, `HEGEL_ISOLATED`, asserts, runners. Thin over libhegel's own protocols; the transport boundary.
- `hegel_gen.h` — higher-level schema/shape system for describing and generating C structs declaratively. Pure C, uses `hegel_c.h` as its backend.

Most new code should use the schema API — it handles allocation, span annotation, ownership tracking, and cleanup automatically. The primitive API is still there for simple scalar tests or when you need finer control.

**Status**: Work in progress. V0 schema API done. Author: c4ffein. License: MIT.

## Architecture

- `hegel_c.h` — Public C API (opaque types, draw functions, spans, collections, recursion, pools, `HEGEL_ISOLATED`, assertions, suite API, stateful)
- `hegel_gen.h` / `hegel_gen.c` — Schema system (declarations + pure-C implementation). See `docs/schema-api.md` and `docs/patterns.md`.
- `core/` — The pure-C runtime under `hegel_c.h`:
  - `hegel_engine.{c,h}` — dlopen binding to `libhegel.so` (search order: `$HEGEL_LIBHEGEL_PATH`, `./build/libhegel.so`, `./.hegel/libhegel/libhegel.so`, system). Mirrors upstream's types with an `hg_` prefix; dlsym keeps our same-named public functions (`hegel_start_span`, `hegel_note`, `hegel_collection_more`, ...) from colliding with libhegel's exports. One process-wide `hegel_context_t`.
  - `hegel_internal.h` — the wire protocol (every engine operation a body can perform is one request op) and the test-case struct.
  - `hegel_server.c` — `hegel__serve_op`: the ONLY code that calls the engine on a body's behalf. Per-case handle table (collections/recursions/pools/state machines, auto-freed at case end), captured output lines, run-scoped string-generator cache.
  - `hegel_runtime.c` — the public primitives as requests; `hegel__call` picks the transport (INPROC = direct call into the server, CHILD = fork parent's pipe). Also `HEGEL_ISOLATED`.
  - `hegel_runner.c` — run drivers (fork + nofork), the fork parent's serve loop, capture-by-origin failure reporting, suite API.
  - `hegel_stateful.c` — stateful driver on the engine's state machine (`hegel_new_state_machine`), shaped like hegel-rust's `run_sequential`; preconditions map onto `rule_rejected`. Works in fork and nofork mode.
- `tests/selftest/` — 88 self-tests (PASS/FAIL/CRASH/HEALTH-CHECK) including schema pattern, binding, stateful, isolation and engine-protocol tests
- `tests/from-hegel-rust/` — 11 tests ported from hegel-rust (5 PASS, 6 SHRINK)
- `tests/mpi/` — 3 MPI tests (mpiexec + MPI_Comm_spawn patterns)
- `tests/irl/scotch/` — 2 Scotch integration tests (sequential + PT-Scotch MPI) + bug pins (`pins/`)
- `tests/irl/linux/` — Linux kernel bug-hunt harnesses, one per target, each built against a pinned kernel checkout (`pins/`)
- `docs/schema-api.md` — schema system reference
- `docs/patterns.md` — pattern catalog (tests as documentation)
- `docs/mpi-testing.md` — MPI integration guide
- `TODO.md` — deferred items

Two execution modes:
- **Fork mode** (default): each test case runs in a forked child; parent serves draw requests via pipe IPC. Crash-safe. Two flavours: *fast* (default, plain fork — the child inherits the parent's heap, so a bug that reads uninitialized memory can behave differently on replay) and *clean* (`hegel_set_fork_mode(HEGEL_FORK_CLEAN)` or `HEGEL_C_FORK=clean`: fork + re-exec the test binary with ASLR off, so every child starts from the same heap; ~2.5x slower per case). Tests that key files by PID can't run in clean mode.
- **Nofork mode**: the body runs in the runner's process and calls the engine directly. Crash isolation only inside `HEGEL_ISOLATED` blocks (generate first, then isolate the code under test — the block forks, sees every drawn value copy-on-write, and turns a crash into a shrinkable failure). Also for benchmarking or MPI_Comm_spawn.

## Build and test

Prerequisites: GCC. That's it for using the library — the engine cdylib (`libhegel.so`) can be a prebuilt artifact from hegeldev/hegel's GitHub releases. `make libhegel` builds it from source, from the `deps/hegel` submodule (pinned to the engine version the runtime is verified against): ~30 s, needs cargo ≥ the `rust-version` libhegel declares (1.86 — newer than Debian 13's packaged 1.85, so use rustup), the only place Rust appears. `make libhegel PREBUILT=1` downloads the release artifact for `LIBHEGEL_VERSION` instead, checked against the sha256 pinned in the Makefile. Builds are tagged — `build/libhegel-<version>-{source,prebuilt}.so` — and `build/libhegel.so` is a symlink to the one in use, so a bump or a switch of origin just relinks (a plain file there is moved to `build/libhegel-untagged.so`; for an engine make must not touch, use `HEGEL_LIBHEGEL_PATH`). Source builds run cargo on every `make libhegel` (~0.1 s when current) and replace the tagged file only if the result changed; without cargo or the submodule (CI test jobs), an existing tagged build is kept. `make check-libhegel` checks the submodule tag, what the symlink points at, and upstream for a newer release.

Rebuilds follow sources: `libhegel_c.a` objects track their headers (`-MMD`, `build/*.d`), and every test Makefile asks the root Makefile for the library on each run, relinking test binaries only when `libhegel_c.a` actually changed. (The `tests/irl/linux` harnesses link `build/libhegel_c.a` but don't rebuild it — run `make lib` first.)

Optional: `mpicc`/OpenMPI for MPI tests, Scotch/PT-Scotch for IRL tests.

```bash
make help                       # all targets and proxy commands

make lib                        # build build/libhegel_c.a (pure C, seconds)
make libhegel                   # build the engine into build/libhegel.so (cargo; PREBUILT=1 downloads it)
make check-libhegel             # engine pin: submodule tag, installed version, newer upstream release

make selftest-test              # 88 tests (PASS/FAIL/CRASH/HEALTH-CHECK)
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

**Primitive draws:** `hegel_draw_int`, `_i64`, `_u64`, `_usize`, `_float`, `_double`, `_bool`, `_text`, `_bytes`, `_regex` (full match)

**Spans:** `hegel_start_span(tc, label)`, `hegel_stop_span(tc, discard)` — structural shrinking hints; derive labels with `hegel_label("lib.thing")` (FNV-1a, as libhegel's `hegel_label_from_name`). Users rarely call these directly; the schema API emits them automatically.

**Engine protocols** (handles are `{ int64_t id; }` newtypes, ids into the server's per-case table — so they work across the fork pipe):
- Collections: `hegel_collection_new(tc, min, max)` / `_more` / `_reject` / `_free` — engine owns the length; shrinks by element deletion.
- Recursion: `hegel_recursion_new(tc, max_depth, max_leaves)` / `_branch` / `_leaf` / `_retry` / `_finish` / `_free` — engine owns branch probabilities and budgets (see `tests/selftest/test_recursion_tree.c`).
- Pools: `hegel_pool_new` / `_add` / `_draw(consume)` / `_restore` / `_free` — ids are opaque int64s chosen by the engine (map them, don't index with them).

**Crash isolation for a block:** `HEGEL_ISOLATED(tc) { ... }` — no draws inside (health failure), no nesting, no break/return out of it. Every failure inside a block (crash, `HEGEL_ASSERT`, `hegel_fail`) shares one origin, `HEGEL_ISOLATED at <file:line>`, so a bug that sometimes asserts and sometimes segfaults shrinks as one bug. If a plain test shrinks poorly, wrapping the code under test in a block is the fix. `HEGEL_C_MERGE_ORIGINS=1` merges every failure in a run into one origin.

**Other:** `hegel_note(tc, msg)` (captured for the counterexample report), `hegel_assume(tc, cond)`, `hegel_fail(msg)` (code-under-test broken), `hegel_health_fail(msg)` (test-setup broken — emits "Health check failure:" prefix), `HEGEL_ASSERT(cond, fmt, ...)`

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
- `HEGEL_SELF()` — optional recursive pointer; 1 slot. Child-or-NULL is decided by the engine's recursion protocol (branch = child, leaf = NULL), which owns the probabilities, the depth cap and a 100-leaf budget; over-budget attempts are unwound (longjmp, tracked allocations freed) and redrawn from the root — see `hegel__draw_root` in `hegel_gen.c`.
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
- `HEGEL_DEFAULT_MAX_DEPTH = 50` — depth cap for `HEGEL_SELF` trees (at most that many levels of structs, root included; slots at the cap are NULL — not a failure). Override via `hegel_schema_draw_n` / `hegel_schema_draw_at_n`.

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

Our own source files (.c, .h, .rs, .md, Makefile) start with an MIT SPDX header:
```
/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
```
Markdown files use the same lines inside an HTML comment. Enforced by the `check-license` target in each test Makefile.

Third-party excerpts (Linux kernel code under `tests/irl/`) keep their upstream SPDX license and get a modification notice plus `SPDX-FileCopyrightText` lines right after it — never relabel them MIT, and never let them into `libhegel_c.a`. Licensing follows REUSE: texts in `LICENSES/`, header-less files covered by `REUSE.toml`, full check with `make check-reuse` (note: it over-reports ignored build artifacts inside still-untracked directories — a git `ls-files --directory` quirk).

## Key dependencies

- **Runtime**: `libhegel.so` from hegeltest-c ≥ 0.47 (Hegel's native engine, loaded via dlopen); ≥ 0.48 for correct shrinking when a second failure turns up mid-shrink (`test_shrink_failure_found_while_shrinking` is red on 0.47). No Rust or Python dependency in our build.
- **C linking**: `-lhegel_c -lpthread -lm -ldl` (add `-lscotch -lscotcherr -lz -lrt` for Scotch tests)

## libhegel (the engine)

We sit on the official C ABI, libhegel, from the Hegel monorepo **hegeldev/hegel** (it absorbed hegel-rust on 2026-10-19; there libhegel is `libhegel/`, next to `frontends/rust` and `frontends/python`; the header is `libhegel/include/hegel.h`). Crate `hegeltest-c`, prebuilt `libhegel-<goos>-<goarch>.so` on GitHub releases (tagged `libhegel/<version>` since the move); a source build writes `target/release/libhegel_c.so` at the workspace root, which `make libhegel` installs as `build/libhegel.so`). Pinned at libhegel 0.48.0 = tag `libhegel/0.48.0` (= `frontends/rust/0.50.3`; the C crate has its own version line). We don't call 0.48's `hegel_set_unicode_version` (C has no host Unicode version), so generators use the engine's newest tables. Run loop, all context-first: `hegel_settings_new` → `hegel_run_start` → `hegel_next_test_case` (the engine resumes on the calling thread — no worker thread) → typed draws (`hegel_generate_integer` / `_float` / `_boolean` / `_bytes` / `_string` with a string-generator handle) and protocols (collection / recursion / pool / state machine) → `hegel_mark_complete(status, origin)` → `hegel_run_result` (status PASSED / FAILED / ERROR, error kind, failures with origin + blob + caveat).

Engine facts that shaped the runtime (verified against 0.47.1, still true on 0.48.0):
- `hegel_settings_new` resolves profiles + `hegel.toml` + `HEGEL_*` env vars (`HEGEL_SEED`, `HEGEL_TEST_CASES`, ...) — they apply to our runs for free. Verbosity enum: NORMAL=0, QUIET=1.
- The engine no longer produces a failure diagnostic: the caller captures output for test cases stamped by `hegel_test_case_should_capture` (the final replay, and first-check replays of each discovered failure), keyed by origin. Our runner stores the lines of the last stamped failing execution per origin and prints them with the report. "Draw N:" traces and notes are OUR output.
- Float draws: bounded draws never allow NaN; infinities only from an unbounded end. 32-bit bounds must be exactly representable as `float`.
- Text generators get `min_codepoint=1` so drawn strings never contain NUL (they're C strings); regex draws are full-match with a NUL-free alphabet.
- `u64` ranges above `INT64_MAX` go through `hegel_generate_integer_big` (9-byte two's-complement LE bounds).
- Draws can return `HEGEL_E_ASSUME` (engine-side rejection) → INVALID, and `HEGEL_E_STOP_TEST` → OVERRUN. Any other error is misuse (bad bounds, bad pattern) → the body aborts the run as a health failure with the engine's diagnostic.
- `origin` strings passed to `mark_complete` identify *which bug* a failure is — `HEGEL_ASSERT` passes its file:line so distinct asserts shrink as distinct bugs; messages embedding drawn values must never be origins.
- Engine health checks surface as a run ERROR of kind HEALTH_CHECK — printed with our `Health check failure:` prefix (same as `hegel_health_fail`).
- A test case may make 2^20 choices before overrunning (the old ~8 KB byte budget is gone) — "huge minimum input" health tests need >1M draws now.
- The engine state machine has no preconditions — rules reject via assume (`hegel_state_machine_rule_rejected`); invariants are `always_check` in our driver to keep "check after every rule".

## Fork mode architecture

In fork mode, the **parent** process owns the engine (in-process, via libhegel). For each test case:
1. Parent calls `hegel_next_test_case`, creates request/response pipes, then `fork()`s
2. Child runs the C test function; every engine call (`hegel_draw_*`, spans, collections, ...) writes a framed request to the pipe
3. Parent serves each request with `hegel__serve_op` — the same function nofork mode calls directly — and writes the response back
4. Child ends with a terminal message (DONE / STOPPED / ASSUME / FAIL / HEALTH); if it crashes instead (SIGSEGV, SIGABRT), parent catches it via `waitpid()` and marks the case INTERESTING so the engine shrinks it

The child never touches the engine — the parent proxies all draws. The message protocol (in `core/hegel_internal.h`) is deliberately transport-shaped: the same message set is meant to later run over a serial/TCP link to a remote target (embedded board), with the board taking the child's role and watchdog-reset taking fork's role as crash isolation.

In nofork mode the body runs in the parent itself and calls `hegel__serve_op` directly; failures longjmp back to the run loop. `HEGEL_ISOLATED` gives a block crash isolation there: the block forks, its child may only send notes and a verdict (mode ISO), and a crash becomes a failure whose origin names the block. In fork mode the block runs inline, after an `ISO_SITE` frame tells the parent which block a crash belongs to.

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

The one build-critical dependency, the **Hegel monorepo** (the engine source), is a pinned git submodule at **`deps/hegel`** — see Build and test. Everything else is reference material: `make inspiration` clones sister bindings and study repos into three categorized subdirs of `inspiration/`, and symlinks `deps/hegel` as `inspiration/hegel/hegel` and its Rust frontend (`frontends/rust`) as `inspiration/hegel/hegel-rust` (the path `RUST_SOURCE:` comments resolve against) for discoverability (the symlinks live in the gitignored `inspiration/`, so they are never committed).

Bug-hunt targets (Scotch, Linux) are not vendored either; they use **pins**. `tests/irl/<project>/project.conf` says where upstream lives and how to build it; `pins/<pin>.conf` names one bug's states:
- `PIN_FIX_SHA` (upstream fix: `buggy` = its parent must be RED, `fixed` must be GREEN)
- `PIN_BUGGY_REF` + `PIN_FIX_PATCH` (our own fix as a patch)
- `PIN_REF` (no known bug: `current`, expected green)
- optionally `PIN_UPSTREAM_REF` (upstream's own attempt, with its expectation).

`scripts/pin-worktree.sh <project> <pin> --state S [--asan]` materialises a gitignored worktree from a shared object store under `inspiration/targets/`. For Linux that store is a partial clone: sparse, shallow, ~15 s per new commit. `scripts/pin-run.sh` judges the run. Use `make test-pin PIN=x STATE=s` / `test-pins` / `fetch-pins` in `tests/irl/scotch` or `tests/irl/linux`. Linux harnesses copy the kernel files they compile into `*_pinned.c` / `pinned-include/` at build time (`tests/irl/linux/pin.mk`). The pin is committed; the GPL source never is. Pin files say what the bug is, so they're hidden from blind bug hunters.

The `fixed` state is what keeps rediscoveries honest: a test that is also red on the fixed commit found an artifact, not the bug.

**`deps/hegel`** (submodule, hegeldev/hegel) — home of the native engine, the `hegeltest-c` crate (`libhegel/`) whose cdylib we sit on, and the Rust and Python frontends (`frontends/`). `libhegel/include/hegel.h` is the ABI contract; `libhegel/examples/*.c` are canonical usage; `frontends/rust/tests/common/utils.rs` has the `find_any`/`minimal()` test helpers our ported tests mirror.

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

**`inspiration/kernel-review/`** — complementary (non-PBT) bug-finding systems aimed at our targets:
- **sashiko** (Linux Foundation, Apache-2.0): agentic static LLM review of Linux kernel patches — 11-stage pipeline (7 specialist lenses + dedup/verify/report). The methodological complement of `hegel-bughunt`: same domain, static vs. dynamic. Unlike the other inspiration clones, it is **pinned** (`SASHIKO_PIN` in the Makefile), because `docs/sashiko-gap-analysis.md` maps its stage prompts (`src/worker/prompts.rs`) to what PBT can/can't reach and must stay in sync — `make check-sashiko` reports pin drift.

## Important notes

- Generator combinators take ownership of sub-generators — don't free sub-generators after passing them to a combinator
- The `from-hegel-rust` test suite uses `RUST_SOURCE:` comments to map each C test to its Rust original. `make verify` uses Claude (opus) to semantically compare them.
- The `from-hegel-rust` integer/float tests use `hegel_run_test_result_n(..., 5000)` for `find_any` edge cases — upstream's `find_any` raised `max_attempts` from 1000 to 5000 when the 0.17.3 distribution fixes capped the interesting-constants pool (boundary values are rarer per draw now).
