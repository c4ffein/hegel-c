/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphCoarsen produces a valid coarser graph.
**
** Documented contract:
**   - rc 0 means coarser graph built successfully
**   - rc 1 means input graph is already too small to coarsen further
**   - the produced graph passes graphCheck and has at most n vertices
**
** We test the rc-0 branch and assert size invariant.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_coarsen (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph parent;
    SCOTCH_Graph coarsened;
    SCOTCH_Num * mate = NULL;
    int parent_inited = 0, coarsen_inited = 0;

    if (csr_gen_any (tc, &g, 12) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 2) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &parent) != 0) {
        hegel_health_fail ("build"); goto cleanup;
    }
    parent_inited = 1;
    if (SCOTCH_graphCheck (&parent) != 0) { hegel_health_fail ("check parent"); goto cleanup; }

    if (SCOTCH_graphInit (&coarsened) != 0) { hegel_health_fail ("init"); goto cleanup; }
    coarsen_inited = 1;

    /* coarmulttab is a multinode array: 2 SCOTCH_Num entries per coarse vertex.
    ** Worst case: every fine vertex is its own multinode singleton (n coarse
    ** vertices), so allocate 2 * n. */
    mate = malloc ((size_t)(2 * g.n + 4) * sizeof *mate);
    if (mate == NULL) { hegel_health_fail ("mate"); goto cleanup; }

    /* Coarsening: target_size = n/2, balance = 0.7 (typical), flag 0. */
    SCOTCH_Num target = (g.n + 1) / 2;
    int rc = SCOTCH_graphCoarsen (&parent, target, 0.7, 0, &coarsened, mate);
    /* rc 0 = coarsened ok, rc 1 = could not coarsen further (acceptable). */
    HEGEL_ASSERT (rc == 0 || rc == 1,
                  "graphCoarsen rc=%d topo=%s n=%ld",
                  rc, csr_topo_name (g.topology), (long) g.n);

    if (rc == 0) {
        int crc = SCOTCH_graphCheck (&coarsened);
        HEGEL_ASSERT (crc == 0,
                      "coarsened graph fails check (rc=%d) topo=%s",
                      crc, csr_topo_name (g.topology));
        SCOTCH_Num cn = 0, cm = 0;
        SCOTCH_graphSize (&coarsened, &cn, &cm);
        HEGEL_ASSERT (cn <= g.n,
                      "coarsened n=%ld > parent n=%ld",
                      (long) cn, (long) g.n);
    }

cleanup:
    free (mate);
    if (coarsen_inited) SCOTCH_graphExit (&coarsened);
    if (parent_inited)  SCOTCH_graphExit (&parent);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_coarsen, 1000);
    return 0;
}
