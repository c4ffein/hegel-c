/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: reversed facet ordering.
**
** The "natural" array layout is pointer-then-count, but C structs can
** put them in either order.  This test uses a struct where the count
** field comes BEFORE the pointer field — which means the SIZE facet
** is drawn first and the VALUE facet is drawn second.
**
** Under the per-draw ctx model, the first facet encountered becomes
** primary (draws the array, owns the shape) and the second is
** secondary (reuses the cached length/pointer).  This test asserts
** that either facet can be primary — the decision is purely dynamic
** and based on draw order, not on some build-time marking.
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

static hegel_schema_t bag_schema;

static
void
init_schema (void)
{
  hegel_schema_t items_arr =
      HEGEL_ARRAY (hegel_schema_int_range (0, 255), 0, 6);

  bag_schema = HEGEL_STRUCT (ReverseBag,
      HEGEL_FACET (items_arr, size),     /* SIZE first → primary */
      HEGEL_FACET (items_arr, value));   /* VALUE second → secondary */

  hegel_schema_free (items_arr);
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
