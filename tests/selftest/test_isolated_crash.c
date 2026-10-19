/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_ISOLATED catches a crash under the NOFORK runner.
**
** Layer 1: checksum() walks a buffer of `len` ints — but trusts a
**          length one past the end when the caller passes len >= 7,
**          reading through a NULL sentinel slot.
** Layer 2: generation runs in-process (nofork: draws are plain calls
**          into the engine); only the call into the code under test is
**          isolated.  The crash must become a shrinkable failure whose
**          origin names the block, and the draws made BEFORE the block
**          must be visible inside it (fork copies the address space).
**
** Expected: EXIT NON-ZERO, report says "HEGEL_ISOLATED", and the
**           counterexample shrinks to Draw 1: 7.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Sums the first `len` entries of a table of pointers.  Bug: the table
** only has 7 valid entries and the last slot is NULL. */

static int values[7] = { 1, 2, 3, 4, 5, 6, 7 };
static int * table[8] = { &values[0], &values[1], &values[2], &values[3],
                          &values[4], &values[5], &values[6], NULL };

static
int
checksum (
int                         len)
{
  int                 sum;
  int                 i;

  sum = 0;
  for (i = 0; i <= len && i < 8; i ++)  /* bug: <= */
    sum += *table[i];
  return (sum);
}

/* ---- Layer 2: hegel test ---- */

static
void
testIsolatedCrash (
hegel_testcase *            tc)
{
  int                 len;
  int                 sum;

  len = hegel_draw_int (tc, 0, 20);
  HEGEL_ISOLATED (tc) {
    sum = checksum (len);
    HEGEL_ASSERT (sum > 0, "checksum(%d) = %d", len, sum);
  }
}

/* ---- Layer 3: runner (see Makefile TESTS_CRASH) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing HEGEL_ISOLATED crash isolation (nofork runner)...\n");
  hegel_run_test_nofork_n (testIsolatedCrash, 200);
  printf ("BUG: should not reach here\n");

  return (1);
}
