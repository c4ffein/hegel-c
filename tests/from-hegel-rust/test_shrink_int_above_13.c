/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_shrink_quality/integers.rs::test_can_find_an_int_above_13
** EXPECTED_SHRINK: 13
** Equivalent of: assert_eq!(minimal(gs::integers::<i64>(), |&x| x >= 13), 13)
** Hegel should shrink to exactly 13 (smallest int >= 13).
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static
void
test_minimal_13 (
hegel_testcase *            tc)
{
  int64_t             x;

  x = hegel_draw_i64 (tc, INT64_MIN, INT64_MAX);
  HEGEL_ASSERT (x < 13,
                "x=%ld >= 13", (long) x);
}

int
main (void)
{
  hegel_run_test (test_minimal_13);
  return (0);
}
