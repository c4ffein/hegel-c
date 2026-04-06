/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_integers.rs::test_u64
** Rust test does:
**   assert_all_examples(integers::<u64>(), |&n| n >= MIN && n <= MAX)
**   find_any(integers::<u64>(), |&n| n > MAX / 2)
**   find_any(integers::<u64>(), |&n| n == MIN)
**   find_any(integers::<u64>(), |&n| n == MAX)
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

/* Layer 2: property tests */

static void test_u64_bounds (hegel_testcase * tc) {
  uint64_t n = hegel_draw_u64 (tc, 0, UINT64_MAX);
  HEGEL_ASSERT (n <= UINT64_MAX,
                "n=%lu out of [0, UINT64_MAX]", (unsigned long) n);
}

static void find_upper_half (hegel_testcase * tc) {
  uint64_t n = hegel_draw_u64 (tc, 0, UINT64_MAX);
  HEGEL_ASSERT (!(n > UINT64_MAX / 2), "n=%lu", (unsigned long) n);
}

static void find_min (hegel_testcase * tc) {
  uint64_t n = hegel_draw_u64 (tc, 0, UINT64_MAX);
  HEGEL_ASSERT (!(n == 0), "n=%lu", (unsigned long) n);
}

static void find_max (hegel_testcase * tc) {
  uint64_t n = hegel_draw_u64 (tc, 0, UINT64_MAX);
  HEGEL_ASSERT (!(n == UINT64_MAX), "n=%lu", (unsigned long) n);
}

/* Layer 3: runner */

int main (void) {
  int errors = 0;

  if (hegel_run_test_result (test_u64_bounds) != 0) {
    fprintf (stderr, "ERROR: bounds check should pass\n");
    errors++;
  }
  /* Rust's find_any uses max_attempts=1000. */
  if (hegel_run_test_result_n (find_upper_half, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n > UINT64_MAX/2\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_min, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n == 0\n");
    errors++;
  }
  if (hegel_run_test_result_n (find_max, 1000) != 1) {
    fprintf (stderr, "ERROR: should find n == UINT64_MAX\n");
    errors++;
  }

  if (errors > 0) {
    fprintf (stderr, "%d find_any check(s) failed\n", errors);
    return (1);
  }
  return (0);
}
