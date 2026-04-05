/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel detects a failure and shrinks toward the boundary.
**
** Property: "all ints are less than 100" — obviously false.
** Hegel should find a counterexample >= 100 and shrink it to exactly 100
** (the smallest failing value in the range).
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

static
void
testLessThan100 (
hegel_testcase *            tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 10000);
  HEGEL_ASSERT (x < 100,
                "x=%d, expected < 100", x);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  hegel_run_test (testLessThan100);

  return (0);
}
