<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Selftest pattern

The selftest suite (`tests/selftest/`) tests hegel-c itself. Each test has **three layers**:

1. **A C function under test** — a standalone function with a *known* bug, crash, or edge case on specific inputs. This is the "code someone wrote." It exists independently of hegel.
2. **A hegel test** — a property test that exercises that function using `hegel_draw_*` and `HEGEL_ASSERT`. Hegel should find the bug and shrink to a minimal counterexample.
3. **The outer runner** — the Makefile target that runs the binary and checks the *exit code* (and optionally stderr). This is the real test: it verifies that hegel-c did its job.

Example structure:
<!-- /ignore example: distilled illustration, not transcluded -->
```c
/* Layer 1: function under test — has a bug when x overflows */
int square(int x) {
    return (int)((unsigned)x * (unsigned)x);  /* wraps */
}

/* Layer 2: hegel test that exercises it */
void testSquare(hegel_testcase *tc) {
    int x = hegel_draw_int(tc, 40000, 100000);
    int result = square(x);
    HEGEL_ASSERT(result >= 0, "square(%d) = %d", x, result);
}

/* main just hands it to hegel */
int main() { hegel_run_test(testSquare); return 0; }
```

Layer 3 (the Makefile) knows this test should exit non-zero — hegel should catch the overflow.

Five categories:
- **PASS tests**: function is correct, hegel should find no bug, exit 0
- **FAIL tests**: function has a known bug, hegel should find it, exit non-zero
- **CRASH tests**: function segfaults/aborts on specific inputs, fork isolation should catch it, exit non-zero
- **CRASH+FAIL tests**: function either crash or fail depending on the inputs, the reducer should still work
- **HEALTH tests**: schema is deliberately pathological (e.g. `HEGEL_FILTER_INT` with a ~99.9999% rejection rate, or `HEGEL_ARRAY_INLINE` with a minimum size that overflows the byte budget). The Hypothesis-side health checks must fire and reach the C user as a non-zero exit plus a recognizable `"Health check failure"` message in stderr. Covers both `filter_too_much` and `large_base_example`, single + suite variants.
