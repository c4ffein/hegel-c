/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: 3D jagged — recursive composition of HEGEL_LET_ARR + HEGEL_USE_AT.
**
** Asks the question: how does the current binding mechanism scale
** beyond the 2D pattern of test_binding_let_arr.c?  Answer (this
** file): one extra LET_ARR + USE_AT pair per dimension.  No new
** mechanism needed.
**
** Topology:
**     Block  { int *items; int len; }
**     Group  { Block **blocks; int m; }
**     Tree   { Group **groups; int n; }
**
**     Tree LETs   n           in [1, 3].
**     Tree LET_ARRs outer_sizes (length n, each in [1, 3]).
**     Tree ARR_OF iterates n elements.  For each Group:
**       Group's m       = outer_sizes[i_tree]                (USE_AT in slot)
**       Group LET_ARRs   inner_sizes (length m, each in [0, 4]).
**       Group ARR_OF iterates m elements.  For each Block:
**         Block's len   = inner_sizes[i_group]               (USE_AT in slot)
**         Block's items = ARR_OF length = inner_sizes[i_group]
**
** Three nested scopes; USE_AT walks to the scope where the binding
** was declared (Tree's scope for outer_sizes, Group's scope for
** inner_sizes) and reads at that scope's iteration index.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int *               items;
  int                 len;
} Block;

typedef struct {
  Block **            blocks;
  int                 m;
} Group;

typedef struct {
  Group **            groups;
  int                 n;
} Tree;

HEGEL_BINDING (n);
HEGEL_BINDING (outer_sizes);
HEGEL_BINDING (inner_sizes);

static hegel_schema_t tree_schema;

static
void
init_schema (void)
{
  hegel_schema_t block_s = HEGEL_STRUCT (Block,
      HEGEL_ARR_OF  (HEGEL_USE_AT (inner_sizes), HEGEL_INT (0, 99)),
      HEGEL_USE_AT  (inner_sizes));

  hegel_schema_t group_s = HEGEL_STRUCT (Group,
      HEGEL_LET_ARR (inner_sizes, HEGEL_USE_AT (outer_sizes), HEGEL_INT (0, 4)),
      HEGEL_ARR_OF  (HEGEL_USE_AT (outer_sizes), block_s),
      HEGEL_USE_AT  (outer_sizes));

  tree_schema = HEGEL_STRUCT (Tree,
      HEGEL_LET     (n, HEGEL_INT (1, 3)),
      HEGEL_LET_ARR (outer_sizes, HEGEL_USE (n), HEGEL_INT (1, 3)),
      HEGEL_ARR_OF  (HEGEL_USE (n), group_s),
      HEGEL_USE     (n));
}

static
void
test_jagged_3d (
hegel_testcase *    tc)
{
  Tree *              t;
  hegel_shape *       sh;
  int                 i, j, k;

  sh = hegel_schema_draw (tc, tree_schema, (void **) &t);

  HEGEL_ASSERT (t->n >= 1 && t->n <= 3, "n=%d out of [1,3]", t->n);
  HEGEL_ASSERT (t->groups != NULL, "groups NULL");

  for (i = 0; i < t->n; i ++) {
    Group * g = t->groups[i];
    HEGEL_ASSERT (g != NULL, "groups[%d] NULL", i);

    /* g->m came from outer_sizes[i] via HEGEL_USE_AT. */
    HEGEL_ASSERT (g->m >= 1 && g->m <= 3,
                  "groups[%d].m=%d out of [1,3]", i, g->m);
    HEGEL_ASSERT (g->blocks != NULL, "groups[%d].blocks NULL", i);

    for (j = 0; j < g->m; j ++) {
      Block * b = g->blocks[j];
      HEGEL_ASSERT (b != NULL, "groups[%d].blocks[%d] NULL", i, j);

      /* b->len came from inner_sizes[j] via HEGEL_USE_AT. */
      HEGEL_ASSERT (b->len >= 0 && b->len <= 4,
                    "groups[%d].blocks[%d].len=%d out of [0,4]", i, j, b->len);

      /* items array length equals b->len (both came from inner_sizes[j]). */
      if (b->len > 0) {
        HEGEL_ASSERT (b->items != NULL,
                      "groups[%d].blocks[%d].items NULL with len=%d",
                      i, j, b->len);
        for (k = 0; k < b->len; k ++) {
          HEGEL_ASSERT (b->items[k] >= 0 && b->items[k] <= 99,
                        "groups[%d].blocks[%d].items[%d]=%d out of [0,99]",
                        i, j, k, b->items[k]);
        }
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
  printf ("Testing 3D jagged (LET_ARR/USE_AT recursive composition)...\n");
  hegel_run_test (test_jagged_3d);
  printf ("  PASSED\n");

  hegel_schema_free (tree_schema);
  return (0);
}
