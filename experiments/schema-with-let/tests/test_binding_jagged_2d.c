/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: jagged 2D — the design's original target.  Exercises every
** stage of the binding system in a single test:
**
**   - Stage 1: HEGEL_LET / HEGEL_USE (same-struct)
**   - Stage 2: HEGEL_USE inside a STRUCT element reaches an outer
**              HEGEL_LET via the scope chain (`outer_n_copy` field)
**   - Stage 3: HEGEL_ARR_OF(HEGEL_USE(m), ...) — array length
**              coheres with a bound int
**   - Stage 4: HEGEL_ARR_OF with STRUCT elements, each getting its
**              own per-instance ctx for independent `m` bindings
**
** Topology:
**     Group  { int outer_n_copy; int m; int *data; }
**     Jagged { int n; Group **groups; }
**
** Jagged draws n in [2, 4].  Each of n Groups draws its own m in
** [1, 3] and a data array of exactly m ints in [0, 9].  The
** outer_n_copy field in every Group must equal Jagged.n — if the
** per-instance ctx accidentally leaked outer bindings differently,
** or per-instance m scoping collapsed, this would fail.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 outer_n_copy;
  int                 m;
  int *               data;
} Group;

typedef struct {
  int                 n;
  Group **            groups;
} Jagged;

HEGEL_BINDING (n);
HEGEL_BINDING (m);

static hegel_schema_t jagged_schema;

static
void
init_schema (void)
{
  hegel_schema_t  group_schema = HEGEL_STRUCT (Group,
      HEGEL_LET (m, HEGEL_INT (1, 3)),                    /* non-positional */
      HEGEL_USE (n),                                       /* field 0: outer_n_copy */
      HEGEL_USE (m),                                       /* field 1: m */
      HEGEL_ARR_OF (HEGEL_USE (m), HEGEL_INT (0, 9)));    /* field 2: data */

  jagged_schema = HEGEL_STRUCT (Jagged,
      HEGEL_LET (n, HEGEL_INT (2, 4)),                   /* non-positional */
      HEGEL_USE (n),                                      /* field 0: n */
      HEGEL_ARR_OF (HEGEL_USE (n), group_schema));       /* field 1: groups */
}

static
void
test_jagged_2d (
hegel_testcase *    tc)
{
  Jagged *            j;
  hegel_shape *       sh;
  int                 i;
  int                 k;

  sh = hegel_schema_draw (tc, jagged_schema, (void **) &j);

  HEGEL_ASSERT (j->n >= 2 && j->n <= 4,
                "n=%d out of [2,4]", j->n);
  HEGEL_ASSERT (j->groups != NULL,
                "groups ptr NULL with n=%d", j->n);

  for (i = 0; i < j->n; i ++) {
    Group * g = j->groups[i];
    HEGEL_ASSERT (g != NULL, "groups[%d] NULL", i);

    /* Stage 2: inner struct reached outer binding. */
    HEGEL_ASSERT (g->outer_n_copy == j->n,
                  "groups[%d].outer_n_copy=%d != j->n=%d",
                  i, g->outer_n_copy, j->n);

    /* Stage 3+4: per-instance m + coherent data length. */
    HEGEL_ASSERT (g->m >= 1 && g->m <= 3,
                  "groups[%d].m=%d out of [1,3]", i, g->m);
    HEGEL_ASSERT (g->data != NULL,
                  "groups[%d].data NULL with m=%d", i, g->m);

    for (k = 0; k < g->m; k ++) {
      HEGEL_ASSERT (g->data[k] >= 0 && g->data[k] <= 9,
                    "groups[%d].data[%d]=%d out of [0,9]",
                    i, k, g->data[k]);
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
  printf ("Testing jagged 2D: HEGEL_LET/USE + HEGEL_ARR_OF end-to-end...\n");
  hegel_run_test (test_jagged_2d);
  printf ("  PASSED\n");

  hegel_schema_free (jagged_schema);
  return (0);
}
