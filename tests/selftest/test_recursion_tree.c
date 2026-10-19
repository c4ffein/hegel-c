/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: the engine's recursion protocol builds bounded trees.
**
** Layer 1: tree_depth() / tree_leaves() — measure a binary tree.
** Layer 2: generate a tree with hegel_recursion_* (branch / leaf /
**          retry / finish), freeing abandoned attempts, then check the
**          engine's bounds: depth <= max_depth and leaves <= max_leaves.
**          Runs under both runners.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

#define MAX_DEPTH  6
#define MAX_LEAVES 20

typedef struct Node {
  struct Node *       left;
  struct Node *       right;   /* both NULL for a leaf */
  int                 value;
} Node;

/* ---- Layer 1: functions under test ---- */

static
int
tree_depth (
const Node *                n)
{
  int                 l;
  int                 r;

  if (!n->left)
    return (0);
  l = tree_depth (n->left);
  r = tree_depth (n->right);
  return (1 + (l > r ? l : r));
}

static
int
tree_leaves (
const Node *                n)
{
  if (!n->left)
    return (1);
  return (tree_leaves (n->left) + tree_leaves (n->right));
}

static
void
tree_free (
Node *                      n)
{
  if (!n)
    return;
  tree_free (n->left);
  tree_free (n->right);
  free (n);
}

/* ---- Layer 2: hegel test ---- */

/* Draw one sub-value at `depth`; NULL means "the attempt outgrew its
** leaf budget — unwind". */
static
Node *
gen_node (
hegel_testcase *            tc,
hegel_recursion             rec,
uint64_t                    depth)
{
  Node *              n;

  n = calloc (1, sizeof *n);
  if (hegel_recursion_branch (tc, rec, depth)) {
    n->left = gen_node (tc, rec, depth + 1);
    if (n->left)
      n->right = gen_node (tc, rec, depth + 1);
    if (!n->left || !n->right) {
      tree_free (n);
      return (NULL);
    }
    return (n);
  }
  if (hegel_recursion_leaf (tc, rec)) {
    free (n);
    return (NULL);
  }
  n->value = hegel_draw_int (tc, 0, 9);
  return (n);
}

static
Node *
gen_tree (
hegel_testcase *            tc)
{
  hegel_recursion     rec;
  Node *              root;

  rec = hegel_recursion_new (tc, MAX_DEPTH, MAX_LEAVES);
  for (;;) {
    root = gen_node (tc, rec, 0);
    if (!root) {
      hegel_recursion_retry (tc, rec);   /* discards the case when exhausted */
      continue;
    }
    if (hegel_recursion_finish (tc, rec) == 0)
      break;
    tree_free (root);                    /* mispriced: start again */
  }
  hegel_recursion_free (tc, rec);
  return (root);
}

static
void
testRecursionTree (
hegel_testcase *            tc)
{
  Node *              t;
  int                 d;
  int                 l;

  t = gen_tree (tc);
  d = tree_depth (t);
  l = tree_leaves (t);
  tree_free (t);
  HEGEL_ASSERT (d <= MAX_DEPTH, "tree depth %d > %d", d, MAX_DEPTH);
  HEGEL_ASSERT (l <= MAX_LEAVES, "tree has %d leaves > %d", l, MAX_LEAVES);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing the engine recursion protocol...\n");
  if (hegel_run_test_result_n (testRecursionTree, 100) != 0)
    return (1);
  hegel_run_test_nofork_n (testRecursionTree, 100);
  printf ("OK\n");

  return (0);
}
