/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_floats.rs::f64_tests::with_min_and_max
** Rust test does:
**   draw two finite f64s, use as min/max, draw a third, verify in [min, max]
**   find_any(floats(), |n| n > 0.0 && n.is_finite())
**   find_any(floats(), |n| n < 0.0 && n.is_finite())
*/
#include <stdio.h>
#include <math.h>

#include "hegel_c.h"

/* Layer 2: property tests */

static void test_f64_bounds (hegel_testcase * tc) {
  double a = hegel_draw_double (tc, -1e15, 1e15);
  double b = hegel_draw_double (tc, -1e15, 1e15);
  double lo = a < b ? a : b;
  double hi = a < b ? b : a;
  double n  = hegel_draw_double (tc, lo, hi);
  HEGEL_ASSERT (n >= lo && n <= hi,
                "n=%g not in [%g, %g]", n, lo, hi);
}

static void find_positive (hegel_testcase * tc) {
  double n = hegel_draw_double (tc, -1e15, 1e15);
  HEGEL_ASSERT (!(n > 0.0), "n=%g", n);
}

static void find_negative (hegel_testcase * tc) {
  double n = hegel_draw_double (tc, -1e15, 1e15);
  HEGEL_ASSERT (!(n < 0.0), "n=%g", n);
}

/* Layer 3: runner */

int main (void) {
  int errors = 0;

  if (hegel_run_test_result (test_f64_bounds) != 0) {
    fprintf (stderr, "ERROR: f64 bounds check should pass\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_positive, 1000) != 1) {
    fprintf (stderr, "ERROR: should find positive f64\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_negative, 1000) != 1) {
    fprintf (stderr, "ERROR: should find negative f64\n");
    errors++;
  }

  if (errors > 0) {
    fprintf (stderr, "%d check(s) failed\n", errors);
    return (1);
  }
  return (0);
}
