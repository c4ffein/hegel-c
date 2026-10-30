/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: an n-ary tree whose kid array comes from a HEGEL_DEPENDS
** callback — the view twin of test_schema_facets_self_recursion.c
** (see test_recursion_nary_tree.c for the trio).
**
** The callback returns the kid array: an HEGEL_ARR_OF with an
** engine-owned length (HEGEL_LEN) whose elements are optional
** pointers to another Tree, or an empty array once the node is
** MAX_DEPTH structs deep.  n_kids is HEGEL_LEN_OF the array, so it
** is filled after the array and the count needs no binding.
**
** No recursion protocol here: termination comes from the depth cap
** and the 50/50 options, not from a leaf budget.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Types ---- */

typedef struct Tree {
  int                 val;
  struct Tree **      kids;    /* each slot is optional (may be NULL) */
  int                 n_kids;
} Tree;

#define MAX_KIDS   3
#define MAX_DEPTH  6

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

/* ---- Schema ---- */

static hegel_schema_t tree_schema;

static
hegel_schema_t
kids_of (
const hegel_view *          v,
void *                      ctx)
{
  (void) ctx;
  if (hegel_view_depth (v) >= MAX_DEPTH)
    return (hegel_schema_arr_of (HEGEL_CONST (0),
                                 hegel_schema_optional_ptr (
                                     hegel_schema_ref (tree_schema))));
  return (hegel_schema_arr_of (HEGEL_LEN (0, MAX_KIDS),
                               hegel_schema_optional_ptr (
                                   hegel_schema_ref (tree_schema))));
}

static
void
init_schema (void)
{
  tree_schema = HEGEL_STRUCT (Tree,
      HEGEL_INT     (-100, 100),                      /* int val */
      HEGEL_DEPENDS (Tree **, kids_of, NULL),         /* Tree ** kids */
      HEGEL_LEN_OF  (Tree, kids));                    /* int n_kids */
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
  hegel_shape *       sh;
  int                 n;
  int                 i;

  sh = hegel_schema_draw (tc, tree_schema, (void **) &root);

  HEGEL_ASSERT (root != NULL, "root should be non-NULL");
  HEGEL_ASSERT (root->n_kids >= 0 && root->n_kids <= MAX_KIDS,
                "root->n_kids=%d out of range", root->n_kids);

  for (i = 0; i < root->n_kids; i ++) {
    g_total_kid_slots ++;
    if (root->kids[i] != NULL) g_nonnull_kid_slots ++;
  }

  n = count_nodes (root);
  HEGEL_ASSERT (n >= 1, "count_nodes returned %d", n);

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
  printf ("Testing a view-driven n-ary tree...\n");
  hegel_run_test_nofork (test_tree);
  printf ("  kid slots drawn: total=%d, non-NULL=%d\n",
          g_total_kid_slots, g_nonnull_kid_slots);
  if (g_total_kid_slots > 0 && g_nonnull_kid_slots == 0) {
    fprintf (stderr, "  FAIL: every kid slot landed NULL\n");
    return (1);
  }
  printf ("  PASSED\n");
  hegel_schema_free (tree_schema);
  return (0);
}
