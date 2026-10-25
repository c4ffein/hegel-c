/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphInduceList produces a valid sub-graph.
**
** Given a parent graph G and a list of vertex indices, the induced subgraph H
** must:
**   - have exactly |list| vertices
**   - have an edge count <= original edge count
**   - pass SCOTCH_graphCheck
**
** A common bug class: the induced graph references stale memory in the parent
** (Scotch documents that the parent must outlive the child). We don't free
** the parent until after we check the child.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_induce_list (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph parent;
    SCOTCH_Graph child;
    SCOTCH_Num * vlist = NULL;
    int parent_inited = 0, child_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &parent) != 0) {
        hegel_health_fail ("parent build"); goto cleanup;
    }
    parent_inited = 1;
    if (SCOTCH_graphCheck (&parent) != 0) { hegel_health_fail ("check parent"); goto cleanup; }

    /* Draw a strict subset of vertices: first decide each vertex's keep bit. */
    int kept = 0;
    char * keep = (char *) calloc ((size_t) g.n, 1);
    if (keep == NULL) { hegel_health_fail ("keep"); goto cleanup; }
    for (int i = 0; i < g.n; i++) {
        keep[i] = (char) HEGEL_DRAW_INT (0, 1);
        if (keep[i]) kept++;
    }
    if (kept == 0) { free (keep); hegel_assume (tc, 0); goto cleanup; }

    vlist = malloc ((size_t) kept * sizeof *vlist);
    if (vlist == NULL) { free (keep); hegel_health_fail ("vlist"); goto cleanup; }
    int idx = 0;
    for (int i = 0; i < g.n; i++) {
        if (keep[i]) vlist[idx++] = (SCOTCH_Num) i + (SCOTCH_Num) g.baseval;
    }
    free (keep);

    if (SCOTCH_graphInit (&child) != 0) { hegel_health_fail ("childInit"); goto cleanup; }
    child_inited = 1;

    int rc = SCOTCH_graphInduceList (&parent, (SCOTCH_Num) kept, vlist, &child);
    HEGEL_ASSERT (rc == 0,
                  "graphInduceList rc=%d topo=%s n=%ld kept=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, kept);

    /* Validate the child. */
    int crc = SCOTCH_graphCheck (&child);
    HEGEL_ASSERT (crc == 0,
                  "induced graph fails graphCheck (rc=%d) topo=%s kept=%d",
                  crc, csr_topo_name (g.topology), kept);

    /* Child has exactly `kept` vertices and at most edgenbr arcs. */
    SCOTCH_Num cn = 0, cm = 0;
    SCOTCH_graphSize (&child, &cn, &cm);
    HEGEL_ASSERT (cn == kept,
                  "induced n: got %ld, expected %d (topo=%s)",
                  (long) cn, kept, csr_topo_name (g.topology));
    HEGEL_ASSERT (cm <= g.edgenbr,
                  "induced m=%ld > parent m=%ld (topo=%s)",
                  (long) cm, (long) g.edgenbr, csr_topo_name (g.topology));

cleanup:
    free (vlist);
    if (child_inited)  SCOTCH_graphExit (&child);
    if (parent_inited) SCOTCH_graphExit (&parent);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_induce_list, 1000);
    return 0;
}
