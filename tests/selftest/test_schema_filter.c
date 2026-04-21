/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_FILTER_INT — predicate enforced, source range preserved,
** value variation on successful draws.
**
** Two sub-tests, each running N cases in nofork mode so a file-scope
** counter can aggregate across cases:
**
**   1. Range + variation — FILTER_INT(int(0, 99), x % 3 == 0).  Asserts
**      each result satisfies the predicate, stays in [0, 99], and that
**      at least two distinct values are seen across N cases.  The
**      variation check catches a regression that returned the same
**      value every case — a bug the pure predicate assertion would miss.
**
**   2. Pinned — FILTER_INT(int(42, 42), x == 42).  Every kept value
**      must equal 42 exactly.  The 1-value source + trivially-true
**      predicate sharpens the plumbing check: a bug in the filter
**      dispatch that wrote the wrong offset, truncated, or dropped
**      the value would fail here even if a range-based test passed.
**
** Expected: EXIT 0.
*/
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

#define N_CASES   4

/* ---- Test 1: x % 3 == 0 over [0, 99] ---- */

static
int
mod3_eq0 (int val, void * ctx)
{
  (void) ctx;
  return (val % 3 == 0);
}

static hegel_schema_t  range_schema;
static int             range_total;
static int             range_min_val = INT_MAX;
static int             range_max_val = INT_MIN;

static
void
testFilterRange (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, range_schema);

  range_total++;
  HEGEL_ASSERT (result % 3 == 0,
                "filter predicate violated: %d %% 3 != 0", result);
  HEGEL_ASSERT (result >= 0 && result <= 99,
                "filter result out of source range: %d", result);
  if (result < range_min_val) range_min_val = result;
  if (result > range_max_val) range_max_val = result;

  hegel_shape_free (sh);
}

/* ---- Test 2: x == 42 over [42, 42] ---- */

static
int
eq42 (int val, void * ctx)
{
  (void) ctx;
  return (val == 42);
}

static hegel_schema_t  pinned_schema;
static int             pinned_total;

static
void
testFilterPinned (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, pinned_schema);

  pinned_total++;
  HEGEL_ASSERT (result == 42,
                "pinned filter produced %d, expected 42", result);

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

  range_schema  = HEGEL_FILTER_INT (HEGEL_INT (0, 99),  mod3_eq0, NULL);
  pinned_schema = HEGEL_FILTER_INT (HEGEL_INT (42, 42), eq42,     NULL);

  printf ("Testing HEGEL_FILTER_INT (x %% 3 == 0 over [0,99])...\n");
  hegel_run_test_nofork_n (testFilterRange, N_CASES);
  if (range_max_val <= range_min_val) {
    fprintf (stderr,
             "filter range: only value %d seen across %d cases\n",
             range_min_val, range_total);
    return (1);
  }
  printf ("  PASSED (values %d..%d in %d cases)\n",
          range_min_val, range_max_val, range_total);

  printf ("Testing HEGEL_FILTER_INT (pinned to 42)...\n");
  hegel_run_test_nofork_n (testFilterPinned, N_CASES);
  printf ("  PASSED (%d cases)\n", pinned_total);

  hegel_schema_free (range_schema);
  hegel_schema_free (pinned_schema);
  return (0);
}
