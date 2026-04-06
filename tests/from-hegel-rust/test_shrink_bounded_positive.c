/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_shrink_quality/integers.rs::test_minimize_bounded_integers_to_positive
** EXPECTED_SHRINK: 1
** Rust: minimal(integers::<i64>().min_value(-10).max_value(10).filter(|&x| x != 0), |_| true) == 1
** Draw from [-10, 10], filter out 0, always fail. Hegel shrinks to 1
** (simplest non-zero value: 0 < 1 < -1 < 2 < -2 ...).
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static int not_zero (int64_t val, void * ctx) {
  (void) ctx;
  return val != 0;
}

static void test_bounded_positive (hegel_testcase * tc) {
  hegel_gen * g = hegel_gen_filter_i64 (hegel_gen_i64 (-10, 10), not_zero, NULL);
  int64_t x = hegel_gen_draw_i64 (tc, g);
  hegel_gen_free (g);
  HEGEL_ASSERT (0, "x=%ld", (long) x);
}

int main (void) {
  hegel_run_test (test_bounded_positive);
  return (0);
}
