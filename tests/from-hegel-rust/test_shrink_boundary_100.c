/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_shrink_quality/integers.rs::test_integers_from_minimizes_leftwards (adapted)
** EXPECTED_SHRINK: 100
** Adapted: minimal(gs::integers::<i64>().min_value(100), |_| true) == 100
** Draw from [100, INT64_MAX], always fail. Hegel shrinks to 100 (leftmost).
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static
void
test_boundary_100 (
hegel_testcase *            tc)
{
  int64_t             x;

  x = hegel_draw_i64 (tc, 100, INT64_MAX);
  /* Always fails. Hegel shrinks to 100 (min of range). */
  HEGEL_ASSERT (0,
                "x=%ld", (long) x);
}

int
main (void)
{
  hegel_run_test (test_boundary_100);
  return (0);
}
