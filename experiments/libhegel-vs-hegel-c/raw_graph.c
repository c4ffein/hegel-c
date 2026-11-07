/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 3, libhegel only: a graph (nvert in [1, 20], then up to 40 edges
** whose endpoints are drawn below nvert, the engine owning the edge
** count through the collection protocol, a span per graph and per edge);
** adj_build must store 2 entries per edge.  Edge storage grows with
** realloc and is freed on every exit path.  Generation follows
** libhegel/examples/spans_collections.c.
** Lines between B{ and }B markers are boilerplate.
*/
#include "raw_check.h"
#include "common.h"
#include "sut.h"

static uint64_t L_GRAPH, L_EDGE;

/* On a non-OK return g->edges may hold a partial array: the caller frees it. */
static hegel_result_t gen_graph (hegel_context_t * ctx, hegel_test_case_t * tc, graph * g)
{
  int64_t nv, u, v;
  hegel_result_t rc;
  hegel_collection_t * c;
  bool more;

  if ((rc = hegel_start_span (ctx, tc, L_GRAPH)) != HEGEL_OK) return rc;
  if ((rc = hegel_generate_integer (ctx, tc, 1, MAX_VERT, &nv)) != HEGEL_OK) return rc;
  g->nvert = (int) nv;
  if ((rc = hegel_new_collection (ctx, tc, 0, MAX_EDGE, &c)) != HEGEL_OK) return rc;
  for (;;) {
    if ((rc = hegel_collection_more (ctx, tc, c, &more)) != HEGEL_OK || !more) break;
    if ((rc = hegel_start_span (ctx, tc, L_EDGE)) != HEGEL_OK) break;
    if ((rc = hegel_generate_integer (ctx, tc, 0, nv - 1, &u)) != HEGEL_OK) break;
    if ((rc = hegel_generate_integer (ctx, tc, 0, nv - 1, &v)) != HEGEL_OK) break;
    if ((rc = hegel_stop_span (ctx, tc, false)) != HEGEL_OK) break;
    edge * grown = realloc (g->edges, (size_t) (g->nedge + 1) * sizeof *grown);
    if (!grown) abort ();
    g->edges = grown;
    g->edges[g->nedge++] = (edge) { (int) u, (int) v };
  }
  hegel_collection_free (ctx, c);
  if (rc == HEGEL_OK) rc = hegel_stop_span (ctx, tc, false);
  return rc;
}

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, bool show,
                      const char ** origin)
{
  count_exec ();
  graph g = { 0 };
  adjacency a;
  /* B{ */
  if (gen_graph (ctx, tc, &g) != HEGEL_OK) {
    free (g.edges);
    return HEGEL_STATUS_OVERRUN;
  }
  /* }B */
  if (show) graph_print (stdout, &g);
  int stored = adj_build (&g, &a);
  int expected = 2 * g.nedge;
  /* B{ */
  free (g.edges);
  /* }B */
  if (stored != expected) {
    if (show) printf ("stored %d of %d adjacency entries\n", stored, expected);
    *origin = "adj_build stores 2 entries per edge";
    return HEGEL_STATUS_INTERESTING;
  }
  return HEGEL_STATUS_VALID;
}

/* B{ */
int main (void)
{
  hegel_context_t * ctx = hegel_context_new ();
  CHECK (hegel_label_from_name, ctx, STR ("lvh.graph"), &L_GRAPH);
  CHECK (hegel_label_from_name, ctx, STR ("lvh.edge"), &L_EDGE);
  hegel_settings_t * s;
  CHECK (hegel_settings_new, ctx, &s);
  CHECK (hegel_settings_set_test_cases, ctx, s, 100);
  CHECK (hegel_settings_set_database, ctx, s, &STR (""));

  hegel_run_t * run;
  CHECK (hegel_run_start, ctx, s, NULL, NULL, &run);
  for (;;) {
    hegel_test_case_t * tc;
    CHECK (hegel_next_test_case, ctx, run, &tc);
    if (!tc) break;
    const char * origin = NULL;
    uint32_t st = prop (ctx, tc, false, &origin);
    CHECK (hegel_mark_complete, ctx, tc, st, origin ? &STR (origin) : NULL);
    CHECK (hegel_test_case_free, ctx, tc);
  }

  hegel_run_result_t * r;
  hegel_run_status_t status;
  size_t nf;
  CHECK (hegel_run_result, ctx, run, &r);
  CHECK (hegel_run_result_status, ctx, r, &status);
  if (status == HEGEL_RUN_STATUS_ERROR) {
    const hegel_string_t * err;
    CHECK (hegel_run_result_error, ctx, r, &err);
    fprintf (stderr, "run error: %.*s\n", (int) err->len, err->data);
  }
  CHECK (hegel_run_result_failure_count, ctx, r, &nf);
  for (size_t i = 0; i < nf; i++) {
    hegel_failure_t * f;
    hegel_string_t origin;
    const hegel_string_t * blob;
    CHECK (hegel_run_result_failure, ctx, r, i, &f);
    CHECK (hegel_failure_origin, ctx, f, &origin);
    CHECK (hegel_failure_reproduction_blob, ctx, f, &blob);
    printf ("FAILED: %.*s\n", (int) origin.len, origin.data);
    if (blob) {
      hegel_test_case_t * tc;
      const char * o = NULL;
      CHECK (hegel_test_case_from_blob, ctx, s, *blob, NULL, NULL, &tc);
      uint32_t st = prop (ctx, tc, true, &o);
      CHECK (hegel_mark_complete, ctx, tc, st, o ? &STR (o) : NULL);
      CHECK (hegel_test_case_free, ctx, tc);
      printf ("blob: %.*s\n", (int) blob->len, blob->data);
    }
    CHECK (hegel_failure_free, ctx, f);
  }
  CHECK (hegel_run_result_free, ctx, r);
  CHECK (hegel_run_free, ctx, run);
  CHECK (hegel_settings_free, ctx, s);
  CHECK (hegel_context_free, ctx);
  return status == HEGEL_RUN_STATUS_PASSED ? 0 : 1;
}
/* }B */
