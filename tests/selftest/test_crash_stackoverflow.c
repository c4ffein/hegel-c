/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: fork isolation catches stack overflow.
**
** Layer 1: stack_heavy() allocates ~64 MB on the stack for large inputs.
** Layer 2: hegel draws x in [0, 20] and calls stack_heavy().
**          Uses alloca (not recursion) so shrinking stays fast.
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <alloca.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Allocates a large stack buffer for inputs > 5.
** Bug: 64 MB exceeds any reasonable stack limit — crashes. */

static volatile char sink;

static
void
stack_heavy (
int                         x)
{
  volatile char *     p;

  if (x > 5) {
    p = (volatile char *) alloca (64 * 1024 * 1024);
    p[0] = 42;
    sink = p[0];
  }
}

/* ---- Layer 2: hegel test ---- */

static
void
testStackOverflow (
hegel_testcase *            tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 20);
  stack_heavy (x);
}

/* ---- Layer 3: runner (see Makefile TESTS_CRASH) ---- */

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
