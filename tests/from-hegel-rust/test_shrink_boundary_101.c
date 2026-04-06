/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_shrink_quality/integers.rs::test_integers_from_minimizes_leftwards
** EXPECTED_SHRINK: 101
** Rust: minimal(gs::integers::<i64>().min_value(101), |_| true) == 101
** Draw from [101, INT64_MAX], always fail. Hegel shrinks to 101 (leftmost).
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static
void
test_boundary_101 (
hegel_testcase *            tc)
{
  int64_t             x;

  x = hegel_draw_i64 (tc, 101, INT64_MAX);
  /* Always fails. Hegel shrinks to 101 (min of range). */
  HEGEL_ASSERT (0,
                "x=%ld", (long) x);
}

int
main (void)
{
  hegel_run_test (test_boundary_101);
  return (0);
}
