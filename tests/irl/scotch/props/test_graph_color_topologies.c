/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphColor produces a proper coloring across all topologies.
**
** For every edge (u, v), colotab[u] != colotab[v]. Every color in [0, colonbr).
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_color_topologies (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Num * colotab = NULL;
    int graf_inited = 0;

    if (csr_gen_any (tc, &g, 12) != 0) { hegel_health_fail ("CSR alloc"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    colotab = malloc ((size_t) g.n * sizeof *colotab);
    if (colotab == NULL) { hegel_health_fail ("colotab"); goto cleanup; }

    SCOTCH_Num colonbr = 0;
    int rc = SCOTCH_graphColor (&grafdat, colotab, &colonbr, 0);
    HEGEL_ASSERT (rc == 0, "graphColor rc=%d topo=%s", rc, csr_topo_name (g.topology));

    /* Per Scotch's own check (test_scotch_graph_color.c) colors are in
    ** [0, colonbr) regardless of baseval. */
    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (colotab[i] >= 0 && colotab[i] < colonbr,
                      "color %ld out of range [0,%ld) at vertex %d (topo=%s)",
                      (long) colotab[i], (long) colonbr, i, csr_topo_name (g.topology));
    }

    /* Adjacent vertices have different colors. We re-walk the CSR. */
    for (int i = 0; i < g.n; i++) {
        SCOTCH_Num lo_e = g.verttab[i] - (SCOTCH_Num) g.baseval;
        SCOTCH_Num hi_e = g.verttab[i+1] - (SCOTCH_Num) g.baseval;
        for (SCOTCH_Num e = lo_e; e < hi_e; e++) {
            int j = (int) (g.edgetab[e] - (SCOTCH_Num) g.baseval);
            HEGEL_ASSERT (colotab[i] != colotab[j],
                          "improper coloring: %d—%d both colored %ld (topo=%s)",
                          i, j, (long) colotab[i], csr_topo_name (g.topology));
        }
    }

cleanup:
    free (colotab);
    if (graf_inited) SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_color_topologies, 1000);
    return 0;
}
