/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: non-adjacent array facets.
**
** Demonstrates HEGEL_ARRAY used via HEGEL_FACET — the array's two
** projections (value pointer + size) land at NON-ADJACENT positions
** in the parent struct, separated by an unrelated field.  This is
** the V2 "handles" use case that the 2-slot inline HEGEL_ARRAY form
** can't express.
**
** The two facets must refer to ONE drawn array: for every test case,
** `bag.items` must point to an int[] of exactly `bag.n` elements,
** and the pointer must match the length drawn.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Types ----
** Layout is intentionally value-pointer, then an unrelated int, then
** size — so the two facets cannot be expressed as a 2-adjacent-slot
** HEGEL_ARRAY. */

typedef struct {
  int *               items;
  int                 tag;      /* unrelated field between the facets */
  int                 n;
} Bag;

/* ---- Schema ---- */

static hegel_schema_t bag_schema;

static
void
init_schema (void)
{
  hegel_schema_t items_arr =
      HEGEL_ARRAY (hegel_schema_int_range (0, 999), 0, 8);

  bag_schema = HEGEL_STRUCT (Bag,
      HEGEL_FACET  (items_arr, value),   /* int * items */
      HEGEL_INT    (-10, 10),            /* int tag */
      HEGEL_FACET  (items_arr, size));   /* int n */

  /* Release the user's own reference to the array schema — HEGEL_FACET
  ** bumped source->refcount for each of the two uses above, and
  ** HEGEL_STRUCT will decrement each when it frees its children.
  ** This call drops the original refcount=1 from hegel_schema_array. */
  hegel_schema_free (items_arr);
}

/* ---- Test ---- */

static
void
test_bag (
hegel_testcase *            tc)
{
  Bag *               b;
  hegel_shape *       sh;
  int                 i;

  sh = hegel_schema_draw (tc, bag_schema, (void **) &b);

  HEGEL_ASSERT (b->n >= 0 && b->n <= 8, "n=%d out of range", b->n);
  HEGEL_ASSERT (b->tag >= -10 && b->tag <= 10,
                "tag=%d out of range", b->tag);
  HEGEL_ASSERT (b->items != NULL, "items pointer is NULL");

  /* Every element within range — confirms items actually points at a
  ** real int array drawn from items_arr's element schema. */
  for (i = 0; i < b->n; i ++) {
    HEGEL_ASSERT (b->items[i] >= 0 && b->items[i] <= 999,
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
  printf ("Testing non-adjacent array facets...\n");
  hegel_run_test (test_bag);
  printf ("  PASSED\n");

  hegel_schema_free (bag_schema);
  return (0);
}
