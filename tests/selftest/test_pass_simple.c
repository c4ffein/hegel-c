/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a trivially true property passes cleanly.
**
** Layer 1: add() returns a + b.
** Layer 2: hegel draws x and asserts add(x, 0) == x (additive identity).
**          This test should PASS (sanity check that the runner works at all).
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Simple addition — correct for all inputs (no overflow in identity test). */

static
int
add (
int                         a,
int                         b)
{
  return (a + b);
}

/* ---- Layer 2: hegel test ---- */

static
void
testIdentity (
hegel_testcase *            tc)
{
  int                 x;

  x = hegel_draw_int (tc, INT_MIN, INT_MAX);
  HEGEL_ASSERT (add (x, 0) == x,
                "add(%d, 0) != %d", x, x);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

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
