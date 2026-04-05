/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_draw_silent_non_debug (adapted)
** Adapted: map(x -> x + n) where n is drawn, verify f(10) == 10 + f(0).
** Since C can't return closures, we test map correctness directly:
**   map(x -> x * 2) on int(0, 50), verify result is even and in [0, 100].
*/
#include <stdio.h>

#include "hegel_c.h"

static
int
double_it (int val, void * ctx)
{
  (void) ctx;
  return (val * 2);
}

static
void
test_map (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  val;

  gn = hegel_gen_map_int (hegel_gen_int (0, 50), double_it, NULL);
  val = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (val % 2 == 0,
                "map(x*2) produced odd: %d", val);
  HEGEL_ASSERT (val >= 0 && val <= 100,
                "map(x*2) out of range: %d", val);

  hegel_gen_free (gn);
}

int
main (void)
{
  hegel_run_test (test_map);
  return (0);
}
