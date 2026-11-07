/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 3, hegel-c: the same graph generator (nvert in [1, 20], up to 40
** edges through the collection protocol, endpoints below nvert, a span
** per graph and per edge); adj_build must store 2 entries per edge.
** Edge storage is case memory (hegel_realloc): no free anywhere.
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
  int stored = adj_build (&g, &a);
  HEGEL_ASSERT (stored == 2 * g.nedge, "stored %d of %d adjacency entries", stored, 2 * g.nedge);
}

/* B{ */
int main (void)
{
  hegel_run_test_n (prop, 100);
  return 0;
}
/* }B */
