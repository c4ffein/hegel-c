---
name: hegel-bughunt
description: >
  Run a property-based-testing audit of a C library to find bugs. Use this skill
  when the user asks to "audit", "bug-hunt", "PBT-sweep", "find bugs in", "build a
  comprehensive test suite for", or "do a security/correctness audit of" a C
  library or project — especially one whose code they don't own. Triggers on:
  "find bugs in X", "audit Y", "comprehensive PBT for Z", "test all the public
  API of W", "exercise the configuration space", "stress-test", "fuzz",
  "regression-hunt". Also load this skill when the user mentions hegel-c
  alongside any of those framings, or when they explicitly want to *find* bugs
  rather than *prevent* them in code they're writing.
---

# Hegel Bug Hunt

This skill is for **PBT-driven library auditing**. The goal is to find real bugs in a target library by building a comprehensive property-based test suite that broadly exercises the public API. The output is a working test suite **plus a report**.

It complements the `hegel` skill, which teaches "add a property test to existing code." The two skills are different in stance and output:

| | `hegel` | `hegel-bughunt` |
|---|---|---|
| Audience | Developer adding tests to their own code | Auditor exercising someone else's code |
| Output per session | A few tests, added to existing files | A test suite scaffold + a report |
| Mindset | "Express the contract this code already satisfies" | "Probe broadly, watch what falls out" |
| Coverage | Per-function depth | Cross-function breadth |
| Bugs found | Ones you suspected | Ones nobody knew existed |

> **You should still consult the `hegel` skill** for the API surface — generators, draws, runners, test structure. This skill is **methodology**, not API. The `hegel` skill answers "how do I write a hegel-c test?"; this skill answers "what tests should I write to find bugs?"

## When this skill applies

Use `hegel-bughunt` when:

- The user asks to **find bugs** rather than prevent them.
- The target is a library, parser, codec, allocator, kernel, or other component with a wide public API surface.
- The user wants **broad coverage** ("test everything", "exercise the whole API", "audit").
- The user is willing to invest in a **session-spanning effort** — this is not a 30-minute task.
- There's no existing comprehensive PBT suite for the target. (If there is, use the `hegel` skill to add a test, not this one to rebuild.)

Do **not** use it for:

- Adding a property test to a single function (use `hegel`).
- Porting an existing PBT suite (use `hegel` + porting docs).
- Verifying code under active development (the developer should write `hegel` tests as they go).

## The core stance

**You are a researcher, not a developer.** Three implications:

1. **You expect to be surprised.** Real bugs come from broad probing of code paths the original author didn't think to defend. If you only test what you "know" should hold, you'll only confirm what's already known.
2. **You log everything.** Bugs are valuable, but so are non-bug surprises ("this function is slower than documented", "this flag has no observable effect", "this combination produces non-deterministic output"). Build an inventory.
3. **You iterate across sessions.** A comprehensive audit isn't a one-shot. Session 1 lays scaffolding; sessions 2+ deepen and react to findings.

## Workflow

### Phase 1 — Setup (do once per project)

#### 1a. Build the target locally

You need a working build of the target library before anything else. Most libraries ship with a `make` / `cmake` / `configure` flow. If the library has external dependencies (sparse, dense linear algebra, compression, MPI, etc.), get them from system packages where possible — building everything from source kills the audit before it starts.

If you can't get the library building in 30 minutes, stop and ask the user. Don't try to patch their build system unless they request it.

#### 1b. Audit the public API surface

Read every public header. List every function. Group them by category:

- **Construction / destruction** (init, build, free)
- **Mutation** (set, insert, delete, configure)
- **Query** (get, check, count, traverse)
- **Algorithm** (the substantive operations the library exists to perform)
- **I/O** (save, load, format, parse)
- **Strategy / configuration** (anything that takes a flag set, mode enum, or string-encoded configuration)

The **strategy / configuration** category is special. **Bugs hide in configuration combinations.** If the library exposes a strategy parser, a flag enum, or a "mode" parameter, that surface needs separate fuzzing — see Phase 2.

Output of this phase: a flat list, e.g.,

```
SCOTCH_graphInit            (construction)
SCOTCH_graphBuild           (construction)
SCOTCH_graphCheck           (query)
SCOTCH_graphPart            (algorithm)
SCOTCH_graphOrder           (algorithm)
SCOTCH_graphColor           (algorithm)
SCOTCH_stratGraphPartBuild  (strategy)
SCOTCH_stratGraphOrderBuild (strategy)
...
```

Every name on this list is a candidate test target. **Don't skip the strategy/configuration calls.** Plan to test the algorithm functions both with default strategies (`SCOTCH_stratInit` + nothing) and with constructed strategies covering the flag space.

#### 1c. Build shared input infrastructure

Before writing tests, write a header (or two) of **input generators** for the data types this library consumes. Examples:

- For graph libraries: `random_sparse`, `2d_grid`, `path`, `complete_K_n`, `star`, `disjoint_union_of_K_2_pairs` (multiple components), `single_isolated_vertex`, `all_isolated_vertices`.
- For tree libraries: `balanced`, `degenerate_left_spine`, `degenerate_right_spine`, `random`, `single_node`, `empty`.
- For parser libraries: `well_formed`, `mostly_well_formed_with_one_corruption`, `random_bytes`, `pathological_inputs_from_OWASP`.
- For arena allocators: `random_alloc_free_sequence`, `alloc_only_until_exhaustion`, `alloc_free_alloc_pattern`.

Write each generator to take a `hegel_testcase *tc` and any size/shape parameters. Return a constructed value (or fill caller-provided buffers). Test the generators themselves against the library's own validity check (`SCOTCH_graphCheck`, `parser_validate`, etc.) — if your generators produce inputs the library rejects, your test budget is wasted on uninteresting cases.

This file ends up looking like `graph_gen.h` / `tree_gen.h` / `parser_gen.h` and is shared across all your test files. **Resist the urge to inline generation per test.** That's the road to 67 divergent reimplementations of "build a graph."

#### 1d. Build a configuration / strategy fuzzer

If the target has a strategy parser, configuration string, or flag set, write a generator that produces **random valid configurations**. For Scotch this is `stratGenMap` / `stratGenOrder`-style strings; for OpenSSL it would be cipher suite strings; for SQLite it would be PRAGMA + COMPILE_OPTIONS combinations.

The configuration fuzzer should:

- Cover every documented flag at least once across the random space.
- Not reject overly aggressively (you want some unusual combinations to slip through — that's where bugs hide).
- Fall back to a known-good default if the parser rejects the random configuration (skip via `hegel_assume`, log the rejection rate).

Do this **before** writing algorithm tests. Every algorithm test then runs under multiple drawn strategies, multiplying coverage of the configuration space without writing a separate test per flag.

#### 1e. Set up the runner

Write a `Makefile` (or equivalent) that:

- Builds every test in your suite.
- Has a `test` target that runs each binary once with the default 100 cases.
- Has a `test-loop` target that runs the suite repeatedly until failure or timeout. For overnight bug hunts, this is what actually finds rare bugs.
- Captures core dumps. Set `ulimit -c unlimited` (or a finite size) inside the test target so SIGSEGVs leave artifacts.
- Has a `clean-cores` target to delete dump artifacts between runs.

Concrete pattern:

```make
test:
	@for t in $(TESTS); do \
		echo "==== $$t ===="; \
		ulimit -c unlimited; ./$$t || exit 1; \
	done

test-loop:
	@while $(MAKE) test; do \
		echo "loop $$(date +%s) — passed, restarting"; \
	done; \
	echo "loop failed — see core dumps"
```

For overnight: `timeout 5h make test-loop`. Expect dozens to thousands of full-suite iterations.

### Phase 2 — Coverage build (session 1 + N)

#### 2a. One test per public function (minimum)

For each function on your audit list, write at least one PBT. Pick the most basic invariant: output range, validity, no-crash-on-valid-input. This is the **breadth pass**. Don't go deep on any one function in this pass — the goal is to have one test for every public function within the first session.

If you finish breadth, deepen by adding a second property per function — roundtrip, idempotence, equivalence to a reference, etc. (See `hegel/SKILL.md` § Property Catalogue for the taxonomy.)

#### 2b. Multiple input topologies per test

A single test with one input shape misses bugs that hide in topology. For graph code, the same property test should run on:

- **Random sparse graphs** (default; finds the obvious cases).
- **2D grids** (regular structure; exercises connectivity-aware code paths).
- **Complete graphs `K_n`** (dense; stresses memory and worst-case complexity).
- **Path graphs** (degenerate; stresses 1D-tree code paths).
- **Disconnected unions** (multiple components — many libraries have separate code paths for connected vs. disconnected input).
- **Edge: empty graph, single vertex, all isolated vertices**.

Don't write 6 separate tests. Write one test that draws a **topology selector** as its first call, dispatches to the matching generator, then runs the property:

```c
enum { TOP_RANDOM, TOP_GRID, TOP_COMPLETE, TOP_PATH, TOP_DISCONNECTED, TOP_END };

static void test_partition_in_range(hegel_testcase *tc) {
    int top = HEGEL_DRAW_INT(0, TOP_END - 1);
    Graph *g = NULL;
    switch (top) {
    case TOP_RANDOM:       g = gen_random_sparse(tc, n_max);     break;
    case TOP_GRID:         g = gen_2d_grid(tc, side_max);        break;
    case TOP_COMPLETE:     g = gen_complete(tc, n_max);          break;
    case TOP_PATH:         g = gen_path(tc, n_max);              break;
    case TOP_DISCONNECTED: g = gen_disjoint_components(tc, ...); break;
    }
    /* ... run property, free g ... */
}
```

Hegel's shrinker will find which topology is the minimal failing case. You don't need to enumerate.

#### 2c. Stress tiers

Each test should run at three scales, ideally as separate binaries to keep run times manageable:

- **Small (n=3–30)**: fast iteration, edge-case focus, default 100 cases.
- **Medium (n=100–1000)**: typical use, 1000 cases via `_n`.
- **Large (n=10K–100K+)**: scaling bugs, 10–50 cases (each is slow), often as a separate `bench_test_*` binary.

Bugs that need disconnected components or unusual configurations **fire most reliably on small inputs** because edge cases are over-represented. Bugs in scaling, allocation, integer overflow fire on large inputs. Cover both.

#### 2d. Strategy-flag sweep

For libraries with configuration: every algorithm test should pull a strategy from your config fuzzer (Phase 1d). Combined with multiple topologies, this puts each algorithm under a Cartesian product of (topology × configuration), which is where library bugs actually live.

Real-world example pattern: an audit that paired random strategies with a sweep across all algorithm functions has surfaced library bugs that the upstream maintainers had never caught — specifically because the bug only fired under flag combinations the maintainers didn't test for. You won't know which combination matters until you cover them all.

### Phase 3 — Run, observe, classify

#### 3a. Run with `_n` and loops

The default 100 cases is not enough for bug-hunting. Use `hegel_run_test_n(fn, 1000)` for normal runs and `make test-loop` for overnight discovery. PBT bugs that fire 5–25% of the time will surface within a few hundred to a few thousand cases — but only if the test actually runs that many.

Watch for:

- **Hard failures**: assertion fired, hegel shrunk, you have a counterexample.
- **Crashes** (SIGSEGV, SIGABRT, SIGFPE): caught by fork mode, hegel turns them into shrink targets. Examine the core dumps under `gdb`.
- **Hangs / timeouts**: use a `timeout` wrapper. A hang is a bug too.
- **Health-check failures**: `FilterTooMuch`, `LargeInitialTestCase`. These mean your generator is bad — fix the generator, not the test.

#### 3b. Classify findings

Every finding gets categorized and logged. Three tiers:

| Tier | Meaning | Action |
|---|---|---|
| **Bug** | Property failure, reproducible, root cause likely identifiable | Add to `REPORT.md` with minimal counterexample, expected behavior, observed behavior. Open an upstream issue if confirmed. |
| **Suspect** | Surprising result that might be a bug but needs investigation | Add to `REPORT.md` under "Suspect behavior". Reproduce in a minimal harness. Decide bug vs. non-bug. |
| **Finding** | Non-bug observation about library behavior worth recording | Add to `REPORT.md` under "Other findings". E.g., "this algorithm uses N colors when it could use log N", "this flag has no observable effect on output". |

A bug-hunt session that produces no bugs but ten findings is still successful. The findings often guide the next session.

#### 3c. Produce a `REPORT.md`

End every session with a written report. Sections:

1. **What was built** — number of tests, categories covered, infrastructure added.
2. **What ran successfully** — how many tests pass, total cases run.
3. **Bugs** — one subsection per bug, with: summary, minimal reproducer (code snippet), trigger conditions, root cause hypothesis (clearly marked as hypothesis), what to investigate next.
4. **Suspect behavior** — one subsection per suspect, with: observed behavior, why it's surprising, what would confirm or refute it.
5. **Other findings** — short bullets.
6. **Test inventory** — table of all tests written, what they check, status (pass / xfail / suspect).
7. **Next session** — concrete plan for what to add or investigate.

The report is the **deliverable**. The test code is also a deliverable, but the report is what the user (and any future audit session) reads first.

### Phase 4 — Multi-session iteration

A comprehensive audit takes multiple sessions. Plan for it.

- **Session 1**: build infrastructure (Phase 1), one test per major function (Phase 2a), basic topology sweep (Phase 2b). Produce first `REPORT.md`. Aim for 10–25 tests.
- **Session 2**: add the remaining tests to reach full breadth. Add the second property per function. Investigate Suspects from session 1. Aim for 40–60 tests total.
- **Session 3+**: deepen specific categories, add stress tests, investigate any remaining suspects, run overnight, expand the configuration fuzzer.

Each session reads the prior `REPORT.md` and builds on it. Don't restart from scratch.

## Anti-patterns

1. **Diving deep on one property in session 1.** You'll write three excellent tests on the most obvious algorithm function and never touch the rest of the public API — and the bug is in a function you didn't get to. Breadth before depth.

2. **Skipping shared generator infrastructure.** Inline graph construction in every test causes 30 divergent implementations, all subtly wrong. Build `graph_gen.h` first.

3. **Trusting the default 100 cases for bug hunting.** Bugs that fire below ~10% need 1000+ cases or loop runs. Default 100 is a CI smoke test, not an audit.

4. **Targeting suspected bugs.** "I think there's a bug in X, let me write a test for it" — if you knew the bug well enough to target it, you wouldn't need PBT. Sweep instead. Bugs surface from broad coverage.

5. **Not testing configuration combinations.** For libraries with a strategy parser, ignoring it means missing the entire category of bugs that hide in flag combinations. Many of the most embarrassing bugs in mature C libraries come down to a flag combination the maintainer didn't think to test — and you won't know which one until you sweep them all.

6. **Not testing edge inputs.** Empty graphs, single-vertex graphs, all-isolated-vertex graphs, fully-connected graphs. Library code paths for these are often less-tested by the upstream maintainers.

7. **Not capturing core dumps.** SIGSEGVs without dumps are unreproducible. Set `ulimit -c unlimited` (or a finite size) before running.

8. **Treating shrinking quality as a binary.** If hegel shrinks a 100-vertex counterexample to 50 vertices, that's still useful — extract what you can, file the report, then iterate on the schema to get a smaller counterexample later.

9. **Stopping after one session because nothing failed.** Audit suites build value over multiple sessions. The first session's "it all passes" is often "I haven't tested broadly enough yet."

10. **Skipping the `REPORT.md`.** Without it, the next session starts from zero. The report is what makes this multi-session.

## Concrete first-session output

A good session 1 produces:

- 1 shared input-generator header (`<thing>_gen.h`)
- 1 shared helpers header (`<thing>_helpers.h`) — reset, strategy fuzzer, error classifier
- 10–25 test files, one per major public function category
- A `Makefile` with `test`, `test-loop`, `clean-cores`
- A `REPORT.md` with the test inventory and any bugs / suspects / findings

If the session ends with fewer than 10 tests, the bottleneck is probably the build setup (Phase 1a) or the input-generator design (Phase 1c). Stop and fix those before adding more tests.

## Pointers to the language references

- API surface of hegel-c (draws, schemas, runners): see the `hegel` skill's `references/c/reference.md`.
- Bug-hunting patterns specific to C (shared `_gen.h` headers, `make test-loop` recipe, core dump capture, fork-mode discipline at scale): see `references/c/reference.md` in this skill.

Always read the `hegel` skill's C reference first for the API. This skill assumes you already know how to write a single hegel-c test.
