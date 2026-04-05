/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: x + y > x when y > 0 — fails on signed overflow.
**
** Expected: hegel finds the failure. The shrunk counterexample should
** have y small (close to 1) and x close to INT_MAX.
** This binary should EXIT NON-ZERO.
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

static
void
testAdditionMonotone (
hegel_testcase *            tc)
{
  int                 x;
  int                 y;
  int                 sum;

  x = hegel_draw_int (tc, 0, INT_MAX);
  y = hegel_draw_int (tc, 1, INT_MAX);
  sum = x + y; /* may overflow */
  HEGEL_ASSERT (sum > x,
                "x=%d, y=%d, x+y=%d — not greater than x", x, y, sum);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  hegel_run_test (testAdditionMonotone);

  return (0);
}
