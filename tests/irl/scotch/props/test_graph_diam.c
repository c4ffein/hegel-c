/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphDiamPV (pseudo-diameter via BFS) returns a non-negative
** value bounded by n-1 for a graph with n vertices.
**
** A graph's diameter (longest shortest path) is at most n-1; a BFS-based
** pseudo-diameter must respect that. For disconnected graphs the documented
** behavior is component-local; we accept any value >= 0.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_diam (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    int graf_inited = 0;

    if (csr_gen_any (tc, &g, 12) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("build"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    SCOTCH_Num d = SCOTCH_graphDiamPV (&grafdat);
    HEGEL_ASSERT (d >= 0,
                  "graphDiamPV returned %ld (topo=%s n=%ld)",
                  (long) d, csr_topo_name (g.topology), (long) g.n);
    /* Empirically Scotch returns INT_MAX (2147483647) for disconnected
    ** graphs as a sentinel for "infinite" — see FINDING #1 in REPORT.md.
    ** Even random topologies can produce disconnected graphs, so accept
    ** either a finite value bounded by n or the INT_MAX sentinel. */
    HEGEL_ASSERT (d == 2147483647 || d <= g.n,
                  "diameter %ld unusual (topo=%s n=%ld)",
                  (long) d, csr_topo_name (g.topology), (long) g.n);

cleanup:
    if (graf_inited) SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_diam, 1000);
    return 0;
}
