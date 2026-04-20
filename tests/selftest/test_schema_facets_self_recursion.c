/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_SELF inside a facet'd HEGEL_ARRAY's element schema.
**
** Regression guard against two bugs fixed together:
**   1. hegel__resolve_self now descends through HEGEL_SCH_SUBSCHEMA
**      into the source, so SELF nested inside a facet'd composite's
**      element schema gets its target patched.
**   2. hegel__draw_array_standalone now has an OPTIONAL_PTR branch,
**      so each element slot is actually drawn (50/50 NULL or a
**      fresh recursive Tree).
** Without either fix, every element slot silently lands NULL and
** the non-NULL-kid tally at end of main() is 0 despite many slots
** being drawn.
**
** Topology:
**     Tree { int val; Tree **kids; int n_kids; }
**
** The `kids` array is built via HEGEL_ARRAY(HEGEL_SELF(), ...) and
** projected into the enclosing struct via HEGEL_FACET.  Each array
** element is an optional pointer to the enclosing Tree — classic
** n-ary recursive tree.
**
** The catch: hegel__resolve_self walks the struct's children to
** patch HEGEL_SCH_SELF targets, but descends through SUBSCHEMA to
** reach the SELF nested inside the array's element schema.  If the
** walk doesn't recurse through SUBSCHEMA, the SELF target stays
** NULL and draw crashes (NULL deref in hegel__draw_alloc).
**
** Expected: EXIT 0 — every reachable kid is a valid Tree (or NULL),
** the total node count is finite, no crashes.
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

/* Sum of all values in the tree. */
static
int64_t
sum_values (
Tree *              root)
{
  int64_t             s;
  int                 i;

  if (root == NULL) return (0);
  s = root->val;
  for (i = 0; i < root->n_kids; i ++)
    s += sum_values (root->kids[i]);
  return (s);
}

/* ---- Schema ---- */

static hegel_schema_t tree_schema;

static
void
init_schema (void)
{
  hegel_schema_t kids_arr = HEGEL_ARRAY (HEGEL_SELF (), 0, 3);
  tree_schema = HEGEL_STRUCT (Tree,
      HEGEL_INT   (-100, 100),
      HEGEL_FACET (kids_arr, value),
      HEGEL_FACET (kids_arr, size));
  hegel_schema_free (kids_arr);
}

/* ---- Test ---- */

/* Shared across nofork iterations — counts how many kid slots across
** all draws have landed as non-NULL.  If the SUBSCHEMA resolve +
** array-elem-OPTIONAL paths actually work, this should be > 0 after
** 100 iterations with n_kids up to 3 and 50/50 optional presence.
** If the bug is still live, every slot is silently NULL and the
** counter stays at 0. */
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
  int64_t             total;
  int                 i;

  sh = hegel_schema_draw (tc, tree_schema, (void **) &root);

  HEGEL_ASSERT (root != NULL, "root should be non-NULL");
  HEGEL_ASSERT (root->n_kids >= 0 && root->n_kids <= 3,
                "root->n_kids=%d out of range", root->n_kids);

  /* Tally: did any kid slot land non-NULL? */
  for (i = 0; i < root->n_kids; i ++) {
    g_total_kid_slots ++;
    if (root->kids[i] != NULL) g_nonnull_kid_slots ++;
  }

  n = count_nodes (root);
  HEGEL_ASSERT (n >= 1, "count_nodes returned %d", n);

  total = sum_values (root);
  (void) total;

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
  printf ("Testing HEGEL_SELF inside a facet'd array...\n");
  hegel_run_test_nofork (test_tree);
  printf ("  kid slots drawn: total=%d, non-NULL=%d\n",
          g_total_kid_slots, g_nonnull_kid_slots);

  /* If recursion actually worked, some slots should have landed
  ** non-NULL across 100 iterations.  A counter of 0 with > 0 total
  ** slots drawn signals the silent "all-NULL" pathology (SELF not
  ** resolved and/or OPTIONAL not handled as an array element kind). */
  if (g_total_kid_slots > 0 && g_nonnull_kid_slots == 0) {
    fprintf (stderr, "  FAIL: every kid slot landed NULL — "
                     "HEGEL_SELF inside a facet'd array is not "
                     "actually recursing (silent no-op).\n");
    return (1);
  }

  printf ("  PASSED\n");
  hegel_schema_free (tree_schema);
  return (0);
}
