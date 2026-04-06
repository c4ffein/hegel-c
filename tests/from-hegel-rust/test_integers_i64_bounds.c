/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_integers.rs::test_i64
** Rust test does:
**   assert_all_examples(integers::<i64>(), |&n| n >= MIN && n <= MAX)
**   find_any(integers::<i64>(), |&n| n < MIN / 2)
**   find_any(integers::<i64>(), |&n| n > MAX / 2)
**   find_any(integers::<i64>(), |&n| n == MIN)
**   find_any(integers::<i64>(), |&n| n == MAX)
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

/* Layer 2: property tests */

static void test_i64_bounds (hegel_testcase * tc) {
  int64_t n = hegel_draw_i64 (tc, INT64_MIN, INT64_MAX);
  HEGEL_ASSERT (n >= INT64_MIN && n <= INT64_MAX,
                "n=%ld out of [INT64_MIN, INT64_MAX]", (long) n);
}

static void find_lower_half (hegel_testcase * tc) {
  int64_t n = hegel_draw_i64 (tc, INT64_MIN, INT64_MAX);
  HEGEL_ASSERT (!(n < INT64_MIN / 2), "n=%ld", (long) n);
}

static void find_upper_half (hegel_testcase * tc) {
  int64_t n = hegel_draw_i64 (tc, INT64_MIN, INT64_MAX);
  HEGEL_ASSERT (!(n > INT64_MAX / 2), "n=%ld", (long) n);
}

static void find_min (hegel_testcase * tc) {
  int64_t n = hegel_draw_i64 (tc, INT64_MIN, INT64_MAX);
  HEGEL_ASSERT (!(n == INT64_MIN), "n=%ld", (long) n);
}

static void find_max (hegel_testcase * tc) {
  int64_t n = hegel_draw_i64 (tc, INT64_MIN, INT64_MAX);
  HEGEL_ASSERT (!(n == INT64_MAX), "n=%ld", (long) n);
}

/* Layer 3: runner */

int main (void) {
  int errors = 0;

  if (hegel_run_test_result (test_i64_bounds) != 0) {
    fprintf (stderr, "ERROR: bounds check should pass\n");
    errors++;
  }
  /* find_any in Rust uses max_attempts=1000; we need more cases
  ** than the default to reliably hit exact boundary values. */
  if (hegel_run_test_result_n (find_lower_half, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n < INT64_MIN/2\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_upper_half, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n > INT64_MAX/2\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_min, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n == INT64_MIN\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_max, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n == INT64_MAX\n");
    errors++;
  }

  if (errors > 0) {
    fprintf (stderr, "%d find_any check(s) failed\n", errors);
    return (1);
  }
  return (0);
}
