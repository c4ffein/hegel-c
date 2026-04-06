/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_floats.rs::f32_tests::with_min_and_max
** Rust test does:
**   draw two finite f32s, use as min/max, draw a third, verify in [min, max]
**   find_any(floats(), |n| n > 0.0 && n.is_finite())
**   find_any(floats(), |n| n < 0.0 && n.is_finite())
*/
#include <stdio.h>
#include <math.h>

#include "hegel_c.h"

/* Layer 2: property tests */

static void test_f32_bounds (hegel_testcase * tc) {
  float a = hegel_draw_float (tc, -1e7f, 1e7f);
  float b = hegel_draw_float (tc, -1e7f, 1e7f);
  float lo = a < b ? a : b;
  float hi = a < b ? b : a;
  float n  = hegel_draw_float (tc, lo, hi);
  HEGEL_ASSERT (n >= lo && n <= hi,
                "n=%g not in [%g, %g]", (double) n, (double) lo, (double) hi);
}

static void find_positive (hegel_testcase * tc) {
  float n = hegel_draw_float (tc, -1e7f, 1e7f);
  HEGEL_ASSERT (!(n > 0.0f), "n=%g", (double) n);
}

static void find_negative (hegel_testcase * tc) {
  float n = hegel_draw_float (tc, -1e7f, 1e7f);
  HEGEL_ASSERT (!(n < 0.0f), "n=%g", (double) n);
}

/* Layer 3: runner */

int main (void) {
  int errors = 0;

  if (hegel_run_test_result (test_f32_bounds) != 0) {
    fprintf (stderr, "ERROR: f32 bounds check should pass\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_positive, 1000) != 1) {
    fprintf (stderr, "ERROR: should find positive f32\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_negative, 1000) != 1) {
    fprintf (stderr, "ERROR: should find negative f32\n");
    errors++;
  }

  if (errors > 0) {
    fprintf (stderr, "%d check(s) failed\n", errors);
    return (1);
  }
  return (0);
}
