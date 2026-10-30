/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_SELF trees stay inside the engine's depth cap and leaf
** budget.
**
** HEGEL_SELF presence is decided by the engine's recursion protocol:
** each HEGEL_SELF slot is a sub-value that either branches (a child
** struct) or is a leaf (NULL).  The engine caps the depth — drawn with
** max_depth d, a tree has at most d levels of structs, root included —
** and the leaves at 100 per value: attempts that outgrow the budget are
** unwound and redrawn from the root.
**
** Three properties, on a binary tree (two HEGEL_SELF slots per node):
**   - capped:  drawn with max_depth 3, no tree is deeper than 3 levels;
**   - budget:  at the default depth, no tree has more than 100 leaves;
**   - spread:  over the run, some tree has at least 10 leaves (the
**              budget is not met by only ever drawing tiny trees).
** The budget property only holds if over-budget attempts are really
** unwound and redrawn: a truncating implementation could not keep it.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: the recursive struct, and functions measuring it ---- */

typedef struct Node {
  int                 val;
  struct Node *       left;
  struct Node *       right;
} Node;

/* Levels of structs, root included. */
static
int
tree_levels (
const Node *                n)
{
  int                 l;
  int                 r;

  if (n == NULL)
    return (0);
  l = tree_levels (n->left);
  r = tree_levels (n->right);
  return (1 + (l > r ? l : r));
}

/* NULL child slots — the engine's leaves. */
static
int
tree_leaves (
const Node *                n)
{
  if (n == NULL)
    return (1);
  return (tree_leaves (n->left) + tree_leaves (n->right));
}

/* ---- Layer 2: hegel tests ---- */

#define CAPPED_DEPTH   3
#define LEAF_BUDGET    100
#define SPREAD_MIN     10

static hegel_schema_t  node_schema;
static int             max_leaves_seen;

static
void
test_capped (
hegel_testcase *            tc)
{
  Node *               root = NULL;
  hegel_shape *        sh;
  int                  levels;

  sh = hegel_schema_draw_at_n (tc, &root, node_schema, CAPPED_DEPTH);
  levels = tree_levels (root);
  hegel_shape_free (sh);
  HEGEL_ASSERT (levels >= 1 && levels <= CAPPED_DEPTH,
                "tree has %d levels, cap is %d", levels, CAPPED_DEPTH);
}

static
void
test_budget (
hegel_testcase *            tc)
{
  Node *               root = NULL;
  hegel_shape *        sh;
  int                  leaves;

  sh = hegel_schema_draw (tc, node_schema, (void **) &root);
  leaves = tree_leaves (root);
  hegel_shape_free (sh);
  if (leaves > max_leaves_seen)
    max_leaves_seen = leaves;
  HEGEL_ASSERT (leaves <= LEAF_BUDGET,
                "tree has %d leaves, budget is %d", leaves, LEAF_BUDGET);
}

/* ---- Layer 3: runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  node_schema = HEGEL_STRUCT (Node,
      HEGEL_INT  (0, 100),
      HEGEL_SELF (),
      HEGEL_SELF ());

  printf ("Testing the HEGEL_SELF depth cap...\n");
  hegel_run_test_nofork (test_capped);

  printf ("Testing the HEGEL_SELF leaf budget...\n");
  hegel_run_test_nofork (test_budget);
  if (max_leaves_seen < SPREAD_MIN) {
    fprintf (stderr, "largest tree had %d leaves — expected some tree "
                     "with at least %d\n", max_leaves_seen, SPREAD_MIN);
    return (1);
  }
  printf ("  largest tree: %d leaves\n", max_leaves_seen);

  hegel_schema_free (node_schema);
  return (0);
}
