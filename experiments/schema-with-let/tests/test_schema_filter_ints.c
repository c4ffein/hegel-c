/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_FILTER_INT and HEGEL_FILTER_I64 — predicate enforcement,
** source range preservation, variation, and width handling.
**
** Two integer types × three sub-tests each, all in nofork mode so
** file-scope counters can aggregate across cases:
**
**   Signed range + variation — FILTER_X(int_X(-lim, lim), |v| v % 3 == 0).
**     Asserts each result satisfies the predicate, stays in [-lim, lim],
**     and at least two distinct values are seen.  Signed range exercises
**     negative-value handling through the filter.
**   Narrow keep — FILTER_X(int_X(lo, hi), |v| v >= threshold).  Higher
**     rejection rate stresses the reject-and-retry path.  Threshold
**     chosen to keep ~50% so filter_too_much doesn't trip.
**   Pinned — FILTER_X(int_X(v, v), x == v).  Every kept value must
**     equal v exactly — a sharp plumbing check.  For I64, v is outside
**     INT32 range to exercise 64-bit width handling.
**
** Only INT and I64 are in the public filter API — HEGEL_FILTER_U64 /
** _I8 / etc. don't exist today.
**
** Expected: EXIT 0.
*/
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

#define N_CASES   4

/* ---- Predicates ---- */

static int iplat_mod3       (int     v, void * c) { (void) c; return (v % 3 == 0); }
static int iplat_ge_neg50   (int     v, void * c) { (void) c; return (v >= -50); }
static int iplat_eq42       (int     v, void * c) { (void) c; return (v == 42); }

static int i64_mod3         (int64_t v, void * c) { (void) c; return (v % 3 == 0); }
static int i64_ge_zero      (int64_t v, void * c) { (void) c; return (v >= 0); }
static int i64_eq_big       (int64_t v, void * c) { (void) c; return (v == 5000000000LL); }

/* ---- Per-type state and tests ---- */

#define FILTER_SUBTESTS(pfx, T, PCAST, PFMT)                                \
  static hegel_schema_t  pfx##_range_s;                                     \
  static hegel_schema_t  pfx##_narrow_s;                                    \
  static hegel_schema_t  pfx##_pinned_s;                                    \
  static T               pfx##_range_lo;                                    \
  static T               pfx##_range_hi;                                    \
  static T               pfx##_narrow_lo;                                   \
  static T               pfx##_narrow_hi;                                   \
  static T               pfx##_narrow_thresh;                               \
  static T               pfx##_pinned_val;                                  \
  static T               pfx##_min;                                         \
  static T               pfx##_max;                                         \
  static int             pfx##_range_total;                                 \
  static int             pfx##_narrow_total;                                \
  static int             pfx##_pinned_total;                                \
                                                                            \
  static void test##pfx##Range (hegel_testcase * tc) {                      \
    T              x;                                                       \
    hegel_shape *  sh = HEGEL_DRAW (&x, pfx##_range_s);                     \
    pfx##_range_total++;                                                    \
    HEGEL_ASSERT (x % 3 == 0,                                               \
                  #pfx " range pred violated: x=%" PFMT, (PCAST) x);        \
    HEGEL_ASSERT (x >= pfx##_range_lo && x <= pfx##_range_hi,               \
                  #pfx " range out of bounds: x=%" PFMT, (PCAST) x);        \
    if (pfx##_range_total == 1) { pfx##_min = x; pfx##_max = x; }           \
    else {                                                                  \
      if (x < pfx##_min) pfx##_min = x;                                     \
      if (x > pfx##_max) pfx##_max = x;                                     \
    }                                                                       \
    hegel_shape_free (sh);                                                  \
  }                                                                         \
                                                                            \
  static void test##pfx##Narrow (hegel_testcase * tc) {                     \
    T              x;                                                       \
    hegel_shape *  sh = HEGEL_DRAW (&x, pfx##_narrow_s);                    \
    pfx##_narrow_total++;                                                   \
    HEGEL_ASSERT (x >= pfx##_narrow_thresh,                                 \
                  #pfx " narrow pred violated: x=%" PFMT, (PCAST) x);       \
    HEGEL_ASSERT (x >= pfx##_narrow_lo && x <= pfx##_narrow_hi,             \
                  #pfx " narrow out of bounds: x=%" PFMT, (PCAST) x);       \
    hegel_shape_free (sh);                                                  \
  }                                                                         \
                                                                            \
  static void test##pfx##Pinned (hegel_testcase * tc) {                     \
    T              x;                                                       \
    hegel_shape *  sh = HEGEL_DRAW (&x, pfx##_pinned_s);                    \
    pfx##_pinned_total++;                                                   \
    HEGEL_ASSERT (x == pfx##_pinned_val,                                    \
                  #pfx " pinned: x=%" PFMT, (PCAST) x);                     \
    hegel_shape_free (sh);                                                  \
  }

FILTER_SUBTESTS (iplat, int,     long long, "lld")
FILTER_SUBTESTS (i64,   int64_t, long long, "lld")

#define RUN_TYPE(pfx)                                                       \
  do {                                                                     \
    printf ("  " #pfx " range...\n");                                       \
    hegel_run_test_nofork_n (test##pfx##Range,  N_CASES);                   \
    if (pfx##_range_total > 1 && pfx##_min == pfx##_max) {                  \
      fprintf (stderr,                                                      \
               "  FAIL: " #pfx " range no variation across %d cases\n",     \
               pfx##_range_total);                                          \
      return (1);                                                           \
    }                                                                       \
    printf ("  " #pfx " narrow...\n");                                      \
    hegel_run_test_nofork_n (test##pfx##Narrow, N_CASES);                   \
    printf ("  " #pfx " pinned...\n");                                      \
    hegel_run_test_nofork_n (test##pfx##Pinned, N_CASES);                   \
  } while (0)

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  iplat_range_lo      = -99;
  iplat_range_hi      = 99;
  iplat_narrow_lo     = -99;
  iplat_narrow_hi     = 99;
  iplat_narrow_thresh = -50;
  iplat_pinned_val    = 42;
  iplat_range_s   = HEGEL_FILTER_INT (HEGEL_INT (-99, 99),
                                      iplat_mod3,     NULL);
  iplat_narrow_s  = HEGEL_FILTER_INT (HEGEL_INT (-99, 99),
                                      iplat_ge_neg50, NULL);
  iplat_pinned_s  = HEGEL_FILTER_INT (HEGEL_INT (42, 42),
                                      iplat_eq42,     NULL);

  i64_range_lo      = -999999999LL;
  i64_range_hi      = 999999999LL;
  i64_narrow_lo     = -1000000000LL;
  i64_narrow_hi     = 1000000000LL;
  i64_narrow_thresh = 0LL;
  i64_pinned_val    = 5000000000LL;
  i64_range_s   = HEGEL_FILTER_I64 (HEGEL_I64 (-999999999LL,  999999999LL),
                                    i64_mod3,    NULL);
  i64_narrow_s  = HEGEL_FILTER_I64 (HEGEL_I64 (-1000000000LL, 1000000000LL),
                                    i64_ge_zero, NULL);
  i64_pinned_s  = HEGEL_FILTER_I64 (HEGEL_I64 (5000000000LL,  5000000000LL),
                                    i64_eq_big,  NULL);

  printf ("Testing HEGEL_FILTER integer types...\n");
  RUN_TYPE (iplat);
  RUN_TYPE (i64);
  printf ("PASSED\n");

  hegel_schema_free (iplat_range_s);
  hegel_schema_free (iplat_narrow_s);
  hegel_schema_free (iplat_pinned_s);
  hegel_schema_free (i64_range_s);
  hegel_schema_free (i64_narrow_s);
  hegel_schema_free (i64_pinned_s);
  return (0);
}
