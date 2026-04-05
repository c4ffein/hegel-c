/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_assume discards test cases without counting as failure.
**
** Layer 1: half() returns x / 2 — correct for even x where half(x)*2 == x.
** Layer 2: hegel draws x in [0, 100], assumes x is even, then asserts
**          half(x) * 2 == x.  The assume filters odd values.
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Integer halving.  For even inputs, half(x) * 2 == x. */

static
int
half (
int                         x)
{
  return (x / 2);
}

/* ---- Layer 2: hegel test ---- */

static
void
testAssume (
hegel_testcase *            tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 100);
  hegel_assume (tc, x % 2 == 0);

  HEGEL_ASSERT (half (x) * 2 == x,
                "half(%d) * 2 = %d, expected %d", x, half (x) * 2, x);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing hegel_assume...\n");
  hegel_run_test (testAssume);
  printf ("PASSED\n");

  return (0);
}
