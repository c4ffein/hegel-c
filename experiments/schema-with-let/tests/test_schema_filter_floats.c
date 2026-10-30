/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_FILTER_DOUBLE — predicate enforcement, source range
** preservation, variation, rejection-path stress.
**
** Three sub-tests in nofork mode so file-scope counters can aggregate
** across cases:
**
**   Signed range + variation — FILTER_DOUBLE(double(-100, 100), |v| v > 0.0).
**     Asserts each result satisfies the predicate, stays in [-100, 100],
**     and at least two distinct values are seen.  Signed range exercises
**     negative-value handling through the filter.  ~50% rejection rate.
**   Narrow keep — FILTER_DOUBLE(double(0, 1), |v| v >= 0.5).  Higher
**     rejection rate stresses the reject-and-retry path without tripping
**     filter_too_much.
**   Pinned — FILTER_DOUBLE(double(3.14, 3.14), v == 3.14).  Every kept
**     value must equal 3.14 exactly — sharp plumbing check.
**
** Only DOUBLE is in the public filter API — HEGEL_FILTER_FLOAT doesn't
** exist today.  from-hegel-rust/test_floats_*_bounds.c and
** test_schema_functional_combinators.c Part 3 also exercise float /
** double generation; this file is the dedicated schema-API surface
** for double filter.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

#define N_CASES   4

/* ---- Predicates ---- */

static int d_positive (double v, void * c) { (void) c; return (v > 0.0); }
static int d_ge_half  (double v, void * c) { (void) c; return (v >= 0.5); }
static int d_eq_pi    (double v, void * c) { (void) c; return (v == 3.14); }

/* ---- State ---- */

static hegel_schema_t  d_range_s;
static hegel_schema_t  d_narrow_s;
static hegel_schema_t  d_pinned_s;
static double          d_min;
static double          d_max;
static int             d_range_total;
static int             d_narrow_total;
static int             d_pinned_total;

/* ---- Tests ---- */

static
void
testDoubleRange (
hegel_testcase *            tc)
{
  double               x;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&x, d_range_s);
  d_range_total++;
  HEGEL_ASSERT (x > 0.0,   "double range pred violated: x=%g", x);
  HEGEL_ASSERT (x >= -100.0 && x <= 100.0,
                "double range out of bounds: x=%g", x);
  if (d_range_total == 1) { d_min = x; d_max = x; }
  else {
    if (x < d_min) d_min = x;
    if (x > d_max) d_max = x;
  }
  hegel_shape_free (sh);
}

static
void
testDoubleNarrow (
hegel_testcase *            tc)
{
  double               x;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&x, d_narrow_s);
  d_narrow_total++;
  HEGEL_ASSERT (x >= 0.5, "double narrow pred violated: x=%g", x);
  HEGEL_ASSERT (x >= 0.0 && x <= 1.0,
                "double narrow out of bounds: x=%g", x);
  hegel_shape_free (sh);
}

static
void
testDoublePinned (
hegel_testcase *            tc)
{
  double               x;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&x, d_pinned_s);
  d_pinned_total++;
  HEGEL_ASSERT (x == 3.14, "double pinned: x=%g", x);
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

  d_range_s  = HEGEL_FILTER_DOUBLE (HEGEL_DOUBLE (-100.0, 100.0),
                                    d_positive, NULL);
  d_narrow_s = HEGEL_FILTER_DOUBLE (HEGEL_DOUBLE (0.0, 1.0),
                                    d_ge_half,  NULL);
  d_pinned_s = HEGEL_FILTER_DOUBLE (HEGEL_DOUBLE (3.14, 3.14),
                                    d_eq_pi,    NULL);

  printf ("Testing HEGEL_FILTER_DOUBLE...\n");

  printf ("  range...\n");
  hegel_run_test_nofork_n (testDoubleRange, N_CASES);
  if (d_range_total > 1 && d_min == d_max) {
    fprintf (stderr, "  FAIL: double range no variation across %d cases\n",
             d_range_total);
    return (1);
  }

  printf ("  narrow...\n");
  hegel_run_test_nofork_n (testDoubleNarrow, N_CASES);

  printf ("  pinned...\n");
  hegel_run_test_nofork_n (testDoublePinned, N_CASES);

  printf ("PASSED\n");

  hegel_schema_free (d_range_s);
  hegel_schema_free (d_narrow_s);
  hegel_schema_free (d_pinned_s);
  return (0);
}
