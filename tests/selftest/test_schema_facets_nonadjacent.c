/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: non-adjacent array layout — pointer first, count last, with
** an unrelated field between them.  Under bindings: HEGEL_LET is
** non-positional, so the count's draw order is decoupled from its
** slot position.  The USE that writes to the `n` slot can come AFTER
** the ARR_OF that consumes its value — the binding table has already
** been populated by the LET.
**
** Previously implemented via HEGEL_ARRAY + non-adjacent HEGEL_FACET
** calls with dynamic first-facet-primary coordination.
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

HEGEL_BINDING (n);

static hegel_schema_t bag_schema;

static
void
init_schema (void)
{
  bag_schema = HEGEL_STRUCT (Bag,
      HEGEL_LET    (n, HEGEL_INT (0, 8)),                  /* non-positional */
      HEGEL_ARR_OF (HEGEL_USE (n), HEGEL_INT (0, 999)),    /* int * items */
      HEGEL_INT    (-10, 10),                              /* int tag */
      HEGEL_USE    (n));                                   /* int n — LAST */
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
