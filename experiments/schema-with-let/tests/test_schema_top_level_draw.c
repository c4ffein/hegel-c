/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: top-level HEGEL_DRAW + typed by-value scalar draws.
**
** Exercises the top-level entry-point patterns:
**
**   1. HEGEL_DRAW_INT (lo, hi)                   — direct primitive
**      dispatch, no schema allocation, returns by value.
**   2. MyStruct *p; HEGEL_DRAW (&p, struct_sch)  — allocating path,
**      pointer lands at &p, returned shape owns the allocation.
**
** The HEGEL_DRAW (&x, scalar_schema) pattern (scalar by-reference via
** a reused schema) is exercised by test_schema_ints.c and
** test_schema_floats.c — not re-tested here.
**
** Layer 1 function under test: check_in_range — standalone predicate.
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: functions under test ---- */

static
int
check_in_range (
int                         x,
int                         lo,
int                         hi)
{
  return (x >= lo && x <= hi);
}

static
int
check_bool_value (
bool                        b)
{
  int v = b ? 1 : 0;
  return (v == 0 || v == 1);
}

/* ---- Layer 2a: HEGEL_DRAW_INT (inline one-shot) ---- */

static
void
testDrawIntInline (
hegel_testcase *            tc)
{
  int                  x = HEGEL_DRAW_INT (0, 10);

  HEGEL_ASSERT (check_in_range (x, 0, 10),
                "HEGEL_DRAW_INT produced out-of-range %d", x);
}

/* ---- Layer 2b: HEGEL_DRAW_I64 + HEGEL_DRAW_DOUBLE + HEGEL_DRAW_BOOL ---- */

static
void
testDrawTypedInline (
hegel_testcase *            tc)
{
  int64_t              a = HEGEL_DRAW_I64    (-1000, 1000);
  double               d = HEGEL_DRAW_DOUBLE (0.0, 1.0);
  bool                 b = HEGEL_DRAW_BOOL   ();

  HEGEL_ASSERT (a >= -1000 && a <= 1000, "i64 out of range: %ld", (long) a);
  HEGEL_ASSERT (d >= 0.0 && d <= 1.0,    "double out of range: %f", d);
  HEGEL_ASSERT (check_bool_value (b),    "bool garbage: %d", (int) b);
}

/* ---- Layer 2c: HEGEL_DRAW (&p, struct_schema) allocating path ---- */

typedef struct {
  int               a;
  int64_t           b;
  double            c;
} Triplet;

static hegel_schema_t  triplet_schema;

static
void
testDrawStruct (
hegel_testcase *            tc)
{
  Triplet *            p = NULL;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&p, triplet_schema);

  HEGEL_ASSERT (p != NULL, "struct pointer is NULL");
  HEGEL_ASSERT (check_in_range (p->a, 0, 10),
                "Triplet.a out of range: %d", p->a);
  HEGEL_ASSERT (p->b >= 0 && p->b <= 1000,
                "Triplet.b out of range: %ld", (long) p->b);
  HEGEL_ASSERT (p->c >= 0.0 && p->c <= 1.0,
                "Triplet.c out of range: %f", p->c);

  hegel_shape_free (sh);
}

/* ---- Layer 3: runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  triplet_schema = HEGEL_STRUCT (Triplet,
      HEGEL_INT    (0, 10),
      HEGEL_I64    (0, 1000),
      HEGEL_DOUBLE (0.0, 1.0));

  printf ("  HEGEL_DRAW_INT inline...\n");
  hegel_run_test (testDrawIntInline);
  printf ("    PASSED\n");

  printf ("  HEGEL_DRAW_I64 / _DOUBLE / _BOOL inline...\n");
  hegel_run_test (testDrawTypedInline);
  printf ("    PASSED\n");

  printf ("  HEGEL_DRAW (&p, struct schema)...\n");
  hegel_run_test (testDrawStruct);
  printf ("    PASSED\n");

  hegel_schema_free (triplet_schema);
  return (0);
}
