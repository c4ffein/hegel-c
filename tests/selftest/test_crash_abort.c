/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: fork isolation catches abort().
**
** Layer 1: check_input() aborts when x == 42 (overzealous validation).
** Layer 2: hegel draws x in [0, 100] and calls check_input().
**          Fork mode should catch the SIGABRT and report it.
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Validates input.  Bug: calls abort() on a valid input (42)
** instead of returning an error code. */

static
void
check_input (
int                         x)
{
  if (x == 42)
    abort ();
}

/* ---- Layer 2: hegel test ---- */

static
void
testAbort (
hegel_testcase *            tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 100);
  check_input (x);

  HEGEL_ASSERT (x >= 0 && x <= 100,
                "x=%d out of range", x);
}

/* ---- Layer 3: runner (see Makefile TESTS_CRASH) ---- */

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
