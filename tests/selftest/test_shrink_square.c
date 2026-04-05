/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: x * x >= 0 fails on signed overflow when |x| > 46340.
**
** Layer 1: square() computes x*x via unsigned cast — wraps on overflow.
** Layer 2: hegel draws x in [40000, 100000] and asserts square(x) >= 0.
**          Hegel should find and shrink to a value near 46341.
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Computes x*x.  Uses unsigned multiply to avoid UB, then casts back.
** Bug: result wraps negative for |x| > 46340. */

static
int
square (
int                         x)
{
  unsigned int              usq;

  usq = (unsigned int) x * (unsigned int) x;
  return ((int) usq);
}

/* ---- Layer 2: hegel test ---- */

static
void
testSquareNonNeg (
hegel_testcase *            tc)
{
  int                 x;
  int                 result;

  x = hegel_draw_int (tc, 40000, 100000);
  result = square (x);
  HEGEL_ASSERT (result >= 0,
                "square(%d) = %d — expected non-negative", x, result);
}

/* ---- Layer 3: runner (see Makefile TESTS_FAIL) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  hegel_run_test (testSquareNonNeg);

  return (0);
}
