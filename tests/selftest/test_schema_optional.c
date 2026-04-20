/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_OPTIONAL — both branches reachable + inner value preserved.
**
** Two sub-tests, each running N cases in nofork mode so a file-scope
** counter can aggregate across cases:
**
**   1. Range — HEGEL_OPTIONAL(int_range(10, 20)).  Asserts both the
**      present and absent branches are exercised, and that present
**      values land in [10, 20].
**
**   2. Pinned — HEGEL_OPTIONAL(int_range(42, 42)).  Same branch check,
**      plus: present values must equal 42 exactly.  The 1-value range
**      turns this into a sharp plumbing assertion — a bug that shifts,
**      truncates, or reads from the wrong offset would fail here even
**      though the range-based test might still pass.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

#define N_CASES   2

typedef struct {
  int *               maybe_val;
} OptInt;

/* ---- Test 1: range [10, 20] ---- */

static hegel_schema_t  range_schema;
static int             range_total;
static int             range_present;

static
void
testOptionalRange (
hegel_testcase *            tc)
{
  OptInt *             t;
  hegel_shape *        sh;

  sh = hegel_schema_draw (tc, range_schema, (void **) &t);

  range_total++;
  if (t->maybe_val != NULL) {
    range_present++;
    HEGEL_ASSERT (*t->maybe_val >= 10 && *t->maybe_val <= 20,
                  "range optional produced %d, expected [10,20]",
                  *t->maybe_val);
  }

  hegel_shape_free (sh);
}

/* ---- Test 2: pinned to 42 ---- */

static hegel_schema_t  pinned_schema;
static int             pinned_total;
static int             pinned_present;

static
void
testOptionalPinned (
hegel_testcase *            tc)
{
  OptInt *             t;
  hegel_shape *        sh;

  sh = hegel_schema_draw (tc, pinned_schema, (void **) &t);

  pinned_total++;
  if (t->maybe_val != NULL) {
    pinned_present++;
    HEGEL_ASSERT (*t->maybe_val == 42,
                  "pinned optional present but value=%d, expected 42",
                  *t->maybe_val);
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

  range_schema  = HEGEL_STRUCT (OptInt,
      HEGEL_OPTIONAL (HEGEL_INT (10, 20)));
  pinned_schema = HEGEL_STRUCT (OptInt,
      HEGEL_OPTIONAL (HEGEL_INT (42, 42)));

  printf ("Testing HEGEL_OPTIONAL (range [10,20])...\n");
  hegel_run_test_nofork_n (testOptionalRange, N_CASES);
  if (range_present == 0 || range_present == range_total) {
    fprintf (stderr,
             "range optional collapsed: %d/%d present\n",
             range_present, range_total);
    return (1);
  }
  printf ("  PASSED\n");

  printf ("Testing HEGEL_OPTIONAL (pinned to 42)...\n");
  hegel_run_test_nofork_n (testOptionalPinned, N_CASES);
  if (pinned_present == 0 || pinned_present == pinned_total) {
    fprintf (stderr,
             "pinned optional collapsed: %d/%d present\n",
             pinned_present, pinned_total);
    return (1);
  }
  printf ("  PASSED\n");

  hegel_schema_free (range_schema);
  hegel_schema_free (pinned_schema);
  return (0);
}
