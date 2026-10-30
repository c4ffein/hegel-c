/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Primitive-API version of test_graph_part_schema.c / _view.c — same
** Graph struct, same properties, same SCOTCH_graphPart call, but no
** hegel_gen.h: the graph comes from plain generator functions on the
** hegel_c.h primitives, each calling the next (gen_graph -> gen_edges
** -> gen_edge), with spans for structure and the collection protocol
** for the edge list.
**
** Dependencies are just locals: an edge's bounds come from the nvert
** passed in, v from the u drawn just before.  And since the generator
** sees the edges drawn so far, it can also refuse duplicates with
** hegel_collection_reject — which closes gap#2 of the schema version
** (the view version leaves it open): build_csr only symmetrizes.
**
** The cost is the bookkeeping the schema layer does for free: spans,
** growing the buffer, and freeing it (graph_free).
**
** Caveat — density.  The collection protocol aims for a small average
** size (~5 for min 0) whatever the max, and libhegel's ABI has no knob
** for it.  So these graphs average 6.4 distinct edges on 25 vertices,
** against ~60 usable edges for the schema / view versions (whose
** ARRAY_INLINE draws a length in [0, 200]).  This version runs ~3x
** faster because it tests much sparser graphs, not because it is
** cheaper per graph.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <scotch.h>

#include "hegel_c.h"

/* ---- Layer 1: types + CSR builder ---- */

typedef struct EdgePair {
  int u;
  int v;
} EdgePair;

typedef struct Graph {
  int          nvert;
  EdgePair *   edges;
  int          nedges;
  int          npart;
} Graph;

#define MAX_VERT  50
#define MAX_EDGES 200

/* ---- Layer 2a: generators ---- */

static
void
gen_edge (
hegel_testcase *            tc,
int                         nvert,
EdgePair *                  e)
{
  hegel_start_span (tc, hegel_label ("scotch.edge"));
  e->u = hegel_draw_int (tc, 0, nvert - 2);
  e->v = hegel_draw_int (tc, e->u + 1, nvert - 1);
  hegel_stop_span (tc, 0);
}

static
int
edge_seen (
const EdgePair *            edges,
int                         n,
const EdgePair *            e)
{
  int                 i;

  for (i = 0; i < n; i++)
    if (edges[i].u == e->u && edges[i].v == e->v)
      return (1);
  return (0);
}

/* Distinct edges, as many as the engine wants (at most every pair). */
static
void
gen_edges (
hegel_testcase *            tc,
Graph *                     g)
{
  int                 max = g->nvert * (g->nvert - 1) / 2;
  int                 cap = 8;
  hegel_collection    coll;
  EdgePair            e;

  if (max > MAX_EDGES)
    max = MAX_EDGES;
  g->edges  = malloc ((size_t) cap * sizeof (EdgePair));
  g->nedges = 0;

  hegel_start_span (tc, HEGEL_SPAN_LIST);
  coll = hegel_collection_new (tc, 0, (uint64_t) max);
  while (hegel_collection_more (tc, coll)) {
    hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);
    gen_edge (tc, g->nvert, &e);
    if (edge_seen (g->edges, g->nedges, &e)) {
      hegel_stop_span (tc, 1);          /* dead end: the shrinker skips it */
      hegel_collection_reject (tc, coll);
      continue;
    }
    hegel_stop_span (tc, 0);
    if (g->nedges == cap) {
      cap *= 2;
      g->edges = realloc (g->edges, (size_t) cap * sizeof (EdgePair));
    }
    g->edges[g->nedges++] = e;
  }
  hegel_collection_free (tc, coll);
  hegel_stop_span (tc, 0);
}

static
Graph *
gen_graph (
hegel_testcase *            tc)
{
  Graph *             g = calloc (1, sizeof (Graph));

  hegel_start_span (tc, hegel_label ("scotch.graph"));
  g->nvert = hegel_draw_int (tc, 3, MAX_VERT);
  gen_edges (tc, g);
  g->npart = hegel_draw_int (tc, 2, g->nvert < 8 ? g->nvert : 8);
  hegel_stop_span (tc, 0);
  return (g);
}

static
void
graph_free (
Graph *                     g)
{
  free (g->edges);
  free (g);
}

/* Build a valid undirected CSR from the logical graph.  Edges arrive
** in range, loop-free, with u < v and distinct (see the generators), so
** all that is left is to symmetrize (each pair emits both directions).
**
** Caller frees *verttab_out + *edgetab_out.
** *nedge_out is the total directed-edge count in edgetab.
*/
static
void
build_csr (
Graph *                     g,
SCOTCH_Num * *              verttab_out,
SCOTCH_Num * *              edgetab_out,
int *                       nedge_out)
{
  int                 nvert = g->nvert;
  int *               deg;
  SCOTCH_Num *        verttab;
  SCOTCH_Num *        edgetab;
  int *               pos;
  int                 total;
  int                 i;
  int                 u, v;

  deg = calloc ((size_t) nvert, sizeof (int));
  for (i = 0; i < g->nedges; i++) {
    deg[g->edges[i].u]++;
    deg[g->edges[i].v]++;
  }
  verttab = calloc ((size_t) (nvert + 1), sizeof (SCOTCH_Num));
  total = 0;
  for (i = 0; i < nvert; i++) {
    verttab[i] = total;
    total += deg[i];
  }
  verttab[nvert] = total;

  edgetab = calloc ((size_t) (total > 0 ? total : 1), sizeof (SCOTCH_Num));
  pos = calloc ((size_t) nvert, sizeof (int));
  for (i = 0; i < g->nedges; i++) {
    u = g->edges[i].u;
    v = g->edges[i].v;
    edgetab[verttab[u] + pos[u]++] = v;
    edgetab[verttab[v] + pos[v]++] = u;
  }

  free (deg);
  free (pos);

  *verttab_out = verttab;
  *edgetab_out = edgetab;
  *nedge_out   = total;
}

/* ---- Layer 2b: hegel test ---- */

static
void
test_partition (
hegel_testcase *            tc)
{
  Graph *             g;
  SCOTCH_Num *        verttab = NULL;
  SCOTCH_Num *        edgetab = NULL;
  SCOTCH_Num *        parttab;
  int                 nedge = 0;
  int                 nvert;
  int                 npart;
  int                 rc;
  int                 i;
  int *               part_size;
  int                 max_size;
  SCOTCH_Graph        grafdat;
  SCOTCH_Strat        stradat;

  g = gen_graph (tc);

  nvert = g->nvert;
  npart = g->npart;

  build_csr (g, &verttab, &edgetab, &nedge);

  SCOTCH_graphInit (&grafdat);
  rc = SCOTCH_graphBuild (&grafdat, 0, nvert, verttab, verttab + 1,
                          NULL, NULL, nedge, edgetab, NULL);
  HEGEL_ASSERT (rc == 0, "SCOTCH_graphBuild failed (rc=%d)", rc);

  rc = SCOTCH_graphCheck (&grafdat);
  HEGEL_ASSERT (rc == 0,
                "SCOTCH_graphCheck failed (rc=%d) nvert=%d nedge=%d",
                rc, nvert, nedge);

  SCOTCH_stratInit (&stradat);
  parttab = calloc ((size_t) nvert, sizeof (SCOTCH_Num));
  rc = SCOTCH_graphPart (&grafdat, npart, &stradat, parttab);
  HEGEL_ASSERT (rc == 0,
                "SCOTCH_graphPart failed (rc=%d) nvert=%d npart=%d nedge=%d",
                rc, nvert, npart, nedge);

  /* Property 1: every vertex in a valid partition. */
  for (i = 0; i < nvert; i++) {
    HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < npart,
                  "vertex %d: partition %d not in [0, %d)",
                  i, (int) parttab[i], npart);
  }

  /* Property 2: every partition used when nvert >= npart. */
  part_size = calloc ((size_t) npart, sizeof (int));
  for (i = 0; i < nvert; i++) part_size[parttab[i]]++;
  if (nvert >= npart) {
    for (i = 0; i < npart; i++) {
      HEGEL_ASSERT (part_size[i] > 0,
                    "partition %d is empty: nvert=%d npart=%d nedge=%d",
                    i, nvert, npart, nedge);
    }
  }

  /* Property 3: load balance — tighter than the primitive-draw version. */
  max_size = 0;
  for (i = 0; i < npart; i++) {
    if (part_size[i] > max_size) max_size = part_size[i];
  }
  {
    int ceil_avg = (nvert + npart - 1) / npart;
    int tolerance = ceil_avg / 5;
    if (tolerance < 1) tolerance = 1;
    HEGEL_ASSERT (max_size <= ceil_avg + tolerance,
                  "load imbalance: max=%d ceil_avg=%d tol=%d "
                  "nvert=%d npart=%d nedge=%d",
                  max_size, ceil_avg, tolerance, nvert, npart, nedge);
  }

  free (part_size);
  free (parttab);
  SCOTCH_stratExit (&stradat);
  SCOTCH_graphExit (&grafdat);
  free (verttab);
  free (edgetab);
  graph_free (g);
}

/* ---- Layer 3: runner ---- */

static
void
seed_scotch (void)
{
  SCOTCH_randomSeed (42);
}

int
main (void)
{
  hegel_set_case_setup (seed_scotch);
  hegel_run_test_n (test_partition, 500);
  return (0);
}
