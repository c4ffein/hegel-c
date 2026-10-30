/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a binary tree whose recursion is driven by HEGEL_DEPENDS
** callbacks stays inside a depth cap and a leaf budget.
**
** Trio on bounded trees:
**   - test_recursion_tree.c     — hand-written, on the engine's
**     recursion protocol (branch / leaf / retry / finish);
**   - test_schema_self_limits.c — HEGEL_SELF, same protocol under
**     the schema layer;
**   - this file                 — callbacks, no protocol: each child
**     slot asks the view how deep it is and counts what it offers.
**
** Same struct and properties as test_schema_self_limits.c:
**   - capped:  no tree deeper than CAPPED_DEPTH levels;
**   - budget:  no tree with more than LEAF_BUDGET leaves;
**   - spread:  some tree has at least SPREAD_MIN leaves.
**
** What the callbacks can't do is the protocol's unwinding: once the
** budget is spent they can only answer NULL, so an over-budget tree is
** truncated (its rightmost slots are empty) instead of being redrawn.
** The budget holds either way; the distribution differs.  The budget
** counter lives in the callback ctx and is reset before each draw —
** a callback has no per-draw state of its own.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

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

/* NULL child slots — the leaves. */
static
int
tree_leaves (
const Node *                n)
{
  if (n == NULL)
    return (1);
  return (tree_leaves (n->left) + tree_leaves (n->right));
}

/* ---- Layer 2: schema + hegel tests ---- */

#define CAPPED_DEPTH   3
#define DEFAULT_DEPTH  50
#define LEAF_BUDGET    100
#define SPREAD_MIN     10

typedef struct {
  int                 max_levels;
  int                 offered;     /* child slots offered so far, this draw */
} Limits;

static hegel_schema_t  node_schema;
static Limits          limits;
static int             max_leaves_seen;

/* A binary tree with k nodes has k + 1 leaves, so offering at most
** LEAF_BUDGET - 1 non-root nodes keeps the leaves within budget. */
static
hegel_schema_t
child_of (
const hegel_view *          v,
void *                      ctx)
{
  Limits *            lim = ctx;

  if (hegel_view_depth (v) + 1 >= lim->max_levels
      || lim->offered >= LEAF_BUDGET - 1)
    return (hegel_schema_i64_range (0, 0));   /* NULL */
  lim->offered ++;
  return (hegel_schema_optional_ptr (hegel_schema_ref (node_schema)));
}

static
Node *
draw_tree (
hegel_testcase *            tc,
int                         max_levels,
hegel_shape * *             sh)
{
  Node *               root = NULL;

  limits.max_levels = max_levels;
  limits.offered    = 0;
  *sh = hegel_schema_draw (tc, node_schema, (void **) &root);
  return (root);
}

static
void
test_capped (
hegel_testcase *            tc)
{
  hegel_shape *        sh;
  Node *               root;
  int                  levels;

  root = draw_tree (tc, CAPPED_DEPTH, &sh);
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
  hegel_shape *        sh;
  Node *               root;
  int                  leaves;

  root = draw_tree (tc, DEFAULT_DEPTH, &sh);
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
      HEGEL_INT     (0, 100),
      HEGEL_DEPENDS (Node *, child_of, &limits),
      HEGEL_DEPENDS (Node *, child_of, &limits));

  printf ("Testing a view-driven depth cap...\n");
  hegel_run_test_nofork (test_capped);

  printf ("Testing a view-driven leaf budget...\n");
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
