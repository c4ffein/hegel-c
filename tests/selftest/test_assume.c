/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_assume discards test cases without counting as failure.
**
** Property: draw x in [0, 100], assume x is even, then assert x is even.
** The assume should discard odd values, so the assertion always holds.
** This test should PASS.
*/
#include <stdio.h>

#include "hegel_c.h"

static
void
testAssume (
hegel_testcase *            tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 100);
  hegel_assume (tc, x % 2 == 0); /* discard odd x */

  HEGEL_ASSERT (x % 2 == 0,
                "x=%d is odd — assume should have discarded this", x);
}

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
