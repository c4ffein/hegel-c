/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: an n-ary tree drawn by hand on the engine's recursion
** protocol — the primitive-API twin of schema-with-let's
** test_schema_facets_self_recursion.c (experiments/).
**
** Trio on n-ary trees (Tree { int val; Tree **kids; int n_kids; },
** each kid slot optional):
**   - this file — hand-written gen_node(): a drawn kid count, and per
**     slot hegel_recursion_branch / _leaf;
**   - the schema versions, in experiments/: schema-with-let's
**     test_schema_facets_self_recursion.c (HEGEL_LET kid count +
**     HEGEL_ARR_OF (HEGEL_SELF ())) and schema-with-callbacks'
**     test_view_nary_tree.c (callbacks, HEGEL_LEN, HEGEL_LEN_OF).
**
** Same properties as the schema version: n_kids in [0, 3], a
** countable tree, and over the run some kid slot non-NULL.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "hegel_c.h"

/* ---- Types ---- */

typedef struct Tree {
  int                 val;
  struct Tree **      kids;    /* each slot is optional (may be NULL) */
  int                 n_kids;
} Tree;

#define MAX_KIDS   3
#define MAX_DEPTH  50
#define MAX_LEAVES 100

/* ---- Function under test ---- */

/* Count all non-NULL nodes reachable from `root` (including root). */
static
int
count_nodes (
Tree *              root)
{
  int                 total;
  int                 i;

  if (root == NULL) return (0);
  total = 1;
  for (i = 0; i < root->n_kids; i ++)
    total += count_nodes (root->kids[i]);
  return (total);
}

static
void
tree_free (
Tree *              t)
{
  int                 i;

  if (t == NULL) return;
  for (i = 0; i < t->n_kids; i ++)
    tree_free (t->kids[i]);
  free (t->kids);
  free (t);
}

/* ---- Generator ---- */

/* Draw one node at `depth`.  Returns 0 when the attempt outgrew the
** leaf budget — the caller unwinds and the whole tree is redrawn. */
static
int
gen_node (
hegel_testcase *            tc,
hegel_recursion             rec,
uint64_t                    depth,
Tree * *                    out)
{
  Tree *              t;
  int                 i;
  int                 ok = 1;

  hegel_start_span (tc, hegel_label ("selftest.nary.node"));
  t = calloc (1, sizeof *t);
  t->n_kids = hegel_draw_int (tc, 0, MAX_KIDS);
  t->val    = hegel_draw_int (tc, -100, 100);
  t->kids   = calloc ((size_t) (t->n_kids ? t->n_kids : 1), sizeof (Tree *));
  for (i = 0; ok && i < t->n_kids; i ++) {
    hegel_start_span (tc, HEGEL_SPAN_OPTIONAL);
    if (hegel_recursion_branch (tc, rec, depth + 1))
      ok = gen_node (tc, rec, depth + 1, &t->kids[i]);
    else if (hegel_recursion_leaf (tc, rec))
      ok = 0;                            /* over budget: unwind */
    hegel_stop_span (tc, 0);
  }
  hegel_stop_span (tc, 0);
  if (!ok) {
    tree_free (t);
    return (0);
  }
  *out = t;
  return (1);
}

static
Tree *
gen_tree (
hegel_testcase *            tc)
{
  hegel_recursion     rec;
  Tree *              root = NULL;

  rec = hegel_recursion_new (tc, MAX_DEPTH, MAX_LEAVES);
  for (;;) {
    if (!gen_node (tc, rec, 0, &root)) {
      hegel_recursion_retry (tc, rec);   /* discards the case when exhausted */
      continue;
    }
    if (hegel_recursion_finish (tc, rec) == 0)
      break;
    tree_free (root);                    /* mispriced: start again */
    root = NULL;
  }
  hegel_recursion_free (tc, rec);
  return (root);
}

/* ---- Test ---- */

static int g_nonnull_kid_slots = 0;
static int g_total_kid_slots   = 0;

static
void
test_tree (
hegel_testcase *            tc)
{
  Tree *              root;
  int                 n;
  int                 i;

  root = gen_tree (tc);

  HEGEL_ASSERT (root != NULL, "root should be non-NULL");
  HEGEL_ASSERT (root->n_kids >= 0 && root->n_kids <= MAX_KIDS,
                "root->n_kids=%d out of range", root->n_kids);

  for (i = 0; i < root->n_kids; i ++) {
    g_total_kid_slots ++;
    if (root->kids[i] != NULL) g_nonnull_kid_slots ++;
  }

  n = count_nodes (root);
  HEGEL_ASSERT (n >= 1, "count_nodes returned %d", n);

  tree_free (root);
}

/* ---- Runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing a hand-written n-ary tree on the recursion protocol...\n");
  hegel_run_test_nofork (test_tree);
  printf ("  kid slots drawn: total=%d, non-NULL=%d\n",
          g_total_kid_slots, g_nonnull_kid_slots);
  if (g_total_kid_slots > 0 && g_nonnull_kid_slots == 0) {
    fprintf (stderr, "  FAIL: every kid slot landed NULL\n");
    return (1);
  }
  printf ("  PASSED\n");
  return (0);
}
