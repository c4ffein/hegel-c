/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_integers.rs::test_i32
** Equivalent of: assert_all_examples(gs::integers::<i32>(), |&n| n >= i32::MIN && n <= i32::MAX)
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

static
void
test_i32_bounds (
hegel_testcase *            tc)
{
  int                 n;

  n = hegel_draw_int (tc, INT_MIN, INT_MAX);
  HEGEL_ASSERT (n >= INT_MIN && n <= INT_MAX,
                "n=%d out of [INT_MIN, INT_MAX]", n);
}

int
main (void)
{
  hegel_run_test (test_i32_bounds);
  return (0);
}
