/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: nested array facets — outer array whose elements themselves
** hold a facet-driven inner array.
**
** Topology:
**     Bag { Chunk **chunks; int n_chunks; }
**     Chunk { int *data; int n_data; }
**
** Both arrays use HEGEL_ARRAY + HEGEL_FACET.  The inner array's
** element buffer must be INDEPENDENT per chunk — if the ctx leaked
** across chunk instances, all chunks would share one int buffer and
** their n_data values would pair with someone else's data pointer.
**
** Specifically: every chunk's data[i] must fall within the inner
** range [0, 9].  If two chunks shared one buffer of a different
** length, indexing would read out of bounds or crash.
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

static hegel_schema_t bag_schema;

static
void
init_schema (void)
{
  /* Inner array: each Chunk has its own data buffer in [0, 9]. */
  hegel_schema_t chunk_data =
      HEGEL_ARRAY (hegel_schema_int_range (0, 9), 0, 4);
  hegel_schema_t chunk = HEGEL_STRUCT (Chunk,
      HEGEL_FACET (chunk_data, value),
      HEGEL_FACET (chunk_data, size));
  hegel_schema_free (chunk_data);

  /* Outer array: each Bag holds 1..3 chunks. */
  hegel_schema_t chunks_arr = HEGEL_ARRAY (chunk, 1, 3);
  bag_schema = HEGEL_STRUCT (Bag,
      HEGEL_FACET (chunks_arr, value),
      HEGEL_FACET (chunks_arr, size));
  hegel_schema_free (chunks_arr);
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
