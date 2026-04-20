/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_ONE_OF — both branches reachable + correct sub-schema dispatch.
**
** Two sub-tests, each running N cases in nofork mode so a file-scope
** counter can aggregate across cases:
**
**   1. Range — HEGEL_ONE_OF(int_range(0, 10), int_range(1000, 9999)).
**      Asserts both branches are exercised and that each value falls
**      in the expected disjoint range.  The disjointness lets the
**      counter split cases by value range.
**
**   2. Pinned — HEGEL_ONE_OF(int_range(0, 0), int_range(1, 1)).
**      Same branch check, plus: every value is exactly 0 or exactly 1.
**      The 1-value ranges turn this into a sharp plumbing assertion —
**      a bug that picked the wrong sub-schema or wrote a corrupted
**      value would fail here even if a range-based test still passed.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

#define N_CASES   2

/* ---- Test 1: disjoint ranges ---- */

static hegel_schema_t  range_schema;
static int             range_total;
static int             range_low;     /* value in [0, 10]       */
static int             range_high;    /* value in [1000, 9999]  */

static
void
testOneOfRange (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, range_schema);

  range_total++;
  if (result >= 0 && result <= 10) {
    range_low++;
  } else if (result >= 1000 && result <= 9999) {
    range_high++;
  } else {
    HEGEL_ASSERT (0,
                  "one_of range produced %d, not in [0,10] or [1000,9999]",
                  result);
  }

  hegel_shape_free (sh);
}

/* ---- Test 2: pinned values 0 and 1 ---- */

static hegel_schema_t  pinned_schema;
static int             pinned_total;
static int             pinned_zero;
static int             pinned_one;

static
void
testOneOfPinned (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, pinned_schema);

  pinned_total++;
  if (result == 0) {
    pinned_zero++;
  } else if (result == 1) {
    pinned_one++;
  } else {
    HEGEL_ASSERT (0,
                  "one_of pinned produced %d, expected 0 or 1",
                  result);
  }

  hegel_shape_free (sh);
}

/* ---- Runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  range_schema = HEGEL_ONE_OF (
      HEGEL_INT (0,    10),
      HEGEL_INT (1000, 9999));
  pinned_schema = HEGEL_ONE_OF (
      HEGEL_INT (0, 0),
      HEGEL_INT (1, 1));

  printf ("Testing HEGEL_ONE_OF (disjoint ranges)...\n");
  hegel_run_test_nofork_n (testOneOfRange, N_CASES);
  if (range_low == 0 || range_high == 0) {
    fprintf (stderr,
             "one_of range collapsed: %d low / %d high / %d total\n",
             range_low, range_high, range_total);
    return (1);
  }
  printf ("  PASSED\n");

  printf ("Testing HEGEL_ONE_OF (pinned 0 vs 1)...\n");
  hegel_run_test_nofork_n (testOneOfPinned, N_CASES);
  if (pinned_zero == 0 || pinned_one == 0) {
    fprintf (stderr,
             "one_of pinned collapsed: %d zeros / %d ones / %d total\n",
             pinned_zero, pinned_one, pinned_total);
    return (1);
  }
  printf ("  PASSED\n");

  hegel_schema_free (range_schema);
  hegel_schema_free (pinned_schema);
  return (0);
}
