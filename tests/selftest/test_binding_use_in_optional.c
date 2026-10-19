/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_OPTIONAL wrapping a struct whose fields HEGEL_USE an
** outer HEGEL_LET.
**
** The optional's inner struct is drawn via hegel__draw_alloc, which
** threads the enclosing ctx as parent — so USE sites inside the inner
** struct must resolve LETs from the struct that holds the optional.
** Was listed as "should work, but no test exercises it" (TODO.md,
** binding-system item 5).
**
** Exercises:
**   1. Inner scalar field mirrors the outer LET when present
**   2. Inner HEGEL_ARR_OF sized by the outer LET (scope walk through
**      the optional AND an array-length resolution)
**   3. hegel_shape_is_some agrees with the struct pointer
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 val;    /* HEGEL_USE (on) — mirrors outer n */
  int *               arr;    /* HEGEL_ARR_OF (USE (on), ...)     */
} Inner;

typedef struct {
  Inner *             maybe;
  int                 n;
} OBox;

HEGEL_BINDING (on);
static hegel_schema_t obox_schema;

static
void
init_obox_schema (void)
{
  hegel_schema_t inner_s = HEGEL_STRUCT (Inner,
      HEGEL_USE    (on),
      HEGEL_ARR_OF (HEGEL_USE (on), HEGEL_INT (0, 9)));
  obox_schema = HEGEL_STRUCT (OBox,
      HEGEL_LET      (on, HEGEL_INT (1, 8)),
      HEGEL_OPTIONAL (inner_s),
      HEGEL_USE      (on));
}

static
void
test_optional_use (
hegel_testcase *    tc)
{
  OBox *              b;
  hegel_shape *       sh;
  int                 is_some;
  int                 i;

  sh = hegel_schema_draw (tc, obox_schema, (void **) &b);

  HEGEL_ASSERT (b->n >= 1 && b->n <= 8, "n=%d", b->n);

  /* Positional shape fields mirror schema children: 0 = LET (NULL),
  ** 1 = the optional, 2 = USE. */
  is_some = hegel_shape_is_some (hegel_shape_field (sh, 1));
  HEGEL_ASSERT (is_some == (b->maybe != NULL),
                "shape is_some=%d but maybe=%p", is_some, (void *) b->maybe);

  if (b->maybe != NULL) {
    HEGEL_ASSERT (b->maybe->val == b->n,
                  "inner USE(on)=%d != outer n=%d", b->maybe->val, b->n);
    HEGEL_ASSERT (b->maybe->arr != NULL, "inner arr is NULL");
    /* Array length is the outer LET — elements 0..n-1 are drawn. */
    for (i = 0; i < b->n; i ++) {
      HEGEL_ASSERT (b->maybe->arr[i] >= 0 && b->maybe->arr[i] <= 9,
                    "arr[%d]=%d out of [0,9]", i, b->maybe->arr[i]);
    }
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

  init_obox_schema ();
  printf ("  USE through OPTIONAL inner struct...\n");
  hegel_run_test (test_optional_use);
  printf ("    PASSED\n");

  hegel_schema_free (obox_schema);
  return (0);
}
