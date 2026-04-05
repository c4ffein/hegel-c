/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a trivially true property passes cleanly.
**
** Property: x + 0 == x for all int x.
** This test should PASS (sanity check that the runner works at all).
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

static
void
testIdentity (
hegel_testcase *            tc)
{
  int                 x;

  x = hegel_draw_int (tc, INT_MIN, INT_MAX);
  HEGEL_ASSERT (x + 0 == x,
                "x + 0 != x for x=%d", x);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing trivial pass...\n");
  hegel_run_test (testIdentity);
  printf ("PASSED\n");

  return (0);
}
