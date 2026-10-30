/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_optional_respects_inner_generator_bounds
** Equivalent of:
**   let value = tc.draw(gs::optional(gs::integers().min_value(10).max_value(20)));
**   if let Some(n) = value { assert!((10..=20).contains(&n)); }
**
** optional(): a present/absent bit in an OPTIONAL span, then the value.
*/
#include <stdio.h>

#include "hegel_c.h"

static
void
test_optional (
hegel_testcase *            tc)
{
  int                  present;
  int                  val = 0;

  hegel_start_span (tc, HEGEL_SPAN_OPTIONAL);
  present = hegel_draw_int (tc, 0, 1);
  if (present)
    val = hegel_draw_int (tc, 10, 20);
  hegel_stop_span (tc, 0);

  if (present) {
    HEGEL_ASSERT (val >= 10 && val <= 20,
                  "optional(int(10,20)) produced %d", val);
  }
}

int
main (void)
{
  hegel_run_test (test_optional);
  return (0);
}
