/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_shrink_quality/integers.rs::test_minimize_single_element_in_silly_large_int_range
** EXPECTED_SHRINK: 0
** Rust: minimal(integers().min_value(MIN/2).max_value(MAX/2), |&x| x >= MIN/4) == 0
** Draw from [MIN/2, MAX/2], fail if x >= MIN/4. Hegel shrinks to 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static void test_large_range (hegel_testcase * tc) {
  int64_t x = hegel_draw_i64 (tc, INT64_MIN / 2, INT64_MAX / 2);
  HEGEL_ASSERT (!(x >= INT64_MIN / 4),
                "x=%ld", (long) x);
}

int main (void) {
  hegel_run_test (test_large_range);
  return (0);
}
