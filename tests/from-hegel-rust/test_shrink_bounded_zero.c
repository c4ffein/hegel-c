/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_shrink_quality/integers.rs::test_minimize_bounded_integers_to_zero
** EXPECTED_SHRINK: 0
** Rust: minimal(integers::<i64>().min_value(-10).max_value(10), |_| true) == 0
** Draw from [-10, 10], always fail. Hegel shrinks to 0 (simplest).
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static void test_bounded_zero (hegel_testcase * tc) {
  int64_t x = hegel_draw_i64 (tc, -10, 10);
  HEGEL_ASSERT (0, "x=%ld", (long) x);
}

int main (void) {
  hegel_run_test (test_bounded_zero);
  return (0);
}
