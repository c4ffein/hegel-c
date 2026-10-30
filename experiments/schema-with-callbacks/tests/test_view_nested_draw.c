/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a HEGEL_DEPENDS callback may run its own schema draw.
**
** Each node of an outer HEGEL_SELF tree takes its `hi` bound from a
** callback that draws a whole inner HEGEL_SELF tree and counts its
** nodes.  So two recursion protocols run at once: the outer draw is
** suspended mid-tree while the inner one draws, retries over its leaf
** budget, and finishes.
**
** Regression guard: the top-level draw used to treat any recursion
** record still set as left over from an abandoned draw and free it —
** here, the outer draw's half-built tree, which the inner draw then
** reused (heap-use-after-free under ASan; corrupted nodes without).
** Each top-level draw now saves the record it finds and restores it
** when done; records really abandoned (a test case that jumped out of
** a draw) are freed by the runner at the end of the case.
**
** Layer 1: tree_check() validates every node of the outer tree.
** Layer 2: draws outer trees, fork and nofork.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: types + function under test ---- */

typedef struct Inner {
  int                 v;
  struct Inner *      l;
  struct Inner *      r;
} Inner;

typedef struct Outer {
  struct Outer *      l;
  struct Outer *      r;
  int                 hi;      /* nodes in the inner tree drawn for it */
  int                 b;       /* in [0, hi] */
} Outer;

#define MAGIC_LIMIT 1000000

static
int
inner_size (
const Inner *               t)
{
  return (t == NULL ? 0 : 1 + inner_size (t->l) + inner_size (t->r));
}

/* 0 if every node holds hi >= 1 and 0 <= b <= hi; else -1. */
static
int
tree_check (
const Outer *               t,
int *                       nodes)
{
  if (t == NULL)
    return (0);
  (*nodes) ++;
  if (t->hi < 1 || t->hi > MAGIC_LIMIT || t->b < 0 || t->b > t->hi)
    return (-1);
  if (tree_check (t->l, nodes) != 0)
    return (-1);
  return (tree_check (t->r, nodes));
}

/* ---- Layer 2: schemas + hegel test ---- */

static hegel_schema_t  inner_schema;
static hegel_schema_t  outer_schema;
static hegel_testcase * cur_tc;
static int             inner_draws;

static
hegel_schema_t
hi_of (
const hegel_view *          v,
void *                      ctx)
{
  Inner *             t;
  hegel_shape *       sh;
  int                 n;

  (void) v;
  (void) ctx;
  sh = hegel_schema_draw (cur_tc, inner_schema, (void **) &t);
  n = inner_size (t);
  hegel_shape_free (sh);
  inner_draws ++;
  return (hegel_schema_int_range (n, n));
}

static
hegel_schema_t
b_of (
const hegel_view *          v,
void *                      ctx)
{
  (void) ctx;
  return (hegel_schema_int_range (0, HEGEL_VIEW_FIELD (v, Outer, hi)));
}

static
void
test_nested (
hegel_testcase *            tc)
{
  Outer *             t;
  hegel_shape *       sh;
  int                 nodes = 0;

  cur_tc = tc;
  sh = hegel_schema_draw (tc, outer_schema, (void **) &t);
  HEGEL_ASSERT (tree_check (t, &nodes) == 0, "corrupted outer tree");
  HEGEL_ASSERT (nodes >= 1, "empty outer tree");
  hegel_shape_free (sh);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  inner_schema = HEGEL_STRUCT (Inner,
      HEGEL_INT  (0, 9),
      HEGEL_SELF (),
      HEGEL_SELF ());
  outer_schema = HEGEL_STRUCT (Outer,
      HEGEL_SELF    (),
      HEGEL_SELF    (),
      HEGEL_DEPENDS (int, hi_of, NULL),
      HEGEL_DEPENDS (int, b_of, NULL));

  printf ("Testing schema draws nested in a callback...\n");
  if (hegel_run_test_result_n (test_nested, 100) != 0)
    return (1);
  hegel_run_test_nofork_n (test_nested, 100);
  printf ("  inner draws (nofork): %d\n", inner_draws);
  if (inner_draws == 0)
    return (1);
  printf ("OK\n");

  hegel_schema_free (outer_schema);
  hegel_schema_free (inner_schema);
  return (0);
}
