/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_filter
** Equivalent of:
**   let value = tc.draw(gs::integers::<i32>().min_value(0).max_value(100).filter(|n| n % 2 == 0));
**   assert!(value % 2 == 0);
**   assert!((0..=100).contains(&value));
**
** Schema-API translation of the original combinator-API port.
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

static
int
is_even (int val, void * ctx)
{
  (void) ctx;
  return (val % 2 == 0);
}

static hegel_schema_t  filter_schema;

static
void
test_filter (
hegel_testcase *            tc)
{
  int                  val = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&val, filter_schema);

  HEGEL_ASSERT (val % 2 == 0,
                "filter produced odd value: %d", val);
  HEGEL_ASSERT (val >= 0 && val <= 100,
                "filter result %d out of [0, 100]", val);

  hegel_shape_free (sh);
}

int
main (void)
{
  filter_schema = HEGEL_FILTER_INT (
      HEGEL_INT (0, 100), is_even, NULL);
  hegel_run_test (test_filter);
  hegel_schema_free (filter_schema);
  return (0);
}
