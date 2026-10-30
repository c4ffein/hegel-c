# Porting C PBT Tests to Hegel

## hegel-c was written fresh

Unlike the Rust, Go, and C++ bindings — each of which has a clear "port from $existing_library" path (proptest/quickcheck → hegel-rust, rapid/gopter → hegel-go, RapidCheck → hegel-cpp) — **hegel-c was written from scratch**. There is no hegel-c precursor in C, and the existing C PBT and fuzzing landscape is dominated by tools that work very differently from hegel.

If you have an existing C codebase using one of the libraries below, **there is no automated translation**. You can still benefit from this skill — read `reference.md`, identify properties from the code-under-test (per the main `SKILL.md` workflow), and write hegel-c tests alongside (or replacing) the existing ones. The notes here are to set expectations on how much carries over.

## Existing C PBT and fuzzing landscape

### theft

[theft](https://github.com/silentbicycle/theft) is the closest prior art to hegel-c — a pure-C99 property-based testing library by silentbicycle. Like hegel-c it forks per case, autoshrinks from a bit pool, and supports multi-arg properties.

**What carries over conceptually:**
- The core PBT loop (random input, assert property, shrink on failure).
- Fork-per-case for crash isolation. theft's `enum theft_run_res` and hegel's exit code conventions are different but the intent matches.
- The idea that broad generators find more bugs.

**What is fundamentally different:**
- **Generation model.** theft uses `enum theft_alloc_res alloc_cb(struct theft *t, void *env, void **instance)` callbacks per type, drawing from a random bit pool; a hegel-c generator is a plain function that fills a value with `hegel_draw_*` calls, which the engine records and shrinks.
- **Shrinking.** theft has hand-written `shrink_cb` callbacks per type; hegel uses Hypothesis's integrated shrinking on the byte stream — you don't write shrink logic at all.
- **Instance hashing for dedup.** theft hashes generated instances to skip duplicates; hegel relies on Hypothesis's byte-stream coverage tracking.
- **Process model.** Both are in-process C; hegel-c loads Hegel's native engine (`libhegel.so`) and, in fork mode, has the forked child ask the parent for every draw over a pipe.

If you're porting a theft test:

1. Drop the `alloc_cb` / `print_cb` / `shrink_cb` triples. The `alloc_cb` body usually becomes the generator almost as is, with its random-bit reads replaced by `hegel_draw_*` calls; print and shrink go away.
2. Replace `struct theft_run_config` with `hegel_run_test_n(test_fn, n_cases)`.
3. Replace `theft_hook_pre_run` / `_post_run` with the global `hegel_set_case_setup` callback.
4. The property body itself usually translates almost directly — get the input (drawn for you in theft, from your generator in hegel-c), assert.

### deepstate

[deepstate](https://github.com/trailofbits/deepstate) is a Trail of Bits framework that exposes a Google-Test-like API on top of multiple back-ends — symbolic execution (Manticore, Angr), coverage-guided fuzzers (AFL, libFuzzer, Eclipser), and example-based runs. Deepstate is **not really comparable to hegel-c** — different design philosophy. Highlights:

- **Multi-backend.** deepstate's value proposition is one harness, many backends. hegel runs against one engine, Hegel's, and puts its effort into that engine's generation and shrinking.
- **Symbolic / coverage-guided.** deepstate can drive symbolic execution or coverage-guided fuzzing; hegel does neither.
- **Stateful testing via `OneOf`.** deepstate has tier-1 stateful testing; hegel-c doesn't yet (see `reference.md` § Stateful Testing).
- **Swarm testing.** deepstate exposes Hypothesis's swarm testing concept directly; hegel-c does not.

If you have deepstate tests, **don't port them blindly to hegel-c.** Decide first whether you want centralized PBT shrinking (use hegel-c) or coverage/symbolic exploration (keep deepstate, or use AFL/libFuzzer directly). The two are complementary, not substitutes.

### Catch2 / Unity / cmocka generators

These are unit-test frameworks. Their "data-driven" features enumerate fixed examples — there is no random exploration or shrinking. Tests that look like:

```c
TEST_P(MyFixture, AdditionCommutes) {
    int a = GetParam().a;
    int b = GetParam().b;
    EXPECT_EQ(a + b, b + a);
}
```

are good candidates for **evolution into hegel-c PBTs** rather than mechanical porting. See `references/evolving-tests.md` for guidance on recognizing the property a parameterized test is hiding. The hegel-c version uses `hegel_draw_int` for the input and the runner of your existing test framework for the surrounding scaffold.

## Coexisting with AFL / libFuzzer

Property-based testing and coverage-guided fuzzing are complementary tools — they explore different parts of the input space and catch different bug classes. hegel-c is deliberately not a coverage-guided fuzzer; if you have AFL or libFuzzer harnesses already, **keep them**.

The full discussion (when to use each, why hegel-c chose its tradeoffs, how to wire both into one test target) lives in [`docs/fuzzing-comparison.md`](../../../../docs/fuzzing-comparison.md) in the hegel-c repo. Read that document if your project already has fuzzers and you're deciding where hegel-c fits — it's the foundation for the recommendations below.

The short version:

- **PBT (hegel-c) is for properties** — you assert what should always hold; hegel finds inputs that violate it.
- **Coverage-guided fuzzing (AFL, libFuzzer) is for crash-only testing of structured input parsers** — feed bytes, watch for SIGSEGV / sanitizer trips.
- **They don't compete for the same bugs.** hegel finds correctness bugs (algorithm wrong on edge case); fuzzers find robustness bugs (parser segfaults on malformed input).
- **Run both in CI** if you have parsers / decoders / wire-format code. Use hegel-c for the algebraic properties, AFL/libFuzzer for the no-crash robustness on raw bytes.

## Porting Checklist

When moving code from theft (or hand-rolled fork-per-case PBT) to hegel-c:

1. **Add hegel-c as a dependency** — vendor or submodule, build the static lib (see `reference.md` § Setup).
2. **Turn each `alloc_cb` into a generator function** (`static void gen_foo (hegel_testcase *tc, Foo *out)`) that draws each field; delete the matching `print_cb` and `shrink_cb`.
3. **Replace random-bit reads with typed draws.** `theft_random_bits` / `theft_random_choice` become `hegel_draw_int (tc, lo, hi)` and friends; wrap list elements in `HEGEL_SPAN_LIST_ELEMENT` spans (see `reference.md` § Writing Generators).
4. **Drop hand-written shrink logic.** Hegel's shrinker operates on the byte stream; remove the `shrink_cb` entirely.
5. **Move the property body into a `void test_fn(hegel_testcase *tc)`** — call your generator, assert, free what it allocated.
6. **Replace `theft_run` with `hegel_run_test_n(test_fn, N)`** — or add to a `hegel_suite` if you have multiple tests in one binary.
7. **Drop instance-hash dedup.** The engine tracks what it has tried on its own; you don't need to dedup explicitly.
8. **Re-enable broader generators.** theft tests often narrow inputs because shrinking was unreliable on edge cases. Hegel's shrinking is robust — try the full type range first, only narrow if a real domain constraint requires it.
9. **Run, observe, iterate.** Failing tests on inputs the old framework didn't reach are the point. Investigate before adding bounds.
