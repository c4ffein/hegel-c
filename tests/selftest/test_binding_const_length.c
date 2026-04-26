/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_ARR_OF length — two paths to a fixed-3-element array.
**
** After the v0 hardening, raw HEGEL_INT(lo,hi) is rejected as an
** ARR_OF length (see test_arr_of_raw_int_abort.c).  The two
** *allowed* mechanisms for a known-3 length are:
**
**   1. HEGEL_CONST(3) — pure literal, no draw.
**   2. HEGEL_LET(n, HEGEL_INT(3, 3)) + HEGEL_USE(n) — singleton
**      range stashed in a binding.  Goes through the protocol
**      (returns 3 immediately, no entropy consumed) and is
**      referenceable by name.
**
** Both produce the same structural result: every drawn array has
** exactly 3 elements.  Confirms the API supports both the
** "compile-time literal" and "named-binding-with-degenerate-range"
** styles, which is the new replacement for the pre-hardening
** "raw HEGEL_INT(3,3) just works" claim.
**
** Expected: EXIT 0.
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct { int * items; } Fixed;

HEGEL_BINDING (n);

static hegel_schema_t  schema_const;
static hegel_schema_t  schema_let_singleton;

static
void
test_const_length (
hegel_testcase *    tc)
{
  Fixed *             f;
  hegel_shape *       sh;
  int                 i;

  sh = hegel_schema_draw (tc, schema_const, (void **) &f);
  HEGEL_ASSERT (f->items != NULL, "items NULL");
  for (i = 0; i < 3; i ++) {
    HEGEL_ASSERT (f->items[i] >= 0 && f->items[i] <= 100,
                  "items[%d]=%d out of [0,100]", i, f->items[i]);
  }
  hegel_shape_free (sh);
}

static
void
test_let_singleton_length (
hegel_testcase *    tc)
{
  Fixed *             f;
  hegel_shape *       sh;
  int                 i;

  sh = hegel_schema_draw (tc, schema_let_singleton, (void **) &f);
  HEGEL_ASSERT (f->items != NULL, "items NULL");
  for (i = 0; i < 3; i ++) {
    HEGEL_ASSERT (f->items[i] >= 0 && f->items[i] <= 100,
                  "items[%d]=%d out of [0,100]", i, f->items[i]);
  }
  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  schema_const = HEGEL_STRUCT (Fixed,
      HEGEL_ARR_OF (HEGEL_CONST (3), HEGEL_INT (0, 100)));

  schema_let_singleton = HEGEL_STRUCT (Fixed,
      HEGEL_LET    (n, HEGEL_INT (3, 3)),
      HEGEL_ARR_OF (HEGEL_USE (n), HEGEL_INT (0, 100)));

  printf ("Testing HEGEL_ARR_OF(HEGEL_CONST(3), ...)...\n");
  hegel_run_test (test_const_length);
  printf ("  PASSED\n");

  printf ("Testing HEGEL_ARR_OF(HEGEL_USE(n) where n=LET(INT(3,3)), ...)...\n");
  hegel_run_test (test_let_singleton_length);
  printf ("  PASSED\n");

  hegel_schema_free (schema_const);
  hegel_schema_free (schema_let_singleton);
  return (0);
}
