/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Reducer demo: hegel finds a real Scotch bug and shrinks to a
** minimal counterexample.
**
** Reproduces the hgraphOrderCp off-by-ordenum bug:
**   https://github.com/c4ffein/scotch/blob/ff403d445b36ee1723ff53d2478d368b21a3341f/REPORTS/BUG_REPORT.md
**
** Bug summary: SCOTCH_graphOrder returns 0 (success) but writes
** an INVALID permutation when (a) SCOTCH_STRATDISCONNECTED is set,
** (b) the graph has multiple connected components, and (c) at
** least one non-first component undergoes vertex compression.
**
** Trigger conditions on a random graph (per the bug report):
**   - At least 2 connected components, so some component is
**     ordered with ordenum > 0.
**   - At least one non-first component is a K_2 pair (two
**     vertices connected only to each other), which compresses.
**
** Both conditions occur naturally in small sparse random graphs.
** The bug fires on ~25% of random graphs in the bug report's
** original test, so hegel should hit a triggering case within
** a handful of test cases.
**
** EXPECTED SHRUNKEN COUNTEREXAMPLE
** ---------------------------------
** The theoretical minimum graph that hits all three conditions:
**   - 3 vertices total
**   - 1 undirected edge
**   - One isolated vertex (component A, ordered first, ordenum=0)
**   - One K_2 pair (component B, ordered second, ordenum=1)
**     which compresses
**
** Hegel's integrated shrinker should land near this — it operates
** on the byte stream behind the schema, so it can independently
** reduce nvert, the array length, and the edge endpoint values.
** With our schema (nvert in [3, 20], up to 30 edges), the shrunk
** result should have small nvert and small nedges.
**
** The Makefile assertion:
**   - exit must be non-zero (the assert fired)
**   - stderr must contain "MINIMAL nvert=N nedges=M ..." where
**     N <= 5 (allowing modest shrinker variance over the theoretical
**     minimum of 3) — see TESTS_SHRINK rule in the Makefile.
**
** This test does double duty:
**   - Real-world demo: "hegel-c finds a real bug in a real
**     production library."
**   - Shrink-quality demo: "integrated shrinking lands near
**     the theoretical minimum on a real failure."
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <scotch.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: types + CSR builder ---- */

typedef struct EdgePair {
  int                 u;
  int                 v;
} EdgePair;

typedef struct Graph {
  int                 nvert;
  EdgePair *          edges;
  int                 nedges;
} Graph;

#define MAX_VERT  20
#define MAX_EDGES 30

static hegel_schema_t graph_schema;

static
void
init_schemas (void)
{
  hegel_schema_t edge = HEGEL_STRUCT (EdgePair,
      HEGEL_INT (0, MAX_VERT - 1),
      HEGEL_INT (0, MAX_VERT - 1));

  graph_schema = HEGEL_STRUCT (Graph,
      HEGEL_INT (3, MAX_VERT),
      HEGEL_ARRAY_INLINE (edge, sizeof (EdgePair), 0, MAX_EDGES));
}

/* Build a valid undirected CSR from the logical graph:
**   - clamp endpoints into [0, nvert)
**   - strip self-loops
**   - dedupe
**   - symmetrize (each undirected pair emits both directions)
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

/* Property: SCOTCH_graphOrder with SCOTCH_STRATDISCONNECTED must
** produce a permtab that is a valid permutation of [0, nvert) —
** every value in range, no duplicates, all positions written.
**
** This SHOULD always hold but does not, due to the hgraphOrderCp
** bug.  When it fails, hegel prints the assertion message
** containing nvert and nedges so the test runner can verify the
** shrunken counterexample is small.
*/
static
void
test_graph_order (
hegel_testcase *            tc)
{
  Graph *             g;
  hegel_shape *       shape;
  SCOTCH_Num *        verttab = NULL;
  SCOTCH_Num *        edgetab = NULL;
  SCOTCH_Num *        permtab;
  int                 nedge = 0;
  int                 nvert;
  int                 rc;
  int                 i;
  int                 bad_idx = -1;
  int                 bad_val = 0;
  const char *        bad_reason = NULL;
  char *              seen;
  SCOTCH_Graph        grafdat;
  SCOTCH_Strat        stradat;
  SCOTCH_Num          cblknbr = 0;

  shape = hegel_schema_draw (tc, graph_schema, (void **) &g);
  nvert = g->nvert;

  /* Annotate the test case with the graph shape so the SHRUNKEN
  ** counterexample is visible to the user (and to the Makefile
  ** assertion) regardless of whether the failure manifests as
  ** an invalid permutation or as a SIGSEGV from inside Scotch.
  ** hegel_note prints only on the final replay — i.e., only after
  ** hegel has shrunk to a minimal failing case. */
  {
    char note_buf[512];
    int  off = 0;
    off += snprintf (note_buf + off, sizeof (note_buf) - off,
                     "MINIMAL nvert=%d nedges=%d edges=[",
                     nvert, g->nedges);
    for (i = 0; i < g->nedges && off < (int) sizeof (note_buf) - 16; i++) {
      off += snprintf (note_buf + off, sizeof (note_buf) - off,
                       "(%d,%d)%s", g->edges[i].u, g->edges[i].v,
                       (i + 1 < g->nedges) ? "," : "");
    }
    snprintf (note_buf + off, sizeof (note_buf) - off, "]");
    hegel_note (tc, note_buf);
  }

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
  /* Build the strategy with SCOTCH_STRATDISCONNECTED — this is
  ** the flag that triggers the buggy hgraphOrderCp code path.
  ** Args 3 and 0.2 are taken verbatim from the bug report's
  ** reproducer #1. */
  SCOTCH_stratGraphOrderBuild (&stradat, SCOTCH_STRATDISCONNECTED, 3, 0.2);

  permtab = calloc ((size_t) nvert, sizeof (SCOTCH_Num));
  memset (permtab, 0xBB, (size_t) nvert * sizeof (SCOTCH_Num));

  /* This call may segfault outright on some inputs (the bug
  ** dereferences uninitialized memory; whether that crashes
  ** depends on heap layout / ASLR).  Hegel's fork-mode catches
  ** the segfault as a property failure regardless. */
  rc = SCOTCH_graphOrder (&grafdat, &stradat, permtab, NULL,
                          &cblknbr, NULL, NULL);
  HEGEL_ASSERT (rc == 0,
                "SCOTCH_graphOrder failed (rc=%d) nvert=%d nedge=%d",
                rc, nvert, nedge);

  /* Validate the permutation: every value in [0, nvert), no dups. */
  seen = calloc ((size_t) nvert, 1);
  for (i = 0; i < nvert; i++) {
    SCOTCH_Num v = permtab[i];
    if (v < 0 || v >= nvert) {
      bad_idx = i;
      bad_val = (int) v;
      bad_reason = "out_of_range";
      break;
    }
    if (seen[v]) {
      bad_idx = i;
      bad_val = (int) v;
      bad_reason = "duplicate";
      break;
    }
    seen[v] = 1;
  }
  free (seen);

  if (bad_reason != NULL) {
    /* Build a parseable failure message — the Makefile greps the
    ** "MINIMAL nvert=N nedges=M" prefix to verify shrink quality. */
    char msg[256];
    snprintf (msg, sizeof (msg),
              "MINIMAL nvert=%d nedges=%d permtab[%d]=%d (%s)",
              nvert, g->nedges, bad_idx, bad_val, bad_reason);
    /* Free Scotch resources before failing — fork-mode means we're
    ** about to _exit, but cleanup keeps ASAN/valgrind quiet. */
    free (permtab);
    SCOTCH_stratExit (&stradat);
    SCOTCH_graphExit (&grafdat);
    free (verttab);
    free (edgetab);
    hegel_shape_free (shape);
    hegel_fail (msg);
  }

  free (permtab);
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
  SCOTCH_randomReset ();
}

int
main (void)
{
  init_schemas ();
  hegel_set_case_setup (seed_scotch);
  /* 200 cases is more than enough — at ~25% trigger rate, hegel
  ** should hit a failing case within the first ~5 attempts and
  ** then spend the rest on shrinking. */
  hegel_run_test_n (test_graph_order, 200);
  hegel_schema_free (graph_schema);
  return (0);
}
