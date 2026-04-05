/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: x * x >= 0 fails on signed overflow when |x| > 46340.
**
** We draw x in [40000, 100000] so overflow is very likely (any x > 46340
** overflows). Hegel should find and shrink to a value near 46341.
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

static
void
testSquareNonNegative (
hegel_testcase *            tc)
{
  int                 x;
  unsigned int        usq;
  int                 sq;

  x = hegel_draw_int (tc, 40000, 100000);
  /* Cast to unsigned to compute without UB, then cast back to check sign bit.
  ** At -O2, signed overflow is UB and the compiler can optimize away checks,
  ** so we do the multiply in unsigned and interpret the result. */
  usq = (unsigned int) x * (unsigned int) x;
  sq = (int) usq;
  HEGEL_ASSERT (sq >= 0,
                "x=%d, x*x=%d — expected non-negative", x, sq);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  hegel_run_test (testSquareNonNegative);

  return (0);
}
