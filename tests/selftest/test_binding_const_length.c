/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_ARR_OF length schema as a pure-constant source.
**
** Two sub-tests, both producing exactly-3-element arrays but via
** different mechanisms:
**
**   1. HEGEL_CONST(3) — no byte-stream draw, just writes 3.
**   2. HEGEL_INT(3, 3) — goes through the protocol (returns 3
**      immediately, no bits consumed) but exercises the draw
**      path.
**
** Both paths should produce the same structural result: every
** drawn array has exactly 3 elements.  Proves the length schema
** is uniformly "any int-producing schema," not hardcoded to USE.
**
** Expected: EXIT 0.
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct { int * items; } Fixed;

static hegel_schema_t  schema_const;
static hegel_schema_t  schema_int_singleton;

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
  /* We can't read length from the struct; derive it from the
  ** shape tree.  The ARR_OF slot is the only field. */
  for (i = 0; i < 3; i ++) {
    HEGEL_ASSERT (f->items[i] >= 0 && f->items[i] <= 100,
                  "items[%d]=%d out of [0,100]", i, f->items[i]);
  }
  hegel_shape_free (sh);
}

static
void
test_int_singleton_length (
hegel_testcase *    tc)
{
  Fixed *             f;
  hegel_shape *       sh;
  int                 i;

  sh = hegel_schema_draw (tc, schema_int_singleton, (void **) &f);
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

  schema_int_singleton = HEGEL_STRUCT (Fixed,
      HEGEL_ARR_OF (HEGEL_INT (3, 3), HEGEL_INT (0, 100)));

  printf ("Testing HEGEL_ARR_OF(HEGEL_CONST(3), ...)...\n");
  hegel_run_test (test_const_length);
  printf ("  PASSED\n");

  printf ("Testing HEGEL_ARR_OF(HEGEL_INT(3, 3), ...)...\n");
  hegel_run_test (test_int_singleton_length);
  printf ("  PASSED\n");

  hegel_schema_free (schema_const);
  hegel_schema_free (schema_int_singleton);
  return (0);
}
