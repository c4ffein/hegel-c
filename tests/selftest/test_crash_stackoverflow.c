/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: fork isolation catches stack overflow.
**
** Uses alloca to blow the stack immediately rather than recursive calls,
** which avoids potential issues with each shrink attempt also stack-
** overflowing (slow death spiral).
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <alloca.h>

#include "hegel_c.h"

static volatile char sink;

static
void
testStackOverflow (
hegel_testcase *            tc)
{
  int                 x;
  volatile char *     p;

  x = hegel_draw_int (tc, 0, 20);

  if (x == 7) {
    /* Allocate ~64 MB on the stack — way past any stack limit */
    p = (volatile char *) alloca (64 * 1024 * 1024);
    p[0] = 42;
    sink = p[0];
  }
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing fork isolation: stack overflow...\n");
  hegel_run_test (testStackOverflow);
  printf ("BUG: should not reach here\n");

  return (1);
}
