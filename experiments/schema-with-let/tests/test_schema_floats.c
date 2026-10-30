/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_FLOAT (f32) and HEGEL_DOUBLE (f64).
**
** Two floating types × two sub-tests each, all in nofork mode so
** file-scope counters can aggregate across cases:
**
**   Range  — HEGEL_X(lo, hi).  Assert each draw is in [lo, hi] and
**            at least two distinct values are seen across N cases.
**   Pinned — HEGEL_X(v, v).  Every draw must equal v exactly.
**
** Verifies the schema wrapper's width/offset plumbing for floats.
** from-hegel-rust/test_floats_f32_bounds.c and test_floats_f64_bounds.c
** test the primitive API; this file covers the schema-API layer.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

#define N_CASES   4

#define FLOAT_SUBTESTS(pfx, T, PFMT)                                       \
  static hegel_schema_t  pfx##_range_s;                                    \
  static hegel_schema_t  pfx##_pinned_s;                                   \
  static T               pfx##_range_lo;                                   \
  static T               pfx##_range_hi;                                   \
  static T               pfx##_pinned_val;                                 \
  static T               pfx##_min;                                        \
  static T               pfx##_max;                                        \
  static int             pfx##_range_total;                                \
  static int             pfx##_pinned_total;                               \
                                                                           \
  static void test##pfx##Range (hegel_testcase * tc) {                     \
    T              x;                                                      \
    hegel_shape *  sh = HEGEL_DRAW (&x, pfx##_range_s);                    \
    pfx##_range_total++;                                                   \
    HEGEL_ASSERT (x >= pfx##_range_lo && x <= pfx##_range_hi,              \
                  #pfx " range: x=" PFMT, (double) x);                     \
    if (pfx##_range_total == 1) { pfx##_min = x; pfx##_max = x; }          \
    else {                                                                 \
      if (x < pfx##_min) pfx##_min = x;                                    \
      if (x > pfx##_max) pfx##_max = x;                                    \
    }                                                                      \
    hegel_shape_free (sh);                                                 \
  }                                                                        \
                                                                           \
  static void test##pfx##Pinned (hegel_testcase * tc) {                    \
    T              x;                                                      \
    hegel_shape *  sh = HEGEL_DRAW (&x, pfx##_pinned_s);                   \
    pfx##_pinned_total++;                                                  \
    HEGEL_ASSERT (x == pfx##_pinned_val,                                   \
                  #pfx " pinned: x=" PFMT, (double) x);                    \
    hegel_shape_free (sh);                                                 \
  }

FLOAT_SUBTESTS (f32, float,  "%g")
FLOAT_SUBTESTS (f64, double, "%g")

#define SETUP(pfx, MAC, LO, HI, PV)                                        \
  do {                                                                     \
    pfx##_range_lo   = (LO);                                               \
    pfx##_range_hi   = (HI);                                               \
    pfx##_pinned_val = (PV);                                               \
    pfx##_range_s    = MAC ((LO), (HI));                                   \
    pfx##_pinned_s   = MAC ((PV), (PV));                                   \
  } while (0)

#define RUN_TYPE(pfx)                                                      \
  do {                                                                     \
    printf ("  " #pfx "...\n");                                            \
    hegel_run_test_nofork_n (test##pfx##Range,  N_CASES);                  \
    if (pfx##_range_total > 1 && pfx##_min == pfx##_max) {                 \
      fprintf (stderr,                                                     \
               "  FAIL: " #pfx " range no variation across %d cases\n",    \
               pfx##_range_total);                                         \
      return (1);                                                          \
    }                                                                      \
    hegel_run_test_nofork_n (test##pfx##Pinned, N_CASES);                  \
  } while (0)

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  SETUP (f32, HEGEL_FLOAT,  -100.0f,    100.0f,    3.14f);
  SETUP (f64, HEGEL_DOUBLE, -1000000.0, 1000000.0, 2.718281828);

  printf ("Testing HEGEL float types (range + pinned)...\n");
  RUN_TYPE (f32);
  RUN_TYPE (f64);
  printf ("PASSED\n");

  hegel_schema_free (f32_range_s); hegel_schema_free (f32_pinned_s);
  hegel_schema_free (f64_range_s); hegel_schema_free (f64_pinned_s);
  return (0);
}
