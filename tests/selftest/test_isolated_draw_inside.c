/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: drawing inside a HEGEL_ISOLATED block is reported as a health
**       failure (the test setup is broken, not the code under test).
**
** Layer 1: identity() — trivially correct; the test is about misuse.
** Layer 2: draws x inside the block, which the runtime must refuse.
**
** Expected: EXIT NON-ZERO with "Health check failure" naming the block.
*/
#include <stdio.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
int
identity (
int                         x)
{
  return (x);
}

/* ---- Layer 2: hegel test ---- */

static
void
testDrawInside (
hegel_testcase *            tc)
{
  HEGEL_ISOLATED (tc) {
    int               x;

    x = hegel_draw_int (tc, 0, 10);  /* misuse: draw inside the block */
    HEGEL_ASSERT (identity (x) == x, "identity broke");
  }
}

/* ---- Layer 3: runner (see Makefile TESTS_HEALTH) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing draw inside HEGEL_ISOLATED (nofork)...\n");
  hegel_run_test_nofork_n (testDrawInside, 20);
  printf ("BUG: should not reach here\n");

  return (1);
}
