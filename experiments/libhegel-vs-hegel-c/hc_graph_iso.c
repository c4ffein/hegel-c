/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 3, hegel-c nofork mode + HEGEL_ISOLATED: hc_graph.c's generator,
** unchanged, run in the runner's process (draws are plain calls into the
** engine), and only adj_build with its check forked.  What it measures:
** crash isolation for a draw-heavy generator at one fork per case and no
** pipe round trip per draw — the row between "hegel-c fork" and "hegel-c
** nofork".
*/
#include "hegel_c.h"
#include "common.h"
#include "sut.h"

static void gen_graph (hegel_testcase * tc, graph * g)
{
  hegel_start_span (tc, hegel_label ("lvh.graph"));
  g->nvert = hegel_draw_int (tc, 1, MAX_VERT);
  hegel_collection c = hegel_collection_new (tc, 0, MAX_EDGE);
  while (hegel_collection_more (tc, c)) {
    hegel_start_span (tc, hegel_label ("lvh.edge"));
    int u = hegel_draw_int (tc, 0, g->nvert - 1);
    int v = hegel_draw_int (tc, 0, g->nvert - 1);
    hegel_stop_span (tc, 0);
    g->edges = hegel_realloc (tc, g->edges, (size_t) (g->nedge + 1) * sizeof *g->edges);
    g->edges[g->nedge++] = (edge) { u, v };
  }
  hegel_collection_free (tc, c);
  hegel_stop_span (tc, 0);
}

static void prop (hegel_testcase * tc)
{
  count_exec ();
  graph g = { 0 };
  adjacency a;
  gen_graph (tc, &g);
  char shape[1024];
  FILE * m = fmemopen (shape, sizeof shape, "w");
  graph_print (m, &g);
  fclose (m);
  hegel_note (tc, shape);
  HEGEL_ISOLATED (tc) {
    int stored = adj_build (&g, &a);
    HEGEL_ASSERT (stored == 2 * g.nedge, "stored %d of %d adjacency entries", stored, 2 * g.nedge);
  }
}

/* B{ */
int main (void)
{
  hegel_run_test_nofork_n (prop, 100);
  return 0;
}
/* }B */
