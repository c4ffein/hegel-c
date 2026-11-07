<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Draft issue for hegeldev/hegel: `FloatChoice::simplest: no valid float for this choice` in a passing run

Not posted. Found 2026-11-06 through hegel-c's port of a hegel-rust test
(`tests/from-hegel-rust/test_floats_f64_bounds.c`), which failed `make test`
once in a while.

---

**Title:** Internal error `FloatChoice::simplest: no valid float for this choice` on a float drawn between two drawn floats

**What happens.** A property that draws two doubles and then a third one
between them fails, in about 3% of runs, with an engine internal error
instead of passing:

```
Internal error in hegel at libhegel/src/native/core/choices.rs:707:9:
FloatChoice::simplest: no valid float for this choice.
This is a bug in hegel itself; please report it at https://github.com/hegeldev/hegel/issues
```

The draw that reports it returns `HEGEL_E_INTERNAL` (rc -8). The property
itself always holds, so this is the generation phase, not shrinking.

**Reproducer** (C, through hegel-c; the draws are plain
`hegel_generate_float` calls with these bounds, no NaN, no infinities):

<!-- /ignore draft for upstream, compiled from a scratch file at the time -->
```c
static void test_f64_bounds (hegel_testcase * tc) {
  double a = hegel_draw_double (tc, -1e15, 1e15);
  double b = hegel_draw_double (tc, -1e15, 1e15);
  double lo = a < b ? a : b, hi = a < b ? b : a;
  double n  = hegel_draw_double (tc, lo, hi);
  HEGEL_ASSERT (n >= lo && n <= hi, "n=%g not in [%g, %g]", n, lo, hi);
}
```

With the default 100 test cases:

| libhegel | failing seeds |
|---|---|
| 0.48.0 | `HEGEL_SEED` = 40, 85, 117, 152, 178, 252, 269, 274 among 1–300 (deterministic: seed 40 fails 3 runs out of 3) |
| 0.49.0 | the same eight seeds, and 7 of 200 fresh seeds (301–500) |
| 0.50.0 | the same eight seeds, and 7 of 200 fresh seeds (301–500) |

The Rust equivalent is hegel-rust's float-bounds test
(`gs::floats().min_value(lo).max_value(hi)` with `lo`, `hi` drawn first); we
have not run it there.

**What we checked.** Fixed bound pairs never trigger it: `[0, 0]`,
`[-0.0, 0.0]`, `[-0.0, -0.0]`, `[1, 1]`, `[5e-324, 5e-324]`,
`[-5e-324, 5e-324]`, `[0, 5e-324]`, `[1e15, 1e15]`, `[1.5, nextafter]` all
pass 200 cases. So it depends on the bounds changing from one execution to
the next.

**Hypothesis (not confirmed).** `FloatChoice::simplest_nonzero` documents
that "a choice whose only valid float is zero is an internal error — callers
ask only when they hold a valid non-zero value". Here the third draw's bounds
come from the first two, so a mutation of the generation phase (span
mutation, or replaying a recorded prefix) can carry a non-zero value recorded
under wide bounds into an execution where `a` and `b` came out equal or
zero, and the third choice's range admits only zeros. The caller's
precondition then no longer holds. Clamping or re-drawing in that case,
rather than asking `simplest_nonzero`, would avoid it.

**Environment.** Linux x86-64, libhegel 0.48.0, 0.49.0 and 0.50.0 prebuilt release
artifacts, through hegel-c (fork mode and nofork mode both show it).
