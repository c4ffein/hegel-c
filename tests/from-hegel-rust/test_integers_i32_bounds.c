/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_integers.rs::test_i32
** Rust test does:
**   assert_all_examples(integers::<i32>(), |&n| n >= MIN && n <= MAX)
**   find_any(integers::<i32>(), |&n| n < MIN / 2)
**   find_any(integers::<i32>(), |&n| n > MAX / 2)
**   find_any(integers::<i32>(), |&n| n == MIN)
**   find_any(integers::<i32>(), |&n| n == MAX)
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

/* Layer 1: identity — draw i32 full range */

/* Layer 2: property tests */

static void test_i32_bounds (hegel_testcase * tc) {
  int n = hegel_draw_int (tc, INT_MIN, INT_MAX);
  HEGEL_ASSERT (n >= INT_MIN && n <= INT_MAX,
                "n=%d out of [INT_MIN, INT_MAX]", n);
}

/* find_any helpers: these SHOULD fail — proving the generator can
** produce values matching the condition.  We assert the negation so
** that hegel finds a counterexample.  hegel_run_test_result returning
** 1 means "found one." */

static void find_lower_half (hegel_testcase * tc) {
  int n = hegel_draw_int (tc, INT_MIN, INT_MAX);
  HEGEL_ASSERT (!(n < INT_MIN / 2), "n=%d", n);
}

static void find_upper_half (hegel_testcase * tc) {
  int n = hegel_draw_int (tc, INT_MIN, INT_MAX);
  HEGEL_ASSERT (!(n > INT_MAX / 2), "n=%d", n);
}

static void find_min (hegel_testcase * tc) {
  int n = hegel_draw_int (tc, INT_MIN, INT_MAX);
  HEGEL_ASSERT (!(n == INT_MIN), "n=%d", n);
}

static void find_max (hegel_testcase * tc) {
  int n = hegel_draw_int (tc, INT_MIN, INT_MAX);
  HEGEL_ASSERT (!(n == INT_MAX), "n=%d", n);
}

/* Layer 3: runner */

int main (void) {
  int errors = 0;

  /* assert_all_examples: should PASS */
  if (hegel_run_test_result (test_i32_bounds) != 0) {
    fprintf (stderr, "ERROR: bounds check should pass\n");
    errors++;
  }

  /* find_any: each should FAIL (rc == 1 means hegel found one).
  ** Rust's find_any uses max_attempts=1000. */
  if (hegel_run_test_result_n (find_lower_half, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n < INT_MIN/2\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_upper_half, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n > INT_MAX/2\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_min, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n == INT_MIN\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_max, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n == INT_MAX\n");
    errors++;
  }

  if (errors > 0) {
    fprintf (stderr, "%d find_any check(s) failed\n", errors);
    return (1);
  }
  return (0);
}
