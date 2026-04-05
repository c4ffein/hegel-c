/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: x + y > x when y > 0 — fails on signed overflow.
**
** Layer 1: checked_add() adds two ints without overflow protection.
** Layer 2: hegel draws x in [0, INT_MAX] and y in [1, INT_MAX],
**          asserts checked_add(x, y) > x.  Hegel should shrink to
**          y close to 1 and x close to INT_MAX.
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Adds two integers.  Bug: no overflow check — wraps around for
** large x + y, returning a value smaller than x. */

static
int
checked_add (
int                         x,
int                         y)
{
  return (x + y);
}

/* ---- Layer 2: hegel test ---- */

static
void
testAddMonotone (
hegel_testcase *            tc)
{
  int                 x;
  int                 y;
  int                 result;

  x = hegel_draw_int (tc, 0, INT_MAX);
  y = hegel_draw_int (tc, 1, INT_MAX);
  result = checked_add (x, y);
  HEGEL_ASSERT (result > x,
                "checked_add(%d, %d) = %d — not greater than x", x, y, result);
}

/* ---- Layer 3: runner (see Makefile TESTS_FAIL) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  hegel_run_test (testAddMonotone);

  return (0);
}
