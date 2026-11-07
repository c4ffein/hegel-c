/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_recursive_child misuse is a health failure, not a crash
**       or a silent wrong tree.
**
** Layer 1: list_len() — length of a singly linked list.
** Layer 2: two broken generators:
**            - one calls hegel_recursive_child from a leaf (the node
**              function got leaf = true);
**            - one calls it with no hegel_draw_recursive running.
**          Each must abort its run with "Health check failure".
** Layer 3: main() runs both (result runners, no exit) and returns
**          non-zero once both were refused.
**
** Expected: EXIT non-zero, "Health check failure" on stderr.
*/
#include <stdio.h>

#include "hegel_c.h"

typedef struct Cell {
  struct Cell *       next;
} Cell;

/* ---- Layer 1: function under test ---- */

static
int
list_len (
const Cell *                c)
{
  int                 n = 0;

  for (; c; c = c->next)
    n ++;
  return (n);
}

/* ---- Layer 2: broken generators ---- */

/* Wrong: a leaf asks for a child. */
static
void *
gen_cell_child_in_leaf (
hegel_testcase *            tc,
bool                        leaf,
void *                      ctx)
{
  Cell *              c;

  (void) leaf;
  (void) ctx;
  c = hegel_alloc (tc, sizeof *c);
  c->next = hegel_recursive_child (tc);       /* ignores `leaf` */
  return (c);
}

static
void
testChildInLeaf (
hegel_testcase *            tc)
{
  Cell *              c;

  c = hegel_draw_recursive (tc, 10, 10, gen_cell_child_in_leaf, NULL);
  HEGEL_ASSERT (list_len (c) >= 1, "empty list");
}

static
void
testChildOutside (
hegel_testcase *            tc)
{
  Cell *              c;

  c = hegel_recursive_child (tc);             /* no hegel_draw_recursive */
  HEGEL_ASSERT (list_len (c) >= 0, "unreachable");
}

/* ---- Layer 3: runner (see Makefile TESTS_HEALTH) ---- */

int
main (
int                 argc,
char *              argv[])
{
  int                 refused = 0;

  (void) argc;
  (void) argv;

  refused += hegel_run_test_nofork_with (testChildInLeaf, NULL);
  refused += hegel_run_test_with (testChildOutside, NULL);
  printf ("%d of 2 misuses refused\n", refused);
  return (refused == 2 ? 1 : 0);
}
