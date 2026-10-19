/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a collection-built list shrinks by element deletion.
**
** Layer 1: sum_fits_byte() — claims the sum of a list always fits in
**          a byte.  Bug: it doesn't check.
** Layer 2: draw a list of ints in [0, 100] with the collection protocol
**          and assert the claim.  The minimal counterexample needs a sum
**          of 256: three elements (100, 100, 56) — the engine deletes
**          the rest and minimises what is left.
**
** Expected: EXIT NON-ZERO; the report's notes show a 3-element list.
*/
#include <stdio.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
int
sum_fits_byte (
const int *                 xs,
int                         n)
{
  int                 s;
  int                 i;

  s = 0;
  for (i = 0; i < n; i ++)
    s += xs[i];
  return (s);   /* bug: callers assume the result is < 256 */
}

/* ---- Layer 2: hegel test ---- */

static
void
testCollectionShrink (
hegel_testcase *            tc)
{
  int                 xs[64];
  int                 n;
  char                note[64];
  hegel_collection    c;

  n = 0;
  c = hegel_collection_new (tc, 0, 64);
  while (hegel_collection_more (tc, c))
    xs[n ++] = hegel_draw_int (tc, 0, 100);
  hegel_collection_free (tc, c);

  snprintf (note, sizeof note, "list length = %d", n);
  hegel_note (tc, note);
  HEGEL_ASSERT (sum_fits_byte (xs, n) < 256, "sum of %d elements overflows a byte", n);
}

/* ---- Layer 3: runner (see Makefile TESTS_FAIL) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing collection shrinking...\n");
  hegel_run_test_n (testCollectionShrink, 300);
  printf ("BUG: should not reach here\n");

  return (1);
}
