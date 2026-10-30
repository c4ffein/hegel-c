/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: count-before-data array layout via HEGEL_LET + HEGEL_ARR_OF.
**
** The struct is laid out count-first-then-pointer: `{int n; int *items;}`.
** HEGEL_LET declares n non-positionally, HEGEL_USE fills the count
** slot, HEGEL_ARR_OF(HEGEL_USE(n), ...) allocates exactly n elements.
**
** Previously implemented via HEGEL_ARRAY + HEGEL_FACET with dynamic
** "first-facet-is-primary" coordination — the binding form expresses
** the same coherence explicitly.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Types ---- */

typedef struct {
  int                 n;          /* size slot first */
  int *               items;      /* value slot second */
} ReverseBag;

/* ---- Schema ---- */

HEGEL_BINDING (n);

static hegel_schema_t bag_schema;

static
void
init_schema (void)
{
  bag_schema = HEGEL_STRUCT (ReverseBag,
      HEGEL_LET    (n, HEGEL_INT (0, 6)),                  /* non-positional */
      HEGEL_USE    (n),                                     /* int n */
      HEGEL_ARR_OF (HEGEL_USE (n), HEGEL_INT (0, 255)));   /* int * items */
}

/* ---- Test ---- */

static
void
test_reverse_bag (
hegel_testcase *            tc)
{
  ReverseBag *        b;
  hegel_shape *       sh;
  int                 i;

  sh = hegel_schema_draw (tc, bag_schema, (void **) &b);

  HEGEL_ASSERT (b->n >= 0 && b->n <= 6, "n=%d out of range", b->n);
  HEGEL_ASSERT (b->items != NULL, "items pointer is NULL");

  /* If the cache miscoordinated ptr and len, this loop would read
  ** garbage.  Every valid in-range integer means both facets refer
  ** to one consistent drawn array. */
  for (i = 0; i < b->n; i ++) {
    HEGEL_ASSERT (b->items[i] >= 0 && b->items[i] <= 255,
                  "items[%d]=%d out of range", i, b->items[i]);
  }

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

  init_schema ();
  printf ("Testing reversed-order array facets...\n");
  hegel_run_test (test_reverse_bag);
  printf ("  PASSED\n");

  hegel_schema_free (bag_schema);
  return (0);
}
