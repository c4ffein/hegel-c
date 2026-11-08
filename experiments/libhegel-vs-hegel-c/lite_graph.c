/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 3, lite libhegel: a graph from the collection protocol (nvert in
** [1, 20], up to 40 edges, endpoints < nvert, a span per graph and per
** edge, as in raw_graph.c and hc_graph.c so generation is the same);
** adj_build must store 2 entries per edge.  Lite: the edges live in a
** fixed array (the collection's max bounds them), so nothing is
** allocated and nothing needs freeing; span labels are plain constants.
** Every engine call inside the generator can end the case (STOP_TEST),
** so each one is checked.  See graph.md.
*/
#include <stdio.h>
#include "hegel.h"
#include "common.h"
#include "sut.h"

static int gen_graph (hegel_context_t * ctx, hegel_test_case_t * tc, graph * g)
{
  int64_t nv, u, v;
  bool more;
  hegel_collection_t * c;
  if (hegel_start_span (ctx, tc, 1) || hegel_generate_integer (ctx, tc, 1, MAX_VERT, &nv)
      || hegel_new_collection (ctx, tc, 0, MAX_EDGE, &c)) return 0;
  g->nvert = (int) nv;
  for (;;) {
    if (hegel_collection_more (ctx, tc, c, &more)) return 0;
    if (!more) break;
    if (hegel_start_span (ctx, tc, 2) || hegel_generate_integer (ctx, tc, 0, nv - 1, &u)
        || hegel_generate_integer (ctx, tc, 0, nv - 1, &v) || hegel_stop_span (ctx, tc, false))
      return 0;
    g->edges[g->nedge++] = (edge) { (int) u, (int) v };
  }
  hegel_collection_free (ctx, c);
  return hegel_stop_span (ctx, tc, false) == HEGEL_OK;
}

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, char * shown)
{
  count_exec ();
  edge edges[MAX_EDGE];
  graph g = { 0, 0, edges };
  adjacency a;
  if (!gen_graph (ctx, tc, &g)) return HEGEL_STATUS_OVERRUN;
  FILE * m = fmemopen (shown, 1024, "w");
  graph_print (m, &g);
  fclose (m);
  return adj_build (&g, &a) == 2 * g.nedge ? HEGEL_STATUS_VALID : HEGEL_STATUS_INTERESTING;
}

/* B{ */
int main (void)
{
  hegel_context_t * ctx = hegel_context_new ();
  hegel_settings_t * s;
  hegel_run_t * run;
  hegel_test_case_t * tc;
  char last[1024] = "";
  hegel_settings_new (ctx, &s);
  hegel_settings_set_database (ctx, s, &(hegel_string_t) { "", 0 });
  hegel_run_start (ctx, s, NULL, NULL, &run);
  while (hegel_next_test_case (ctx, run, &tc) == HEGEL_OK && tc) {
    bool capture = false;
    char shown[1024];
    hegel_test_case_should_capture (ctx, tc, &capture);
    uint32_t st = prop (ctx, tc, shown);
    if (capture && st == HEGEL_STATUS_INTERESTING) snprintf (last, sizeof last, "%s", shown);
    hegel_mark_complete (ctx, tc, st, NULL);
    hegel_test_case_free (ctx, tc);
  }
  hegel_run_result_t * r;
  hegel_run_status_t status;
  hegel_run_result (ctx, run, &r);
  hegel_run_result_status (ctx, r, &status);
  if (status == HEGEL_RUN_STATUS_FAILED) printf ("FAILED: %s", last);
  if (status == HEGEL_RUN_STATUS_ERROR) {
    const hegel_string_t * err;
    hegel_run_result_error (ctx, r, &err);
    printf ("run error: %.*s\n", (int) err->len, err->data);
  }
  return status == HEGEL_RUN_STATUS_PASSED ? 0 : 1;
}
/* }B */
