# Verify one hegel-rust → hegel-c test equivalence

You are comparing a C test file (from hegel-c) against the Rust test it claims to be equivalent to.

## Your task

1. Read the C test file provided as the first argument
2. Extract the `RUST_SOURCE:` tag from its header comment — this tells you the Rust file and function name
3. Read the Rust source file from `inspiration/hegel-rust/` and find the exact test function
4. Compare the two tests semantically
5. Output your verdict as a single line in this exact format:

```
VERDICT: <MATCH|MISMATCH> — <one sentence reason>
```

## Rules

**MATCH** — The C test exercises the same property with equivalent generators and assertions.
- Using `int` for Rust `i32` is fine (same width)
- Using `hegel_draw_int(tc, min, max)` for Rust `tc.draw(gs::integers::<i32>().min_value(min).max_value(max))` is fine
- Using `hegel_gen_*` combinators for Rust `.map()/.filter()/.flat_map()` is fine
- Minor syntactic differences between C and Rust are expected and fine

**MISMATCH** — The C test does NOT faithfully represent the Rust test.
- Different generator bounds
- Assertion checks a different property
- Missing coverage (Rust checks 3 things, C checks 1)
- Wrong `EXPECTED_SHRINK` value for shrink tests
- Uses a different generator type (e.g. int instead of text) even if documented — that means hegel-c needs improvement, not that the test is OK

## For shrink tests

If the C file has `EXPECTED_SHRINK:`, verify it matches the Rust test's `assert_eq!(minimal(...), expected)` value.

## Important

- If the `inspiration/hegel-rust/` directory doesn't exist, output: `ERROR: Run make inspiration first`
- If the Rust source file or function can't be found, output: `ERROR: Rust source not found — <details>`
- Be strict. If the C test covers less than the Rust test, it's a MISMATCH.
- Output ONLY the verdict line. No other text.
