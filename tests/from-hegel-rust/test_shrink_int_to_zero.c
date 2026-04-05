/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_shrink_quality/integers.rs::test_can_find_an_int
** EXPECTED_SHRINK: 0
** Equivalent of: assert_eq!(minimal(gs::integers::<i64>(), |_| true), 0)
** Every i64 satisfies the condition; Hegel should shrink to 0 (simplest).
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static
void
test_minimal_zero (
hegel_testcase *            tc)
{
  int64_t             x;

  x = hegel_draw_i64 (tc, INT64_MIN, INT64_MAX);
  /* Always fails — every value is "interesting". Hegel shrinks to 0. */
  HEGEL_ASSERT (0,
                "x=%ld", (long) x);
}

int
main (void)
{
  hegel_run_test (test_minimal_zero);
  return (0);
}
