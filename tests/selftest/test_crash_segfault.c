/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: fork isolation catches segfaults.
**
** The function dereferences a null pointer when x == 0.
** In fork mode, hegel should catch the crash (SIGSEGV), report it,
** and continue shrinking. The test runner itself must NOT crash.
**
** Expected: EXIT NON-ZERO (hegel reports the crash as a test failure).
*/
#include <stdio.h>

#include "hegel_c.h"

static volatile int sink;

static
void
testSegfault (
hegel_testcase *            tc)
{
  int                 x;
  int *               ptr;

  x = hegel_draw_int (tc, 0, 100);

  if (x == 0) {
    ptr = (int *) 0;
    sink = *ptr; /* SIGSEGV */
  }

  HEGEL_ASSERT (x >= 0, "x=%d", x); /* always true, but we never get here if x==0 */
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing fork isolation: segfault...\n");
  hegel_run_test (testSegfault);
  /* If we reach here, fork mode caught the crash */
  printf ("BUG: should not reach here\n");

  return (1);
}
