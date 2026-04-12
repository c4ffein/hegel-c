/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Schema-API version of test_graph_part.c.  Generates a logical
** (nvert, edges[], npart) graph via hegel_gen.h, builds a valid CSR
** from it (clamp endpoints, strip self-loops, dedupe, symmetrize),
** and partitions with SCOTCH_graphPart.
**
** Stronger properties than the primitive-draw version (test_graph_part.c):
**   1. partition index in [0, npart)
**   2. every partition used when nvert >= npart (no empty partitions)
**   3. load balance — max partition <= ceil(nvert/npart) + 20%
**
** Schema-API gaps surfaced by this test:
**
**   gap#1 — sibling-dependent field bounds.  Edge endpoints should
**           be bounded by the sibling nvert, but schema only
**           supports constant integer bounds.  Worked around by
**           drawing in [0, MAX_VERT-1] and clamping (u % nvert)
**           at build time.
**
**   gap#2 — structural invariants across array elements.  The CSR
**           the schema produces must be dedup'd, symmetric, and
**           self-loop-free.  Schema can't express that; done in
**           a C helper (build_csr) after drawing.
**
**   gap#3 (FIXED 2026-04-12) — see TODO.md for the original
**           hegel-c fork-mode orphan-leak bug surfaced by this
**           test.  Root cause was `tc.draw()` panicking with
**           `__HEGEL_STOP_TEST` inside parent_serve and bypassing
**           the child reap.  Fix is in rust-version/src/lib.rs.
**           This test now passes meaningfully.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <scotch.h>

#include "hegel_c.h"
#include "hegel_gen.h"

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

static hegel_schema_t graph_schema;

static
void
init_schemas (void)
{
  hegel_schema_t edge = hegel_schema_struct (sizeof (EdgePair),
      HEGEL_INT (EdgePair, u, 0, MAX_VERT - 1),
      HEGEL_INT (EdgePair, v, 0, MAX_VERT - 1));

  graph_schema = hegel_schema_struct (sizeof (Graph),
      HEGEL_INT (Graph, nvert, 3, MAX_VERT),
      HEGEL_ARRAY_INLINE (Graph, edges, nedges,
                          edge, sizeof (EdgePair), 0, MAX_EDGES),
      HEGEL_INT (Graph, npart, 2, 8));
}

/* Build a valid undirected CSR from the logical graph:
**   - clamp endpoints into [0, nvert)
**   - strip self-loops
**   - dedupe
**   - symmetrize (each undirected pair emits both directions)
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
  int                 cap   = g->nedges + 1;
  int                 (*undir)[2];
  int                 n_undir = 0;
  int *               deg;
  SCOTCH_Num *        verttab;
  SCOTCH_Num *        edgetab;
  int *               pos;
  int                 total;
  int                 i, j;
  int                 u, v, t;

  undir = calloc ((size_t) cap, sizeof (int[2]));
  for (i = 0; i < g->nedges; i++) {
    u = g->edges[i].u % nvert;
    v = g->edges[i].v % nvert;
    if (u == v) continue;
    if (u > v) { t = u; u = v; v = t; }
    {
      int dup = 0;
      for (j = 0; j < n_undir; j++) {
        if (undir[j][0] == u && undir[j][1] == v) { dup = 1; break; }
      }
      if (dup) continue;
    }
    undir[n_undir][0] = u;
    undir[n_undir][1] = v;
    n_undir++;
  }

  deg = calloc ((size_t) nvert, sizeof (int));
  for (i = 0; i < n_undir; i++) {
    deg[undir[i][0]]++;
    deg[undir[i][1]]++;
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
  for (i = 0; i < n_undir; i++) {
    u = undir[i][0];
    v = undir[i][1];
    edgetab[verttab[u] + pos[u]++] = v;
    edgetab[verttab[v] + pos[v]++] = u;
  }

  free (undir);
  free (deg);
  free (pos);

  *verttab_out = verttab;
  *edgetab_out = edgetab;
  *nedge_out   = total;
}

/* ---- Layer 2: hegel test ---- */

static
void
test_partition (
hegel_testcase *            tc)
{
  Graph *             g;
  hegel_shape *       shape;
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

  shape = hegel_schema_draw (tc, graph_schema, (void **) &g);

  nvert = g->nvert;
  npart = g->npart;
  if (npart > nvert) npart = nvert;  /* no more parts than vertices */

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
  hegel_shape_free (shape);
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
  init_schemas ();
  hegel_set_case_setup (seed_scotch);
  hegel_run_test_n (test_partition, 500);
  hegel_schema_free (graph_schema);
  return (0);
}
