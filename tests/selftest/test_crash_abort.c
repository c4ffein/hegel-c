/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: fork isolation catches abort().
**
** The function calls abort() when x == 42.
** In fork mode, hegel should catch the SIGABRT and report it.
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
void
testAbort (
hegel_testcase *            tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 100);

  if (x == 42)
    abort ();

  HEGEL_ASSERT (x != 42, "x=%d", x); /* redundant, but clear */
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing fork isolation: abort...\n");
  hegel_run_test (testAbort);
  printf ("BUG: should not reach here\n");

  return (1);
}
