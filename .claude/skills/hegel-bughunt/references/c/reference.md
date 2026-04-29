# Hegel Bug Hunt — C Reference

Patterns specific to running a hegel-c bug-hunting audit against a C library. **This is methodology, not API surface.** For the hegel-c API (draws, schemas, runners), read the `hegel` skill's `references/c/reference.md` first — that's a prerequisite.

## Table of Contents

- [Project layout](#project-layout) — directory shape for an audit suite
- [Shared input generators](#shared-input-generators) — `*_gen.h` patterns
- [Shared helpers](#shared-helpers) — global-state reset, error classifiers, config fuzzers
- [Configuration / strategy fuzzing in C](#configuration--strategy-fuzzing-in-c)
- [Makefile for an audit suite](#makefile-for-an-audit-suite) — `test`, `test-loop`, core dump capture
- [Loop runs and overnight execution](#loop-runs-and-overnight-execution)
- [Classifying failures](#classifying-failures) — gdb on cores, signal handling, hangs
- [The `REPORT.md`](#the-reportmd-template) — template
- [Anti-patterns specific to C](#anti-patterns-specific-to-c)

## Project layout

A C bug-hunting suite typically lives in a dedicated subdirectory of the target's tree (or a sibling directory if you can't write to the target). Use a flat structure inside it:

```
src/check/hegel/                        # or wherever; flat is fine
  Makefile                              # build + test + test-loop + clean-cores
  graph_gen.h                           # shared input generators
  scotch_helpers.h                      # shared helpers (reset, strat fuzzer, error)
  test_graph_part.c                     # one test per public function
  test_graph_part_balance.c
  test_graph_part_fixed.c
  test_graph_color.c
  test_graph_order.c
  ...
  bench_test_graph_part_huge.c          # stress tests; slower, run separately
  REPORT.md                             # session-spanning report
  .cores/                               # core dumps land here; gitignored
```

Two non-obvious choices:

- **Flat directory, not subdirs by category.** The Makefile already groups tests; subdirs add friction without value. If you have 80 tests, a flat list is fine.
- **`.cores/` is per-suite, not per-test.** All crashes go in one bucket; you'll want them sorted by test in the analysis pass anyway.

## Shared input generators

The single biggest leverage point in a C audit suite is a clean `*_gen.h` of input generators. Without it, every test reimplements graph construction (or tree, or string-of-tokens, or whatever) and they drift apart. Build this *first*.

### Anatomy of a generator header

Each generator is a function that takes `hegel_testcase *tc` plus shape parameters, returns a heap-allocated value (or fills caller-provided buffers), and is paired with a destructor.

```c
/* graph_gen.h — illustrative, not literal */
#ifndef GRAPH_GEN_H
#define GRAPH_GEN_H

#include "hegel_c.h"
#include "hegel_gen.h"
#include <scotch.h>

typedef struct {
    int          n;
    SCOTCH_Num  *verttab;     /* length n + 1 */
    SCOTCH_Num  *edgetab;     /* length edgenbr */
    SCOTCH_Num   edgenbr;
    int          baseval;     /* 0 or 1 */
} CSRGraph;

CSRGraph *gen_random_sparse(hegel_testcase *tc, int max_n);
CSRGraph *gen_2d_grid      (hegel_testcase *tc, int max_side);
CSRGraph *gen_path         (hegel_testcase *tc, int max_n);
CSRGraph *gen_complete_kn  (hegel_testcase *tc, int max_n);
CSRGraph *gen_disjoint_k2  (hegel_testcase *tc, int max_pairs); /* multiple K_2 components */
CSRGraph *gen_all_isolated (hegel_testcase *tc, int max_n);     /* n vertices, 0 edges */
CSRGraph *gen_single_vertex(hegel_testcase *tc);
CSRGraph *gen_empty        (hegel_testcase *tc);                /* 0 vertices, 0 edges */

/* Topology selector: draws a topology choice and dispatches.
** Use this when a single test should run across multiple shapes. */
CSRGraph *gen_any_topology(hegel_testcase *tc, int max_n);

void csr_free(CSRGraph *g);

#endif
```

Three rules for these generators:

1. **Each generator must produce inputs that pass the library's own validity check.** For Scotch, that's `SCOTCH_graphCheck`. If your generator produces invalid graphs, you waste your test budget on cases the library will reject — and you can't tell whether a failure is a generator bug or a library bug. **Validate every generator output once at the top of every test using `hegel_health_fail` if validation fails**:

   ```c
   CSRGraph *g = gen_random_sparse(tc, 16);
   /* ... build SCOTCH_Graph from CSR ... */
   if (SCOTCH_graphCheck(&grafdat) != 0) {
       hegel_health_fail("generator produced invalid graph");
   }
   ```

2. **Each generator must have a baseval choice.** Many graph libraries support both 0-based and 1-based indexing (Scotch does; FORTRAN-shaped libraries do). Generators should draw `baseval` as 0 or 1 and produce CSR consistent with that choice. This catches off-by-one bugs in code paths that handle one baseval but not the other.

3. **The topology selector dispatches via `HEGEL_DRAW_INT(0, N-1)`.** Hegel's shrinker will minimize the topology index — failures get reported with the simplest topology that triggers them.

### Don't inline; refactor as you go

Resist writing the first generator inline in the first test. The moment you have two tests, factor it out. If you find your `gen_random_sparse` has subtly diverged between two test files, you've already lost.

## Shared helpers

Beyond input generation, every audit suite needs:

### Per-case reset

Most C libraries have global state — internal RNGs, allocator pools, error-state flags, statistics. In fork mode each child starts fresh, but **library state set by the parent before fork is copied into every child** (it's a fork). If your test depends on a specific RNG seed, you need to set it in the child:

```c
static void reset_scotch(void) {
    SCOTCH_randomSeed(42);
    SCOTCH_randomReset();
}

int main(void) {
    hegel_set_case_setup(reset_scotch);
    hegel_run_test_n(test_graph_part, 1000);
    return 0;
}
```

The setup callback runs in the child process before each case. **Without this, library RNG output is not reproducible across cases**, and shrinking can't replay deterministically.

### Error classifier

Library-returned error codes are usually integers without obvious meaning. Wrap them:

```c
static const char *scotch_strerror(int rc) {
    switch (rc) {
    case 0:  return "ok";
    case 1:  return "generic-error";
    case -1: return "invalid-argument";
    /* etc. */
    default: return "unknown";
    }
}
```

Use this in `HEGEL_ASSERT` messages so failures print `expected ok, got generic-error (rc=1)` instead of `rc=1`. The shrunk counterexample is much easier to diagnose.

### Configuration parser fallback

If your config fuzzer (next section) sometimes produces a string the library rejects, **don't fail the test** — that's a generator quality issue, not a library bug. Use `hegel_assume(tc, ok)` to discard the case, and log the rejection rate at the end. If the rate creeps above 20%, fix the fuzzer.

## Configuration / strategy fuzzing in C

Libraries with a configuration parser hide bugs in the configuration combination space. The pattern to test these:

### 1. Inventory the configuration vocabulary

Read the library's documentation. Make a list of every flag, mode, parameter, or sub-strategy. For Scotch's strategy-string parser, this is keywords like `r`, `b`, `f`, `m`, `g`, plus structural composers (`,`, `|`, parentheses) and parameter forms (`{rat=0.7}`, `{maxnghb=8}`).

### 2. Write a generator that emits random valid strings

In C, the cleanest path is a recursive generator that picks a structural form, recurses for sub-strategies, and emits a leaf when it bottoms out. Example skeleton:

```c
/* scotch_helpers.h: */

/* Emit a random valid graph-ordering strategy string into `buf`.
** Returns the length written (excluding terminator). */
int strat_gen_order(hegel_testcase *tc, char *buf, int cap, int max_depth);
```

Implementation sketch:

```c
int strat_gen_order(hegel_testcase *tc, char *buf, int cap, int max_depth) {
    if (max_depth <= 0 || HEGEL_DRAW_INT(0, 1) == 0) {
        /* Leaf: pick a base method. */
        const char *leaf;
        switch (HEGEL_DRAW_INT(0, 4)) {
        case 0: leaf = "h{pass=10}";       break;
        case 1: leaf = "g";                break;
        case 2: leaf = "s";                break;
        case 3: leaf = "f";                break;
        default: leaf = "m{};";            break;
        }
        return snprintf(buf, cap, "%s", leaf);
    }
    /* Recurse: structural composer. */
    int form = HEGEL_DRAW_INT(0, 2);
    /* ... write `(A,B)` or `A|B` or similar, recursing twice ... */
}
```

The hegel-c shrinker will reduce the byte stream behind these draws, so failures shrink to small strategy strings naturally.

### 3. Test every algorithm with a drawn strategy

Every algorithm test should pull a strategy from the fuzzer and use it instead of (or in addition to) `SCOTCH_stratInit` (the default empty strategy):

```c
char strat[1024];
int len = strat_gen_order(tc, strat, sizeof strat, 4);
SCOTCH_Strat s;
SCOTCH_stratInit(&s);
if (SCOTCH_stratGraphOrder(&s, strat) != 0) {
    /* Parser rejected the string — discard, don't fail. */
    SCOTCH_stratExit(&s);
    hegel_assume(tc, 0);
    return;
}
/* run algorithm with `s`, check property, cleanup */
```

This is the pattern that finds bugs in strategy-driven code paths. Without strategy fuzzing, every algorithm test runs only against the library default — and the bugs that hide behind specific configurations never surface. The fuzzer doesn't need to be smart about which configurations matter; the shrinker will minimize when something fails.

### 4. Also test with explicit flag combinations

For libraries that expose flag enums separately from strategy strings, build a fuzzer that emits random subsets of the flag space — one shrinkable bit per documented flag:

```c
/* For a hypothetical library MYLIB with flag macros MYLIB_FLAG_*: */
unsigned flags = 0;
if (HEGEL_DRAW_INT(0, 1)) flags |= MYLIB_FLAG_FAST;
if (HEGEL_DRAW_INT(0, 1)) flags |= MYLIB_FLAG_QUALITY;
if (HEGEL_DRAW_INT(0, 1)) flags |= MYLIB_FLAG_SAFETY;
/* ... one line per flag the documentation lists ... */
mylib_configure(&handle, flags);
```

One bit per flag, drawn independently. The shrinker will minimize the flag set — when a bug fires, you'll get the minimal combination that triggers it.

**Generate every flag the documentation lists**, even ones whose effect isn't clear from the docs. The flags that look like "no-ops" or "edge-case-handlers" are exactly the ones whose code paths get the least testing upstream — and where bugs hide. "Flag with no observable effect" is itself a finding worth recording in the report; "flag that makes correct output go wrong" is a bug.

When the library exposes the same configurability via *both* a strategy-string parser and a flag enum, fuzz both — they typically dispatch to overlapping but not identical code paths.

## Makefile for an audit suite

Audit Makefiles need three things `make test` doesn't usually provide: per-test core dump capture, a loop runner, and clean-cores.

```make
# SPDX-License-Identifier: MIT

REPO_ROOT := $(abspath ../../..)
HEGEL_C_LIB := $(REPO_ROOT)/rust-version/target/release
TARGET_INC ?= -I/usr/include/scotch
TARGET_LIB ?= 

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -I$(REPO_ROOT) $(TARGET_INC) -funwind-tables -fexceptions -g
LDFLAGS := -L$(HEGEL_C_LIB) $(TARGET_LIB) -lhegel_c -lscotch -lscotcherr -lz -lm -lpthread -ldl -lrt

# Every test. Order doesn't matter for correctness — but slower tests last so
# fast feedback comes first when running the full suite.
TESTS := \
  test_graph_part \
  test_graph_part_balance \
  test_graph_part_fixed \
  test_graph_color \
  test_graph_order \
  test_graph_order_strat \
  test_graph_save_load \
  test_graph_induce \
  test_arch_roundtrip \
  test_strat_use \
  test_stress_dense \
  test_stress_disconnected \
  test_stress_isolated \
  ...

CORES := .cores

.PHONY: all test test-loop clean clean-cores help check-license

all: $(TESTS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Single full-suite run. Each test gets a fresh core file location.
test: all
	@mkdir -p $(CORES)
	@cd $(REPO_ROOT) && \
		failed=0; \
		for t in $(TESTS); do \
			printf "==== %-40s " "$$t"; \
			ulimit -c unlimited 2>/dev/null; \
			rc=0; \
			cd $(CURDIR) && timeout 120 ./$$t > $(CORES)/$$t.log 2>&1 || rc=$$?; \
			cd $(REPO_ROOT); \
			if [ $$rc -eq 0 ]; then \
				echo "OK"; \
			else \
				echo "FAIL (rc=$$rc)"; \
				failed=1; \
				if [ -f core ]; then mv core $(CURDIR)/$(CORES)/$$t.core; fi; \
			fi; \
		done; \
		test $$failed -eq 0

# Loop until failure or timeout. Usage:
#   timeout 5h make test-loop
test-loop:
	@iter=0; \
	while $(MAKE) -s test; do \
		iter=$$((iter + 1)); \
		echo "==== loop $$iter passed at $$(date +%T) ===="; \
	done; \
	echo "==== loop failed after $$iter iterations ===="; \
	echo "see $(CORES)/ for failure logs and core dumps"

clean:
	rm -f $(TESTS)
	rm -rf $(CORES)

clean-cores:
	rm -rf $(CORES)
	mkdir -p $(CORES)

check-license:
	@for f in *.c *.h Makefile; do \
		if ! head -3 "$$f" | grep -q "SPDX-License-Identifier:"; then \
			echo "MISSING SPDX: $$f"; exit 1; \
		fi; \
	done
	@echo "All files have SPDX headers."

help:
	@echo "Targets:"
	@echo "  test         Run the full suite once (default)."
	@echo "  test-loop    Run the suite in a loop until failure."
	@echo "  clean        Remove built binaries + cores."
	@echo "  clean-cores  Just remove cores/logs."
```

Notes:

- The `cd $(REPO_ROOT)` before running each test is required for the hegel server's `.hegel/` resolution (see the `hegel` skill's C reference).
- Per-test `timeout 120` prevents one hanging test from stalling the whole run.
- Per-test `.log` files keep failure context separate. After a long run you grep `.cores/*.log` for failure patterns.

## Loop runs and overnight execution

The default 100 cases per run is a smoke test. To find rare bugs, run for many cases or in a loop. Three escalating tiers:

1. **`hegel_run_test_n(fn, 1000)`** — bumps per-test cases. Bugs that fire >5% will surface within one run.
2. **`make test-loop`** — re-runs the entire suite repeatedly. Good for CI, finds cross-test variance, ~1 minute per loop for a 20-test suite.
3. **`timeout 5h make test-loop`** overnight — runs ~6M total test cases for a moderate suite. Bugs that fire <0.1% surface here.

When a loop run finds a failure, the `.cores/` directory has the log and (if it crashed) the core dump. The next session starts by triaging these.

## Classifying failures

A failure can be:

- **Property failure** (`HEGEL_ASSERT` fired): hegel printed a counterexample. Read the output, copy the `Draw N: …` lines into a minimal reproducer, file the bug.
- **Crash** (SIGSEGV, SIGABRT, SIGFPE): fork mode caught it; hegel shrinking ran on it. The output should still show a shrunk crash counterexample. Examine the core under `gdb`:

  ```bash
  gdb ./test_graph_part .cores/test_graph_part.core
  (gdb) bt
  ```

  The backtrace points at the offending library function. Often it's not the function the test was directly calling — the bug is deeper.

- **Hang** (process killed by `timeout`): the library entered an infinite loop. Sometimes a real bug (unhandled cycle), sometimes a test issue (filter rejection rate too high). Distinguish by examining the test's draw counts in the log.

- **Health-check failure** (`Health check failure: …`): your test setup is wrong. Generator producing invalid input, filter rejecting too much, recursion exhausting the depth bound. **Fix the test, not the library.**

For each failure category, write the result into `REPORT.md` under the appropriate tier (Bug / Suspect / Finding). Don't let failures pile up untriaged — by session 3 you won't remember which is which.

## The `REPORT.md` template

```markdown
# Audit Report — <Library Name> — Session <N>

**Date:** <YYYY-MM-DD>
**Hegel-c version:** <commit / version>
**Library version:** <commit / version>
**Total test cases run this session:** <approx>

## Summary

One paragraph: what was done, what was found, what's next.

## What was built (this session)

- N test files added: <list>
- Shared infrastructure: <new generators, helpers>
- Coverage gaps remaining: <functions not yet tested>

## Bugs

### Bug 1: <title>

**Trigger:** <minimal conditions>
**Frequency:** <approx % of cases that hit it>
**Severity:** <crash / silent corruption / wrong result / performance>

**Minimal reproducer:**
```c
/* paste minimal C snippet that reproduces */
```

**Observed behavior:** <what happens>
**Expected behavior:** <what the docs / contract say>
**Root cause hypothesis (UNCONFIRMED):** <if you have a guess, mark it clearly>
**Investigation TODO:** <what to read to confirm>

## Suspect behavior

### Suspect 1: <title>

<observed behavior, why it's suspicious, what would confirm/refute>

## Other findings (not bugs)

- <bullet> <library behavior worth recording even though it's not a bug>

## Test inventory

| Test | Property checked | Status |
|---|---|---|
| `test_graph_part` | output in [0, nparts) | pass |
| `test_graph_color` | proper coloring | pass |
| `test_strat_use` | strategy parser | XFAIL — see Bug 1 |
| ... | ... | ... |

## Next session

- <concrete plan: which tests to add, which suspects to investigate, what to deepen>
```

This is the **deliverable**. The user reads this first; the test code is the supporting artifact.

## Anti-patterns specific to C

1. **Per-test inline graph construction.** If `test_graph_part.c` builds a CSR graph inline and `test_graph_color.c` does the same, they'll diverge. Refactor into `graph_gen.h` after the second test. By the tenth test, this would be a maintenance nightmare without the shared header.

2. **Forgetting `ulimit -c unlimited` in the runner.** SIGSEGVs without dumps are unreproducible. Set it in the Makefile, not as a one-off.

3. **`-O2` without `-g`.** Crashes are unreadable in gdb without debug info. Use `-O2 -g` for audit builds — slightly larger binaries, vastly easier triage.

4. **Per-case state reset in `main()` instead of `hegel_set_case_setup`.** Doing `SCOTCH_randomSeed(42)` in `main()` only seeds it once for the parent. Each child inherits at fork time, but subsequent draws shift. Use `hegel_set_case_setup` for true per-case reset.

5. **Strategy fuzzer that always falls back to default.** If your fuzzer produces strings the parser rejects 80% of the time, every test ends up with the default strategy — and you've wasted the audit. Track the rejection rate; if >20%, narrow the fuzzer's grammar.

6. **Building stress tests at small N.** Stress tests should probe **scaling bugs** — integer overflow, allocator pressure, cache pathology. n=100 is not stress; n=10K+ is. Keep the stress tests separate from the main suite (so the main suite stays fast) and run them less frequently.

7. **Not gitignoring `.cores/`**. Core dumps are binary artifacts that don't belong in the repo.

8. **Treating "all tests pass" as success in session 1.** It usually means: not enough tests, or not enough cases, or generators that don't probe interesting inputs. Compare your test count and total case count against the project's API surface — if it's an obvious gap, add more tests before declaring victory.

## Cross-references

- Single-test API surface: `.claude/skills/hegel/references/c/reference.md`
- Property catalogue (Tier 1 patterns): `.claude/skills/hegel/SKILL.md`
- This skill's methodology: `.claude/skills/hegel-bughunt/SKILL.md`
