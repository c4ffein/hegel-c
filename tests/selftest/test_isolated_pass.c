/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_ISOLATED blocks that pass, under both runners.
**
** Layer 1: reverse_in_place() reverses an int array (correct).
** Layer 2: draw an array, then — inside an isolated block — reverse it
**          twice and check it round-trips.  Exercises: values drawn
**          before the block visible inside it, notes and asserts inside
**          the block, drawing again AFTER the block, and several blocks
**          per test case.  Run under nofork (block forks) and fork
**          (block runs inline in the case's child).
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
void
reverse_in_place (
int *                       a,
int                         n)
{
  int                 i;
  int                 t;

  for (i = 0; i < n / 2; i ++) {
    t = a[i];
    a[i] = a[n - 1 - i];
    a[n - 1 - i] = t;
  }
}

/* ---- Layer 2: hegel test ---- */

static
void
testIsolatedPass (
hegel_testcase *            tc)
{
  int                 a[16];
  int                 orig[16];
  int                 n;
  int                 i;
  int                 extra;

  n = hegel_draw_int (tc, 0, 16);
  for (i = 0; i < n; i ++)
    a[i] = hegel_draw_int (tc, -100, 100);
  memcpy (orig, a, sizeof a);

  HEGEL_ISOLATED (tc) {
    hegel_note (tc, "inside the first isolated block");
    reverse_in_place (a, n);
    reverse_in_place (a, n);
    HEGEL_ASSERT (memcmp (a, orig, (size_t) n * sizeof (int)) == 0,
                  "double reverse changed the array (n=%d)", n);
  }

  /* Drawing after a block is fine. */
  extra = hegel_draw_int (tc, 0, 3);

  HEGEL_ISOLATED (tc) {
    HEGEL_ASSERT (extra >= 0 && extra <= 3, "extra out of range: %d", extra);
  }
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing passing HEGEL_ISOLATED blocks (nofork, then fork)...\n");
  if (hegel_run_test_result_n (testIsolatedPass, 50) != 0)
    return (1);
  hegel_run_test_nofork_n (testIsolatedPass, 50);
  printf ("OK\n");

  return (0);
}
