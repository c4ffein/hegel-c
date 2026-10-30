/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphCheck accepts every well-formed graph our generators
** produce, across all topologies and both baseval choices.
**
** This is a meta-test for the generator infrastructure: if our generators
** produce graphs that graphCheck rejects, all downstream tests are wasting
** their case budget. graphCheck is also the public validity oracle Scotch
** users rely on, so its acceptance over the topology zoo is itself a sanity
** invariant.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_check_topologies (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    int graf_inited = 0;

    if (csr_gen_any (tc, &g, 12) != 0) { hegel_health_fail ("CSR alloc failed"); goto cleanup; }

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("SCOTCH_graphBuild failed"); goto cleanup;
    }
    graf_inited = 1;

    int rc = SCOTCH_graphCheck (&grafdat);
    HEGEL_ASSERT (rc == 0,
                  "graphCheck rejected topology %s n=%ld baseval=%d (rc=%d)",
                  csr_topo_name (g.topology), (long) g.n, g.baseval, rc);

    /* SCOTCH_graphSize must agree with what we built. */
    SCOTCH_Num gn = 0, gm = 0;
    SCOTCH_graphSize (&grafdat, &gn, &gm);
    HEGEL_ASSERT (gn == g.n, "graphSize n: got %ld, expected %ld",
                  (long) gn, (long) g.n);
    HEGEL_ASSERT (gm == g.edgenbr, "graphSize m: got %ld, expected %ld",
                  (long) gm, (long) g.edgenbr);

cleanup:
    if (graf_inited) SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_check_topologies, 1000);
    return 0;
}
