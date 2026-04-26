/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: nested arrays — outer array whose elements themselves hold
** an inner array.  Both use HEGEL_LET + HEGEL_ARR_OF.
**
** Topology:
**     Bag   { Chunk **chunks; int n_chunks; }
**     Chunk { int *data;      int n_data;   }
**
** Each struct's pointer field comes BEFORE its count field — the
** count's USE entry appears AFTER the ARR_OF that consumes the
** value.  That works because HEGEL_LET is non-positional: by the
** time draw reaches the count-USE slot, the value has already been
** drawn and cached.
**
** Per-instance binding scope: every Chunk created as an element of
** the outer array gets its own ctx, so each Chunk draws its own
** n_data independently.  If per-instance scoping leaked, all chunks
** would share one cached n_data and buffer sizes/pointers would
** desynchronize.
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
  int *               data;
  int                 n_data;
} Chunk;

typedef struct {
  Chunk **            chunks;
  int                 n_chunks;
} Bag;

/* ---- Schema ---- */

HEGEL_BINDING (n_chunks);
HEGEL_BINDING (n_data);

static hegel_schema_t bag_schema;

static
void
init_schema (void)
{
  /* Inner struct: each Chunk has its own data buffer in [0, 9].
  ** Per-instance binding scope guarantees n_data is independent
  ** across chunks. */
  hegel_schema_t chunk = HEGEL_STRUCT (Chunk,
      HEGEL_LET    (n_data, HEGEL_INT (0, 4)),
      HEGEL_ARR_OF (HEGEL_USE (n_data), HEGEL_INT (0, 9)),   /* int * data */
      HEGEL_USE    (n_data));                                 /* int n_data */

  bag_schema = HEGEL_STRUCT (Bag,
      HEGEL_LET    (n_chunks, HEGEL_INT (1, 3)),
      HEGEL_ARR_OF (HEGEL_USE (n_chunks), chunk),            /* Chunk ** chunks */
      HEGEL_USE    (n_chunks));                               /* int n_chunks */
}

/* ---- Test ---- */

static
void
test_nested_bag (
hegel_testcase *            tc)
{
  Bag *               b;
  hegel_shape *       sh;
  int                 i;
  int                 j;

  sh = hegel_schema_draw (tc, bag_schema, (void **) &b);

  HEGEL_ASSERT (b->n_chunks >= 1 && b->n_chunks <= 3,
                "n_chunks=%d out of range", b->n_chunks);

  for (i = 0; i < b->n_chunks; i ++) {
    Chunk * c = b->chunks[i];
    HEGEL_ASSERT (c != NULL, "chunks[%d] is NULL", i);
    HEGEL_ASSERT (c->n_data >= 0 && c->n_data <= 4,
                  "chunks[%d].n_data=%d out of range", i, c->n_data);
    for (j = 0; j < c->n_data; j ++) {
      /* If the inner ctx leaked across chunks, we might read from a
      ** different chunk's buffer and land outside [0, 9] or crash. */
      HEGEL_ASSERT (c->data[j] >= 0 && c->data[j] <= 9,
                    "chunks[%d].data[%d]=%d out of range",
                    i, j, c->data[j]);
    }
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
  printf ("Testing nested array facets (independent per-instance)...\n");
  hegel_run_test (test_nested_bag);
  printf ("  PASSED\n");

  hegel_schema_free (bag_schema);
  return (0);
}
