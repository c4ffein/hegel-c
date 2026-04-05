/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: fork isolation catches segfaults.
**
** Layer 1: deref() dereferences a pointer with no NULL check.
** Layer 2: hegel draws x in [0, 100] and passes a NULL pointer when
**          x == 0.  Fork mode should catch the SIGSEGV and continue
**          shrinking.
**
** Expected: EXIT NON-ZERO (hegel reports the crash as a test failure).
*/
#include <stdio.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Returns the value at *p.  Bug: no NULL check — crashes on NULL. */

static
int
deref (
const int *                 p)
{
  return (*p);
}

/* ---- Layer 2: hegel test ---- */

static volatile int sink;

static
void
testSegfault (
hegel_testcase *            tc)
{
  int                 x;
  int *               ptr;

  x = hegel_draw_int (tc, 0, 100);

  ptr = (x == 0) ? (int *) 0 : &x;
  sink = deref (ptr);
}

/* ---- Layer 3: runner (see Makefile TESTS_CRASH) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing fork isolation: segfault...\n");
  hegel_run_test (testSegfault);
  printf ("BUG: should not reach here\n");

  return (1);
}
