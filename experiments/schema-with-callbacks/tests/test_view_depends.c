/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_DEPENDS / HEGEL_DERIVED / HEGEL_LEN_OF — fields computed
** from the tree drawn so far, with no binding ids.
**
** Layer 1: graph_check() — validates a small graph: every edge endpoint
**          is a vertex, dst >= src, edge ids are their positions,
**          weights[i] <= i, counts match, the label fits nvert.
** Layer 2: a schema where each of those facts comes from a view:
**   - Edge.src         DEPENDS on the parent's nvert   (hegel_view_up)
**   - Edge.dst         DEPENDS on its own src too      (sibling field)
**   - Edge.id          DERIVED: its index in the array (hegel_view_index)
**   - Graph.weights[i] DEPENDS (scalar ARR_OF element) on i
**   - Graph.label      DEPENDS, returns a text schema  (any slot kind)
**   - Graph.nedge / nweight  LEN_OF the engine-length arrays (HEGEL_LEN)
**   - Graph.total      DERIVED from the drawn weights   (HEGEL_VIEW_AS)
** Layer 3: hegel_run_test — every generated graph must pass.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 src;
  int                 dst;
  int                 id;
} Edge;

typedef struct {
  int                 nvert;
  Edge **             edges;
  int                 nedge;
  int *               weights;
  int                 nweight;
  char *              label;
  int                 total;
} Graph;

/* ---- Layer 1: function under test ---- */

static
const char *
graph_check (
const Graph *       g)
{
  int                 i;
  int                 total = 0;

  if (g->nvert < 1 || g->nvert > 8)
    return ("nvert out of range");
  for (i = 0; i < g->nedge; i ++) {
    const Edge * e = g->edges[i];
    if (e->src < 0 || e->src >= g->nvert || e->dst < 0 || e->dst >= g->nvert)
      return ("edge endpoint is not a vertex");
    if (e->dst < e->src)
      return ("dst < src");
    if (e->id != i)
      return ("edge id is not its position");
  }
  for (i = 0; i < g->nweight; i ++) {
    if (g->weights[i] < 0 || g->weights[i] > i)
      return ("weights[i] out of [0, i]");
    total += g->weights[i];
  }
  if (total != g->total)
    return ("total is not the sum of the weights");
  if ((int) strlen (g->label) > g->nvert)
    return ("label longer than nvert");
  return (NULL);
}

/* ---- Layer 2: schema callbacks ---- */

static
hegel_schema_t
edge_src (
const hegel_view *  v,
void *              ctx)
{
  int nvert = HEGEL_VIEW_FIELD (hegel_view_up (v, 1), Graph, nvert);
  (void) ctx;
  return (hegel_schema_int_range (0, nvert - 1));
}

static
hegel_schema_t
edge_dst (
const hegel_view *  v,
void *              ctx)
{
  int nvert = HEGEL_VIEW_FIELD (hegel_view_up (v, 1), Graph, nvert);
  int src   = HEGEL_VIEW_FIELD (v, Edge, src);
  (void) ctx;
  return (hegel_schema_int_range (src, nvert - 1));
}

static
void
edge_id (
const hegel_view *  v,
void *              out,
void *              ctx)
{
  (void) ctx;
  *(int *) out = hegel_view_index (hegel_view_up (v, 1));
}

static
hegel_schema_t
weight_at (
const hegel_view *  v,
void *              ctx)
{
  (void) ctx;
  return (hegel_schema_int_range (0, hegel_view_index (v)));
}

static
hegel_schema_t
label_of (
const hegel_view *  v,
void *              ctx)
{
  (void) ctx;
  return (hegel_schema_text (0, HEGEL_VIEW_FIELD (v, Graph, nvert)));
}

static
void
total_of (
const hegel_view *  v,
void *              out,
void *              ctx)
{
  const Graph *       g = HEGEL_VIEW_AS (v, Graph);
  int                 i;
  int                 total = 0;

  (void) ctx;
  for (i = 0; i < g->nweight; i ++)
    total += g->weights[i];
  *(int *) out = total;
}

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t graph_schema;

static
void
test_graph (
hegel_testcase *    tc)
{
  Graph *             g;
  hegel_shape *       sh;
  const char *        err;

  sh = hegel_schema_draw (tc, graph_schema, (void **) &g);
  err = graph_check (g);
  if (err == NULL && hegel_shape_array_len (hegel_shape_field (sh, 1)) != g->nedge)
    err = "nedge differs from the drawn array length";
  hegel_shape_free (sh);
  HEGEL_ASSERT (err == NULL, "%s", err);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  hegel_schema_t edge = HEGEL_STRUCT (Edge,
      HEGEL_DEPENDS (int, edge_src, NULL),
      HEGEL_DEPENDS (int, edge_dst, NULL),
      HEGEL_DERIVED (int, edge_id, NULL));

  graph_schema = HEGEL_STRUCT (Graph,
      HEGEL_INT (1, 8),
      HEGEL_ARR_OF (HEGEL_LEN (0, 12), edge),
      HEGEL_LEN_OF (Graph, edges),
      HEGEL_ARR_OF (HEGEL_LEN (0, 6), HEGEL_DEPENDS (int, weight_at, NULL)),
      HEGEL_LEN_OF (Graph, weights),
      HEGEL_DEPENDS (char *, label_of, NULL),
      HEGEL_DERIVED (int, total_of, NULL));

  printf ("Testing views (DEPENDS / DERIVED / LEN_OF)...\n");
  hegel_run_test_n (test_graph, 300);
  printf ("PASSED\n");
  hegel_schema_free (graph_schema);
  return (0);
}
