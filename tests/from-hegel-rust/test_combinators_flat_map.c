/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_flat_map
** Equivalent of:
**   let value = tc.draw(gs::integers::<usize>().min_value(1).max_value(5)
**       .flat_map(|len| gs::text().min_size(len).max_size(len)));
**   assert!(!value.is_empty());
**   assert!(value.chars().count() <= 5);
**
** NOTE: hegel-c flat_map doesn't support text generators yet.
** Adapted: flat_map(n -> int(0, n)) on int(1, 5), verify result in [0, 5].
*/
#include <stdio.h>

#include "hegel_c.h"

static
hegel_gen *
make_range (int n, void * ctx)
{
  (void) ctx;
  return (hegel_gen_int (0, n));
}

static
void
test_flat_map (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  val;

  gn = hegel_gen_flat_map_int (hegel_gen_int (1, 5), make_range, NULL);
  val = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (val >= 0 && val <= 5,
                "flat_map result %d not in [0, 5]", val);

  hegel_gen_free (gn);
}

int
main (void)
{
  hegel_run_test (test_flat_map);
  return (0);
}
