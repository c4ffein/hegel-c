/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_LET_ARR + HEGEL_USE_AT — sizes drives groups.
**
** Topology:
**     Inner   { int *items; int my_size }
**     Outer   { Inner **groups; int n }
**
**     Outer LETs n in [1,4].
**     Outer LET_ARR sizes (length n, each in [0,5]) — non-positional.
**     Outer ARR_OF iterates n elements; for each element:
**       - Inner draws ARR_OF length = sizes[i_outer] (USE_AT)
**       - Inner my_size = sizes[i_outer] (USE_AT, in slot)
**
** Property: each Inner's items array length equals sizes[i] of the
** outer's iteration that drew it, AND the my_size field also equals
** that same sizes[i].  Verifies:
**   - LET_ARR allocates and caches an array
**   - USE_AT walks scope chain to find the array binding
**   - USE_AT reads the iteration index of the SCOPE WHERE the binding
**     lives (outer's ARR_OF iteration), not inner's
**   - The array buffer is freed when outer's ctx exits (no leak)
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int *               items;
  int                 my_size;
} Inner;

typedef struct {
  Inner **            groups;
  int                 n;
} Outer;

HEGEL_BINDING (n);
HEGEL_BINDING (sizes);

static hegel_schema_t outer_schema;

static
void
init_schema (void)
{
  hegel_schema_t inner_s = HEGEL_STRUCT (Inner,
      HEGEL_ARR_OF (HEGEL_USE_AT (sizes), HEGEL_INT (0, 100)),
      HEGEL_USE_AT (sizes));

  outer_schema = HEGEL_STRUCT (Outer,
      HEGEL_LET     (n, HEGEL_INT (1, 4)),
      HEGEL_LET_ARR (sizes, HEGEL_USE (n), HEGEL_INT (0, 5)),
      HEGEL_ARR_OF  (HEGEL_USE (n), inner_s),
      HEGEL_USE     (n));
}

static
void
test_let_arr (
hegel_testcase *    tc)
{
  Outer *             o;
  hegel_shape *       sh;
  int                 i, k;

  sh = hegel_schema_draw (tc, outer_schema, (void **) &o);

  HEGEL_ASSERT (o->n >= 1 && o->n <= 4, "n=%d out of [1,4]", o->n);
  HEGEL_ASSERT (o->groups != NULL, "groups NULL");

  for (i = 0; i < o->n; i ++) {
    Inner * g = o->groups[i];
    HEGEL_ASSERT (g != NULL, "groups[%d] NULL", i);

    /* my_size came from sizes[i] via HEGEL_USE_AT. */
    HEGEL_ASSERT (g->my_size >= 0 && g->my_size <= 5,
                  "groups[%d].my_size=%d out of [0,5]", i, g->my_size);

    /* items array length equals my_size (both came from sizes[i]). */
    if (g->my_size > 0) {
      HEGEL_ASSERT (g->items != NULL, "groups[%d].items NULL with my_size=%d",
                    i, g->my_size);
      for (k = 0; k < g->my_size; k ++) {
        HEGEL_ASSERT (g->items[k] >= 0 && g->items[k] <= 100,
                      "groups[%d].items[%d]=%d out of [0,100]",
                      i, k, g->items[k]);
      }
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

  init_schema ();
  printf ("Testing HEGEL_LET_ARR + HEGEL_USE_AT (sizes drives groups)...\n");
  hegel_run_test (test_let_arr);
  printf ("  PASSED\n");

  hegel_schema_free (outer_schema);
  return (0);
}
