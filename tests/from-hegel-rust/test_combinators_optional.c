/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_optional_respects_inner_generator_bounds
** Equivalent of:
**   let value = tc.draw(gs::optional(gs::integers().min_value(10).max_value(20)));
**   if let Some(n) = value { assert!((10..=20).contains(&n)); }
*/
#include <stdio.h>

#include "hegel_c.h"

static
void
test_optional (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  present;
  int                  out;

  gn = hegel_gen_optional (hegel_gen_int (10, 20));

  out = -1;
  present = hegel_gen_draw_optional_int (tc, gn, &out);

  HEGEL_ASSERT (present == 0 || present == 1,
                "draw_optional returned %d", present);

  if (present) {
    HEGEL_ASSERT (out >= 10 && out <= 20,
                  "optional(int(10,20)) produced %d", out);
  }

  hegel_gen_free (gn);
}

int
main (void)
{
  hegel_run_test (test_optional);
  return (0);
}
