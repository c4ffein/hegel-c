# CLAUDE.md

## Project overview

hegel-c is a C binding for Hegel, a property-based testing framework. It is pure C end to end: tests include the headers and link a static `libhegel_c.a`; at runtime the library dlopens `libhegel.so` — Hegel's official native engine (a Rust cdylib from the hegel-rust repo, prebuilt per platform on its GitHub releases). There is no server process, no Python, and no Rust in our build. (History: until 2026-06 this binding was a Rust FFI bridge to the Python `hegel-core` server; upstream 0.17.0 deleted the server and shipped the in-process engine + C ABI we now sit on.)

**One public API:** `hegel_c.h` — primitive draws (and `HEGEL_DRAW_*` shorthands), spans, engine collections / recursion / pools, `HEGEL_ISOLATED`, asserts, runners. Thin over libhegel's own protocols; the transport boundary. Generators are plain C functions over it — see "Writing generators" below.

Until 2026-10-30 there was a second layer, `hegel_gen.h`: a declarative schema system (describe a C struct, get generation, allocation and cleanup). It moved to `experiments/schema-with-let/`, with a view-callbacks extension in `experiments/schema-with-callbacks/`; `experiments/README.md` has what was measured and why. Don't reintroduce it on main without revisiting that README.

**Status**: Work in progress. Author: c4ffein. License: MIT.

## Architecture

- `hegel_c.h` — Public C API (opaque types, draw functions, spans, collections, recursion, pools, `HEGEL_ISOLATED`, assertions, suite API, stateful)
- `experiments/` — designs built, measured and not kept, each still buildable against main's library (`make -C experiments/<name> test PREBUILT=1`), never part of `make test`: `schema-with-let/` (the retired schema layer with its tests and docs), `schema-with-callbacks/` (the view-callbacks PoC), `TODO-schema.md` (the schema backlog), `README.md` (what was measured, why main is imperative, ideas not pursued).
- `core/` — The pure-C runtime under `hegel_c.h`:
  - `hegel_engine.{c,h}` — dlopen binding to `libhegel.so` (search order: `$HEGEL_LIBHEGEL_PATH`, `./build/libhegel.so`, `./.hegel/libhegel/libhegel.so`, system). Mirrors upstream's types with an `hg_` prefix; dlsym keeps our same-named public functions (`hegel_start_span`, `hegel_note`, `hegel_collection_more`, ...) from colliding with libhegel's exports. One process-wide `hegel_context_t`.
  - `hegel_internal.h` — the wire protocol (every engine operation a body can perform is one request op) and the test-case struct.
  - `hegel_server.c` — `hegel__serve_op`: the ONLY code that calls the engine on a body's behalf. Per-case handle table (collections/recursions/pools/state machines, auto-freed at case end), captured output lines, run-scoped caches: string generators (text, regex, email, url, domain) and compiled grammars (by spec, so in fork mode the parent compiles each once per run).
  - `hegel_runtime.c` — the public primitives as requests; `hegel__call` picks the transport (INPROC = direct call into the server, CHILD = fork parent's pipe). Also `HEGEL_ISOLATED`.
  - `hegel_runner.c` — run drivers (fork + nofork), the fork parent's serve loop, capture-by-origin failure reporting, suite API.
  - `hegel_stateful.c` — stateful driver on the engine's state machine (`hegel_new_state_machine`), shaped like hegel-rust's `run_sequential`; preconditions map onto `rule_rejected`. Works in fork and nofork mode.
  - `hegel_alloc.c` — generator helpers that live in the body's process, never on the wire: `hegel_alloc` / `_realloc` / `_free` (per-case blocks linked into the test case with serial numbers; the case drivers release them after the cleanup hook) and `hegel_draw_recursive` / `hegel_recursive_child` (the recursion protocol around a node function; an over-budget attempt longjmps back and its blocks are released by serial).
- `tests/selftest/` — 58 self-tests (PASS/FAIL/CRASH/HEALTH-CHECK/SHRINK): draws, value draws, grammars, spans, shrinking, stateful, isolation, engine protocols (collections, recursion, repeat loops), events, settings and profiles, generator helpers
- `tests/from-hegel-rust/` — 11 tests ported from hegel-rust (5 PASS, 6 SHRINK)
- `tests/mpi/` — 3 MPI tests (mpiexec + MPI_Comm_spawn patterns)
- `tests/irl/scotch/` — 3 Scotch integration tests (sequential, the `hgraphOrderCp` reducer demo, PT-Scotch MPI), a 29-test property suite (`props/`: 24 green on current master via `make test-props` / pin `current`, 5 witnesses of open bugs run by their pins) + bug pins (`pins/`; `PIN_ASAN=1` in a pin forces an ASan Scotch)
- `tests/irl/linux/` — Linux kernel bug-hunt harnesses, one per target, each built against a pinned kernel checkout (`pins/`)
- `docs/shrinking.md` — integrated shrinking, with the Scotch reducer walkthrough
- `docs/mpi-testing.md` — MPI integration guide
- `docs/upstream/` — drafts of requests to upstream (hegeldev/hegel), never posted automatically: `collection-average-size.md` (a feature request; engine bugs are tracked in c4ffein/bugs)
- `TODO.md` — deferred items
- **Bugs live in c4ffein/bugs** (private, the single tracker since 2026-11-07): every bug hegel-c finds in Linux, Scotch or libhegel, with its write-up, submissions (patches as sent), witnesses and upstream state. hegel-c keeps what makes hegel-c better — pins, the rediscovery corpus, the run log — and names a bug there as `bugs:<uuid>` (the uuid never changes; the slug in a comment next to it is for reading). Until 2026-11-07 that was `docs/findings.toml`, `docs/findings/`, `submissions/` and a findings page here; they moved (bugs commit `25ab604`).
- `docs/runs.toml` + `docs/runs/<run-id>/` — every run that pointed hegel-c at bugs (blind-recall, harness, pins, hunt), with its setup (hegel-c commit, engine, model, blind or leaked) and a verdict per target (a corpus `Fixed-by`, a `bugs:<uuid>` or a pin); the folder keeps the report and the briefs needed to re-run it. `make runs` prints targets × runs and flags regressions (a hit in one run, not in a later run of the same kind); `make lint-runs` (`tools/runs.py`, Python 3.11+, no deps; also a CI job) checks it, and every folder in `docs/runs/` must belong to a run. Names bugs: never in a blind sandbox.
- `docs/corpus.toml` — the rediscovery targets runs are scored on: upstream-fixed bugs keyed by fix SHA (mechanism, harness plan, property to check, pin) and our own finds by `bug = "bugs:<uuid>"`; merged with sashiko's corpus by `tools/merge-corpus.py` (hunters only ever get SHA, subsystem, tier, harness_dir, status). See `docs/rediscovery-corpus.md`. `lint-runs` checks it: statuses, pins naming the same fix, bug references well-formed (and existing, when a checkout of c4ffein/bugs is around).

Two execution modes:
- **Fork mode** (default): each test case runs in a forked child; parent serves draw requests via pipe IPC. Crash-safe. Two flavours: *fast* (default, plain fork — the child inherits the parent's heap, so a bug that reads uninitialized memory can behave differently on replay: the shrink stalls at a larger input than it should and the report carries a `nondeterministic failure` caveat; rerun such a test in clean mode, and make it fork-clean in its own `main()` with `hegel_set_fork_mode`, so CI, pins and manual runs all get it — measured example in `docs/shrinking.md`, Step 7) and *clean* (`hegel_set_fork_mode(HEGEL_FORK_CLEAN)` or `HEGEL_C_FORK=clean`: fork + re-exec the test binary with ASLR off, so every child starts from the same heap; ~2.5x slower per case). Tests that key files by PID can't run in clean mode.
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
make check-libhegel-upstream    # only: is there a libhegel release newer than LIBHEGEL_VERSION? (CI release watch)
make check-libhegel-coverage    # every libhegel function bound or classified (tools/libhegel-coverage.toml)

make selftest-test              # 58 tests (PASS/FAIL/CRASH/HEALTH-CHECK/SHRINK)
make from-hegel-rust-test       # 11 tests (5 PASS, 6 SHRINK)
make mpi-test                   # 3 tests (needs mpicc)
make scotch-test                # Scotch tests (needs Scotch — clone via make inspiration)

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
- `hegel_run_test_with(fn, &opts)` / `hegel_run_test_nofork_with` — `hegel_run_options`: `test_cases`, `has_seed`+`seed`, `suppress_health` (`HEGEL_SUPPRESS_*` bits), `skip_phases` (`HEGEL_SKIP_*` bits), `report_multiple` (report every distinct failure, each with its own reproduce line; `HEGEL_REPORT_MULTIPLE_FAILURES=1` does the same, see `tests/selftest/test_report_multiple.c`); zero fields keep the engine default. Returns 0/1. See `tests/selftest/test_run_options.c`.
- Engine settings, as more `hegel_run_options` fields (zero keeps what the profile and environment resolved): tri-state `HEGEL_ON` / `HEGEL_OFF` for `derandomize`, `stop_on_first_failure` (OFF spends the whole budget), `show_statistics` (the engine's statistics block on stderr; also `HEGEL_STATISTICS=1`), `print_blob` (OFF drops the reproduce line) and `unbounded_choices`; `backend` (`HEGEL_BACKEND_*`), `nondeterminism` (`HEGEL_NONDETERMINISM_QUIET/WARN/ERROR`); `settings` (a `"key=value ..."` string in hegel.toml's vocabulary); `profile` (`base`, `development`, `ci`, `workload`, a hegel.toml section or a registered one); `test_file` / `test_line` / `test_function` (`HEGEL_TEST_LOCATION(fn)`; the engine uses them only inside Antithesis, where every run is now reported). `hegel_register_profile(name, &opts)` snapshots a profile, `hegel_set_default_profile(name)` picks the default (NULL clears). Precedence, lowest first: profile < `HEGEL_*` environment < `settings` string < typed fields < what the runner owns (verbosity, database, report_multiple, test location). See `tests/selftest/test_settings.c`, `test_profiles.c`.
- Replay: every failure report ends with `reproduce: HEGEL_C_REPRODUCE='N:<blob>'` — N is the run's number within the binary (1-based, in call order). Rerunning the binary with that variable replays exactly that failure through `hegel_run_start_blob` (other runs are skipped); with the bug fixed it prints "did not reproduce" and passes. See `tests/selftest/test_reproduce_blob.c`.
- Example database: off unless `HEGEL_DATABASE=<dir>` is set; then failures are stored per `<binary basename>#<run number>` and replayed first by the next invocation.

**Primitive draws:** `hegel_draw_int`, `_i64`, `_u64`, `_usize`, `_float`, `_double`, `_bool`, `_text`, `_bytes`, `_regex` (full match)

**Value draws:** `hegel_draw_date(tc, &min, &max)` / `_time` / `_datetime` return `hegel_date` / `hegel_time` / `hegel_datetime` structs (NULL bounds = 0001-01-01..9999-12-31 and the whole day; a date shrinks toward 2000-01-01 by distance, a time toward its minimum); `hegel_draw_uuid(tc, version)` (0 = any but nil, 1..15 forces the version nibble and the RFC 4122 variant); `hegel_draw_ipv4` / `_ipv6` (byte structs, network order); `hegel_draw_email` / `_url` / `_domain(tc, max_length /*0 = 255*/, ...)` with `hegel_draw_text`'s buffer contract (ASCII, never NUL; URLs have no length bound, so use a 4 KB buffer). Bounds that aren't real dates or times, min > max, a uuid version > 15 or a domain length outside 4..255 are health failures. See `tests/selftest/test_value_draws_*.c`.

**Grammars:** `hegel_draw_grammar(tc, lark_text, start)` returns a string the Lark grammar accepts (`start` NULL = rule `start`), owned by the case (`hegel_alloc`), NUL-free, never truncated; it shrinks through its derivation. `hegel_draw_grammar_with(tc, text, &opts)` takes `hegel_grammar_options`: `start`, `max_size` (characters, 0 = 8192), `max_depth` (0 = 200), `import_dir` (`%import a.b.X` reads `<dir>/a/b.lark`; Lark's bundled grammars otherwise), and explicit terminals (`explicit_names` / `n_explicit` + `fill(tc, i, ctx)`, which draws the value with ordinary draws and returns it; required for `%declare`d terminals, and it can override a defined one). A rejected grammar is a health failure with the engine's message. See `tests/selftest/test_grammar_*.c`.

**Draw shorthands:** `HEGEL_DRAW_INT(lo, hi)` / `HEGEL_DRAW_INT()` (full range), same for `_I64` / `_U64` / `_FLOAT` / `_DOUBLE`, and `HEGEL_DRAW_BOOL()` — the primitives for a test case named `tc` in scope; same choices, so they shrink the same.

**Spans:** `hegel_start_span(tc, label)`, `hegel_stop_span(tc, discard)` — structural shrinking hints; derive labels with `hegel_label("lib.thing")` (FNV-1a, as libhegel's `hegel_label_from_name`).

**Engine protocols** (handles are `{ int64_t id; }` newtypes, ids into the server's per-case table — so they work across the fork pipe):
- Collections: `hegel_collection_new(tc, min, max)` / `_more` / `_reject` / `_free` — engine owns the length; shrinks by element deletion.
- Recursion: `hegel_recursion_new(tc, max_depth, max_leaves)` / `_branch` / `_leaf` / `_retry` / `_finish` / `_free` — engine owns branch probabilities and budgets (see `tests/selftest/test_recursion_tree.c`). Prefer the helper below.
- Repeat loops: `hegel_repeat_new(tc)` / `_more` / `_failed` / `_free` — the engine owns the iteration count, and a failing loop shrinks to the fewest iterations. `HEGEL_REPEAT(tc) { ... }` is the loop statement (an assume discards the case; no `break` / `return` out of it); `hegel_repeat_run(tc, fn, ctx)` the function form (an assume ends only that iteration, as hegel-rust's `tc.repeat`). A case failing mid-iteration (assert, `hegel_fail`, crash, either mode) gets its `hegel_repeat_failed` call from the server, innermost loop first. A passing loop runs long (often until the 2^20-choice overrun, every iteration a pipe round trip in fork mode), so run repeat properties nofork, or cap a raw loop and `hegel_repeat_free` it. See `tests/selftest/test_repeat_basic.c`, `test_repeat_shrinks.c`.
- Pools: `hegel_pool_new` / `_add` / `_draw(consume)` / `_restore` / `_free` — ids are opaque int64s chosen by the engine (map them, don't index with them).

**Generator helpers** (run in the body's process; nothing crosses the wire):
- `hegel_alloc(tc, size)` (zeroed, max-aligned) / `hegel_realloc(tc, p, size)` (growth zeroed) / `hegel_free(tc, p)` — memory owned by the test case, freed when it ends however it ends (return, assert, assume, overrun, health failure; fork and nofork; a stateful rule's blocks last until the case ends, after teardown). Under nofork, a `HEGEL_ISOLATED` block's own blocks vanish with its child; don't `hegel_free` an earlier block inside one. See `tests/selftest/test_alloc_lifetime.c`.
- `hegel_draw_recursive(tc, max_depth, max_leaves, node_fn, ctx)` + `hegel_recursive_child(tc)` — the recursion protocol run for you. `void *node_fn(tc, bool leaf, ctx)` builds ONE node with `hegel_alloc`; when `leaf` is false it calls `hegel_recursive_child` once per child slot. An over-budget attempt longjmps back to the top, its `hegel_alloc` blocks are freed and the engine closes its spans; each sub-value gets its own span. Rules: nodes from `hegel_alloc` only, hold nothing else across a child call, no child call from a leaf or outside a node function (health failures). Defaults `HEGEL_RECURSIVE_MAX_DEPTH` (50) / `_MAX_LEAVES` (100): ~80 nodes on average for a binary tree, same distribution as the raw protocol. See `tests/selftest/test_recursive_tree.c`, `test_recursive_shrinks.c`.

**Crash isolation for a block:** `HEGEL_ISOLATED(tc) { ... }` — no draws inside (health failure), no nesting, no break/return out of it. Every failure inside a block (crash, `HEGEL_ASSERT`, `hegel_fail`) shares one origin, `HEGEL_ISOLATED at <file:line>`, so a bug that sometimes asserts and sometimes segfaults shrinks as one bug. If a plain test shrinks poorly, wrapping the code under test in a block is the fix. `HEGEL_C_MERGE_ORIGINS=1` merges every failure in a run into one origin.

**Other:** `hegel_note(tc, msg)` (captured for the counterexample report), `hegel_event(tc, label)` / `hegel_event_value(tc, label, value)` (observations for the engine's end-of-run statistics, like Hypothesis's `event()`: printed on stderr with `show_statistics` or `HEGEL_STATISTICS=1`; `event` gives the share of cases with the label, `event_value` count/min/median/mean/p90/max of the values, which must be finite; only generation-phase cases that completed count; they never steer generation or shrinking; see `tests/selftest/test_event_stats.c`), `hegel_label_combine(labels, n)` (pure C, bit for bit libhegel's), `hegel_target(tc, score, label)` (the TARGET phase hill-climbs toward higher scores; needs a budget well above the default 100 cases — see `tests/selftest/test_target.c`), `hegel_assume(tc, cond)`, `hegel_fail(msg)` (code-under-test broken), `hegel_health_fail(msg)` (test-setup broken — emits "Health check failure:" prefix), `HEGEL_ASSERT(cond, fmt, ...)`

## Writing generators

A generator is a C function that fills a value from draws: `void gen_graph (hegel_testcase *tc, Graph *g)`. Conventions the existing harnesses follow:

- **Dependencies are variables.** Draw what a field depends on first and use it as a bound (`u = hegel_draw_int (tc, 0, g->nvert - 1)`) instead of drawing wide and clamping after (`% nvert` costs shrink quality).
- **Case memory, no cleanup chains.** Allocate what the generator builds with `hegel_alloc (tc, size)` (zeroed); the case frees it on every exit path, so an assert or assume mid-generation leaks nothing in nofork mode. Keep `malloc` for memory the code under test will `free` itself.
- **Spans for structure.** Wrap a list in `HEGEL_SPAN_LIST` and each element in `HEGEL_SPAN_LIST_ELEMENT`; a struct or node in its own span (`hegel_label ("lib.thing")`). `hegel_stop_span (tc, 1)` discards a dead end. Measured on 0.48.0 (`tests/selftest/test_span_list_shrinks.c`): a list failing on one element shrinks to that one element with or without spans; spans changed only the shrinker's work — ~40% fewer executions with variable-width elements, ~2.7x more with a nested list per element. A hint, not what makes lists shrink.
- **Lengths:** a drawn length (`n = hegel_draw_int (tc, 0, MAX)`) gives sizes across the range; the collection protocol (`hegel_collection_*`) lets the engine own the length and reject elements (`_reject`, e.g. duplicates) but aims for ~5 elements whatever the max — use it when small lists are what you want.
- **Recursive values** go through `hegel_draw_recursive`: a node function that builds one node from `hegel_alloc` and calls `hegel_recursive_child` per child; the helper runs branch / leaf / retry / finish and frees abandoned attempts. The raw protocol, with its hand-written unwinding, is in `tests/selftest/test_recursion_tree.c` and `test_recursion_nary_tree.c`.
- **Generate first, then isolate:** draws happen before `HEGEL_ISOLATED (tc) { ... }`, never inside.
- **Notes for crash-prone code:** `hegel_note` the input's shape before calling the code under test, so a crash still reports it.

Real examples: `tests/irl/scotch/test_graph_order_shrink.c` (`gen_graph`, walked through in `docs/shrinking.md`), `tests/irl/scotch/props/graph_gen.h` (topology generators building CSR arrays).

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

Third-party excerpts (Linux kernel code under `tests/irl/`) keep their upstream SPDX license and get a modification notice plus `SPDX-FileCopyrightText` lines right after it — never relabel them MIT, and never let them into `libhegel_c.a`. Licensing follows REUSE: texts in `LICENSES/`, header-less files covered by `REUSE.toml`, full check with `make check-reuse`, also a CI job (note: it over-reports ignored build artifacts inside still-untracked directories — a git `ls-files --directory` quirk).

## Key dependencies

- **Runtime**: `libhegel.so` ≥ 0.50.0 (Hegel's native engine, loaded via dlopen). 0.50.0 changed the state-machine ABI (create, declare each rule and invariant, then `hegel_state_machine_start`), which `core/hegel_server.c` uses, so an older engine fails to load with a "missing symbol" message. (Before that: ≥ 0.48 was needed for correct shrinking when a second failure turns up mid-shrink, `test_shrink_failure_found_while_shrinking`.) No Rust or Python dependency in our build.
- **C linking**: `-lhegel_c -lpthread -lm -ldl` (add `-lscotch -lscotcherr -lz -lrt` for Scotch tests)

## libhegel (the engine)

We sit on the official C ABI, libhegel, from the Hegel monorepo **hegeldev/hegel** (it absorbed hegel-rust on 2026-10-19; there libhegel is `libhegel/`, next to `frontends/rust` and `frontends/python`; the header is `libhegel/include/hegel.h`). Crate `hegeltest-c`, prebuilt `libhegel-<goos>-<goarch>.so` on GitHub releases (tagged `libhegel/<version>` since the move); a source build writes `target/release/libhegel_c.so` at the workspace root, which `make libhegel` installs as `build/libhegel.so`). Pinned at libhegel 0.50.0 = tag `libhegel/0.50.0` (the C crate has its own version line; 0.48.0 was `frontends/rust/0.50.3`). A scheduled workflow (`.github/workflows/libhegel-release-watch.yml`, every 6 h and on Makefile changes) runs `make check-libhegel-upstream` and goes red while a newer `libhegel/X.Y.Z` tag exists (pre-release / suffixed tags ignored; a network failure prints "COULD NOT CHECK", never an update), and also while `deps/hegel` as committed is not the commit of tag `libhegel/<LIBHEGEL_VERSION>` ("SUBMODULE NOT AT PIN": CI builds the engine from the submodule; check out the tag, never `git submodule update --remote`). Bumping: read `libhegel/CHANGELOG.md`, check out `libhegel/<new>` in `deps/hegel`, run `make check-libhegel-coverage LIBHEGEL_VERSION=<new>` (fails on a function the new header declares that `core/hegel_engine.c` doesn't bind and `tools/libhegel-coverage.toml` doesn't list as deliberate or a gap, and on bound/listed functions it dropped: classify or bind each), set `LIBHEGEL_VERSION` + both `LIBHEGEL_SHA256_*` (the release's `.sha256` assets), `make libhegel` (and `PREBUILT=1`), `make check-libhegel`, rerun the suites, update the version here. We don't call 0.48's `hegel_set_unicode_version` (C has no host Unicode version), so generators use the engine's newest tables. Run loop, all context-first: `hegel_settings_new` → `hegel_run_start` → `hegel_next_test_case` (the engine resumes on the calling thread — no worker thread) → typed draws (`hegel_generate_integer` / `_float` / `_boolean` / `_bytes` / `_string` with a string-generator handle) and protocols (collection / recursion / pool / state machine) → `hegel_mark_complete(status, origin)` → `hegel_run_result` (status PASSED / FAILED / ERROR, error kind, failures with origin + blob + caveat).

Engine facts that shaped the runtime (verified against 0.47.1, still true on 0.48.0; on 0.50.0 the suites, both experiments and every Scotch and Linux pin were re-run, 2026-11-06, runs `2026-11-06-*-pins-libhegel-0-50`):
- `hegel_settings_new` resolves profiles + `hegel.toml` + `HEGEL_*` env vars (`HEGEL_SEED`, `HEGEL_TEST_CASES`, ...) — they apply to our runs, except what the runner sets after: verbosity (QUIET, or NORMAL when show_statistics or nondeterminism WARN resolved on; engine output goes to stderr through the output callback), the test location, report-multiple-failures (off, unless `report_multiple` or `HEGEL_REPORT_MULTIPLE_FAILURES` asks for it), the database (off unless `HEGEL_DATABASE` names a dir, keyed per run), an explicit `_n` count, and any non-zero `hegel_run_options` field. Verbosity enum: NORMAL=0, QUIET=1.
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

- Transforming a drawn value (map), rejecting it with `hegel_assume` (filter) or drawing based on it (flat_map) does NOT degrade shrinking quality
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
- `PIN_REF` (no known bug: `current`, expected green; or a bug with no fix yet: `PIN_EXPECT=red`)
- optionally `PIN_UPSTREAM_REF` (upstream's own attempt, with its expectation).
- `PIN_BUG="bugs:<uuid>"` when the pin witnesses one of our own finds: the bug in c4ffein/bugs (`make lint-runs` checks the form).

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

- **Kernel trees: never clone the object store** (`inspiration/targets/linux-pins.git`). Use `scripts/pin-worktree.sh` or `git -C <store> worktree add` with a sparse checkout. A `git clone --filter` of the store (2026-10-23) bypassed the local fast path, ran pack-objects on the shallow partial store and took the machine down.
- **Cap heavy commands** with a timeout and a memory limit, but not `ulimit -v` for ASan builds: ASan reserves terabytes of shadow address space and aborts at startup under it, which looks exactly like a red test. Use `systemd-run --user --scope -p MemoryMax=2G …`, or only a timeout.
- **Skills are hunter-visible.** Blind bug hunts read `.claude/skills/`, so skill text and examples must never describe a bug in `docs/corpus.toml` or in c4ffein/bugs; keep examples generic, or the next blind run of that target is void.
- The `from-hegel-rust` test suite uses `RUST_SOURCE:` comments to map each C test to its Rust original. `make verify` uses Claude (opus) to semantically compare them.
- The `from-hegel-rust` integer/float tests use `hegel_run_test_result_n(..., 5000)` for `find_any` edge cases — upstream's `find_any` raised `max_attempts` from 1000 to 5000 when the 0.17.3 distribution fixes capped the interesting-constants pool (boundary values are rarer per draw now).
