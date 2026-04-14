/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: nested arrays — a matrix (array of arrays of ints).
**
** Demonstrates that ARRAY_INLINE + ARRAY compose naturally to produce
** arrays of arrays.  Each Row is an inline struct in the outer array,
** containing a pointer to a separately-allocated int array.  The
** struct wrapper (Row) is idiomatic C — you always need somewhere to
** store the per-row length anyway.
**
** Shape tree owns everything: the Matrix, the contiguous Row array,
** and every row's int array.  One hegel_shape_free walks it all.
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
  int *               values;
  int                 n_values;
} Row;

typedef struct {
  Row *               rows;
  int                 n_rows;
} Matrix;

/* ---- Schema ---- */

static hegel_schema_t matrix_schema;

static
void
init_schema (void)
{
  hegel_schema_t row = HEGEL_STRUCT (Row,
      HEGEL_ARRAY (hegel_schema_int_range (0, 99), 1, 6));

  matrix_schema = HEGEL_STRUCT (Matrix,
      HEGEL_ARRAY_INLINE (row, sizeof (Row), 1, 4));
}

/* ---- Test ---- */

static
void
test_matrix (
hegel_testcase *            tc)
{
  Matrix *            m;
  hegel_shape *       sh;
  int                 r;
  int                 c;

  sh = hegel_schema_draw (tc, matrix_schema, (void **) &m);

  HEGEL_ASSERT (m->n_rows >= 1 && m->n_rows <= 4,
                "n_rows=%d", m->n_rows);

  for (r = 0; r < m->n_rows; r ++) {
    HEGEL_ASSERT (m->rows[r].n_values >= 1 && m->rows[r].n_values <= 6,
                  "rows[%d].n_values=%d", r, m->rows[r].n_values);
    for (c = 0; c < m->rows[r].n_values; c ++) {
      HEGEL_ASSERT (m->rows[r].values[c] >= 0
                    && m->rows[r].values[c] <= 99,
                    "rows[%d].values[%d]=%d", r, c,
                    m->rows[r].values[c]);
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
  printf ("Testing nested arrays (matrix)...\n");
  hegel_run_test (test_matrix);
  printf ("  PASSED\n");

  hegel_schema_free (matrix_schema);
  return (0);
}
