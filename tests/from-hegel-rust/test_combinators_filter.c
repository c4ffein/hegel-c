/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_filter
** Equivalent of:
**   let value = tc.draw(gs::integers::<i32>().min_value(0).max_value(100).filter(|n| n % 2 == 0));
**   assert!(value % 2 == 0);
**   assert!((0..=100).contains(&value));
**
** filter(): draw, and reject the case when the predicate fails.
*/
#include <stdio.h>

#include "hegel_c.h"

static
void
test_filter (
hegel_testcase *            tc)
{
  int                  val;

  val = hegel_draw_int (tc, 0, 100);
  hegel_assume (tc, val % 2 == 0);

  HEGEL_ASSERT (val % 2 == 0,
                "filter produced odd value: %d", val);
  HEGEL_ASSERT (val >= 0 && val <= 100,
                "filter result %d out of [0, 100]", val);
}

int
main (void)
{
  hegel_run_test (test_filter);
  return (0);
}
