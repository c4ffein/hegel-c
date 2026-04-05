/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_one_of_returns_value_from_one_generator
** Equivalent of:
**   let value = tc.draw(one_of!(gs::integers().min_value(0).max_value(10),
**                               gs::integers().min_value(100).max_value(110)));
**   assert!((0..=10).contains(&value) || (100..=110).contains(&value));
*/
#include <stdio.h>

#include "hegel_c.h"

static
void
test_one_of (
hegel_testcase *            tc)
{
  hegel_gen *          gens[2];
  hegel_gen *          gn;
  int                  val;

  gens[0] = hegel_gen_int (0, 10);
  gens[1] = hegel_gen_int (100, 110);
  gn = hegel_gen_one_of (gens, 2);

  val = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT ((val >= 0 && val <= 10) || (val >= 100 && val <= 110),
                "one_of produced %d, not in [0,10] or [100,110]", val);

  hegel_gen_free (gn);
}

int
main (void)
{
  hegel_run_test (test_one_of);
  return (0);
}
