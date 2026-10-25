/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphInducePart with a 2-coloring produces a valid
** subgraph containing exactly the vertices selected by the partval matching.
**
** SCOTCH_graphInducePart takes a parttab of SCOTCH_GraphPart2 (= byte) values
** and a partval to keep; it returns the induced subgraph on the kept vertices.
**
** Properties:
**   - rc == 0
**   - induced graph passes graphCheck
**   - induced n == count of vertices with parttab[i] == partval
**   - induced m <= parent m
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_induce_part (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph parent;
    SCOTCH_Graph child;
    SCOTCH_GraphPart2 * parttab = NULL;
    int parent_inited = 0, child_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &parent) != 0) {
        hegel_health_fail ("parent build"); goto cleanup;
    }
    parent_inited = 1;
    if (SCOTCH_graphCheck (&parent) != 0) { hegel_health_fail ("check"); goto cleanup; }

    parttab = malloc ((size_t) g.n * sizeof *parttab);
    if (parttab == NULL) { hegel_health_fail ("parttab"); goto cleanup; }

    /* Two-color the vertices independently — one shrinkable bit per vertex. */
    SCOTCH_Num kept_target = 0;
    SCOTCH_GraphPart2 keep_val = (SCOTCH_GraphPart2) HEGEL_DRAW_INT (0, 1);
    for (int i = 0; i < g.n; i++) {
        parttab[i] = (SCOTCH_GraphPart2) HEGEL_DRAW_INT (0, 1);
        if (parttab[i] == keep_val) kept_target++;
    }
    /* Need at least one kept vertex — Scotch may reject empty. */
    if (kept_target == 0) { hegel_assume (tc, 0); goto cleanup; }

    if (SCOTCH_graphInit (&child) != 0) { hegel_health_fail ("childInit"); goto cleanup; }
    child_inited = 1;

    int rc = SCOTCH_graphInducePart (&parent, kept_target, parttab, keep_val, &child);
    HEGEL_ASSERT (rc == 0,
                  "graphInducePart rc=%d topo=%s n=%ld kept=%ld val=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, (long) kept_target, keep_val);

    int crc = SCOTCH_graphCheck (&child);
    HEGEL_ASSERT (crc == 0,
                  "induced graph fails check rc=%d topo=%s",
                  crc, csr_topo_name (g.topology));

    SCOTCH_Num cn = 0, cm = 0;
    SCOTCH_graphSize (&child, &cn, &cm);
    HEGEL_ASSERT (cn == kept_target,
                  "induced n: got %ld, expected %ld (topo=%s)",
                  (long) cn, (long) kept_target, csr_topo_name (g.topology));
    HEGEL_ASSERT (cm <= g.edgenbr,
                  "induced m=%ld > parent m=%ld",
                  (long) cm, (long) g.edgenbr);

cleanup:
    if (child_inited)  SCOTCH_graphExit (&child);
    if (parent_inited) SCOTCH_graphExit (&parent);
    free (parttab);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_induce_part, 1000);
    return 0;
}
