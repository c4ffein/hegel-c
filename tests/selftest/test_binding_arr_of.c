/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_ARR_OF with schema-valued length, length coherent
** via HEGEL_USE against a HEGEL_LET-bound int.
**
** Topology:
**     Bag { int n; int *items; }
**
** n is drawn in [2, 5] via HEGEL_LET.  items is allocated with
** exactly n elements via HEGEL_ARR_OF(HEGEL_USE(n), ...).  Each
** element is drawn in [0, 100].
**
** Property: len(items) == n, every item in [0, 100].
** This replaces the facet value/size pattern with explicit
** coherence.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 n;
  int *               items;
} Bag;

HEGEL_BINDING (n);

static hegel_schema_t bag_schema;

static
void
init_schema (void)
{
  bag_schema = HEGEL_STRUCT (Bag,
      HEGEL_LET (n, HEGEL_INT (2, 5)),                  /* non-positional */
      HEGEL_USE (n),                                     /* field 0: int n */
      HEGEL_ARR_OF (HEGEL_USE (n), HEGEL_INT (0, 100))); /* field 1: int * */
}

static
void
test_bag_coherence (
hegel_testcase *    tc)
{
  Bag *               b;
  hegel_shape *       sh;
  int                 i;

  sh = hegel_schema_draw (tc, bag_schema, (void **) &b);

  HEGEL_ASSERT (b->n >= 2 && b->n <= 5,
                "n=%d out of [2,5]", b->n);
  HEGEL_ASSERT (b->items != NULL,
                "items is NULL with n=%d", b->n);

  /* The point: array length coheres with stored n field. */
  for (i = 0; i < b->n; i ++) {
    HEGEL_ASSERT (b->items[i] >= 0 && b->items[i] <= 100,
                  "items[%d]=%d out of [0,100]", i, b->items[i]);
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
  printf ("Testing HEGEL_ARR_OF(HEGEL_USE(n), ...) coherence...\n");
  hegel_run_test (test_bag_coherence);
  printf ("  PASSED\n");

  hegel_schema_free (bag_schema);
  return (0);
}
