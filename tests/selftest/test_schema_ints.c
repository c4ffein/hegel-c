/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_I8..I64, HEGEL_U8..U64, HEGEL_INT, HEGEL_LONG.
**
** Ten integer widths × two sub-tests each, all in nofork mode so
** file-scope counters can aggregate across cases:
**
**   Range  — HEGEL_X(lo, hi).  Assert each draw is in [lo, hi] and
**            at least two distinct values are seen across N cases.
**   Pinned — HEGEL_X(v, v).  Every draw must equal v exactly.
**
** Verifies the schema wrapper's width/offset plumbing for each integer
** type.  from-hegel-rust/test_integers_bounds.c tests the primitive
** API comprehensively (including corner-value find); this file covers
** the schema-API layer on top without duplicating that matrix.
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

/* Generate the per-width test functions, schemas, counters, and bounds
** storage.  PCAST / PFMT are used only in error-message formatting. */
#define INT_SUBTESTS(pfx, T, PCAST, PFMT)                                  \
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
                  #pfx " range: x=%" PFMT, (PCAST) x);                     \
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
                  #pfx " pinned: x=%" PFMT, (PCAST) x);                    \
    hegel_shape_free (sh);                                                 \
  }

INT_SUBTESTS (i8,    int8_t,   long long,          "lld")
INT_SUBTESTS (i16,   int16_t,  long long,          "lld")
INT_SUBTESTS (i32,   int32_t,  long long,          "lld")
INT_SUBTESTS (i64,   int64_t,  long long,          "lld")
INT_SUBTESTS (iplat, int,      long long,          "lld")
INT_SUBTESTS (lplat, long,     long long,          "lld")
INT_SUBTESTS (u8,    uint8_t,  unsigned long long, "llu")
INT_SUBTESTS (u16,   uint16_t, unsigned long long, "llu")
INT_SUBTESTS (u32,   uint32_t, unsigned long long, "llu")
INT_SUBTESTS (u64,   uint64_t, unsigned long long, "llu")

#define SETUP(pfx, MAC, LO, HI, PV)                                        \
  do {                                                                     \
    pfx##_range_lo   = (LO);                                               \
    pfx##_range_hi   = (HI);                                               \
    pfx##_pinned_val = (PV);                                               \
    pfx##_range_s    = MAC ((LO), (HI));                                   \
    pfx##_pinned_s   = MAC ((PV), (PV));                                   \
  } while (0)

#define RUN_WIDTH(pfx)                                                     \
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

  SETUP (i8,    HEGEL_I8,     -10,           10,          -5);
  SETUP (i16,   HEGEL_I16,    -1000,         1000,        -300);
  SETUP (i32,   HEGEL_I32,    -100000,       100000,      -70000);
  SETUP (i64,   HEGEL_I64,    -100000LL,     100000LL,     5000000000LL);
  SETUP (iplat, HEGEL_INT,    -100000,       100000,      -42);
  SETUP (lplat, HEGEL_LONG,   -100000L,      100000L,     -99999L);
  SETUP (u8,    HEGEL_U8,     0,             200,          7);
  SETUP (u16,   HEGEL_U16,    0,             1000,         500);
  SETUP (u32,   HEGEL_U32,    0,             100000,       100000);
  SETUP (u64,   HEGEL_U64,    0ULL,          100000ULL,    10000000000ULL);

  printf ("Testing HEGEL integer widths (range + pinned)...\n");
  RUN_WIDTH (i8);
  RUN_WIDTH (i16);
  RUN_WIDTH (i32);
  RUN_WIDTH (i64);
  RUN_WIDTH (iplat);
  RUN_WIDTH (lplat);
  RUN_WIDTH (u8);
  RUN_WIDTH (u16);
  RUN_WIDTH (u32);
  RUN_WIDTH (u64);
  printf ("PASSED\n");

  hegel_schema_free (i8_range_s);    hegel_schema_free (i8_pinned_s);
  hegel_schema_free (i16_range_s);   hegel_schema_free (i16_pinned_s);
  hegel_schema_free (i32_range_s);   hegel_schema_free (i32_pinned_s);
  hegel_schema_free (i64_range_s);   hegel_schema_free (i64_pinned_s);
  hegel_schema_free (iplat_range_s); hegel_schema_free (iplat_pinned_s);
  hegel_schema_free (lplat_range_s); hegel_schema_free (lplat_pinned_s);
  hegel_schema_free (u8_range_s);    hegel_schema_free (u8_pinned_s);
  hegel_schema_free (u16_range_s);   hegel_schema_free (u16_pinned_s);
  hegel_schema_free (u32_range_s);   hegel_schema_free (u32_pinned_s);
  hegel_schema_free (u64_range_s);   hegel_schema_free (u64_pinned_s);
  return (0);
}
