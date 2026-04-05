/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_filter
** Equivalent of:
**   let value = tc.draw(gs::integers::<i32>().min_value(0).max_value(100).filter(|n| n % 2 == 0));
**   assert!(value % 2 == 0);
**   assert!((0..=100).contains(&value));
*/
#include <stdio.h>

#include "hegel_c.h"

static
int
is_even (int val, void * ctx)
{
  (void) ctx;
  return (val % 2 == 0);
}

static
void
test_filter (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  val;

  gn = hegel_gen_filter_int (hegel_gen_int (0, 100), is_even, NULL);
  val = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (val % 2 == 0,
                "filter produced odd value: %d", val);
  HEGEL_ASSERT (val >= 0 && val <= 100,
                "filter result %d out of [0, 100]", val);

  hegel_gen_free (gn);
}

int
main (void)
{
  hegel_run_test (test_filter);
  return (0);
}
