/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a HEGEL_ASSERT failing INSIDE a HEGEL_ISOLATED block is an
**       ordinary shrinkable failure (nofork runner: the verdict travels
**       back from the block's child and is re-raised in the parent).
**
** Layer 1: clamp_add() adds two bytes with saturation — bug: wraps
**          instead of saturating once the sum exceeds 255.
** Layer 2: draw a and b, check clamp_add inside the block.
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
unsigned char
clamp_add (
unsigned char               a,
unsigned char               b)
{
  return ((unsigned char) (a + b));  /* bug: no saturation */
}

/* ---- Layer 2: hegel test ---- */

static
void
testIsolatedAssert (
hegel_testcase *            tc)
{
  int                 a;
  int                 b;

  a = hegel_draw_int (tc, 0, 255);
  b = hegel_draw_int (tc, 0, 255);
  HEGEL_ISOLATED (tc) {
    unsigned char     r;

    r = clamp_add ((unsigned char) a, (unsigned char) b);
    HEGEL_ASSERT (r >= a && r >= b, "clamp_add(%d, %d) = %d", a, b, r);
  }
}

/* ---- Layer 3: runner (see Makefile TESTS_FAIL) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing HEGEL_ASSERT inside HEGEL_ISOLATED...\n");
  hegel_run_test_nofork_n (testIsolatedAssert, 200);
  printf ("BUG: should not reach here\n");

  return (1);
}
