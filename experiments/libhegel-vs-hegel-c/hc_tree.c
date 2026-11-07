/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 4, hegel-c: the same expression tree ('+' / '-' branches, leaves
** 0..9; max depth 50, max leaves 100) through hegel_draw_recursive, which
** runs the protocol, spans each sub-value and frees abandoned attempts;
** expr_simplify must keep the value.
*/
#include "hegel_c.h"
#include "common.h"
#include "sut.h"

static void * gen_node (hegel_testcase * tc, bool leaf, void * ctx)
{
  (void) ctx;
  expr * e = hegel_alloc (tc, sizeof *e);
  if (leaf) {
    e->val = hegel_draw_int (tc, 0, 9);
    return e;
  }
  e->op = hegel_draw_int (tc, 0, 1) ? '-' : '+';
  e->a = hegel_recursive_child (tc);
  e->b = hegel_recursive_child (tc);
  return e;
}

static void prop (hegel_testcase * tc)
{
  count_exec ();
  expr * e = hegel_draw_recursive (tc, 50, 100, gen_node, NULL);
  char text[8192];
  FILE * m = fmemopen (text, sizeof text, "w");
  fprintf (m, "expr = ");
  expr_print (m, e);
  fclose (m);
  hegel_note (tc, text);
  long before = expr_eval (e);
  long after = expr_eval (expr_simplify (e));
  HEGEL_ASSERT (before == after, "value %ld, simplified %ld", before, after);
}

/* B{ */
int main (void)
{
  hegel_run_test_n (prop, 100);
  return 0;
}
/* }B */
