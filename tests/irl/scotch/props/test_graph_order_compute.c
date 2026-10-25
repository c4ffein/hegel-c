/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphOrderCompute (the Ordering-object API) produces an
** ordering that SCOTCH_graphOrderCheck validates.
**
** This is a different code path from SCOTCH_graphOrder (which wraps the
** Ordering internally). The Ordering object exposes Init/Compute/Check/Exit
** explicitly; users hold the permtab and friends in their own buffers.
**
** SCOTCH_graphOrderCheck is the library's own validator — if it disagrees
** with us, that's a bug. Any non-zero return from check is a property failure.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_order_compute (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Ordering ordedat;
    SCOTCH_Num * permtab = NULL;
    SCOTCH_Num * peritab = NULL;
    SCOTCH_Num * rangtab = NULL;
    SCOTCH_Num * treetab = NULL;
    int graf_inited = 0, strat_inited = 0, ord_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
    strat_inited = 1;
    /* The default strategy, or one of a few simple ones.  "o" is here because a
    ** 2026-04 claim said its ordering fails SCOTCH_graphOrderCheck (not reproduced). */
    static const char * strats[] = { "", "o", "g", "n{sep=g}" };
    const char * strat = strats[HEGEL_DRAW_INT (0, 3)];
    if (strat[0] != '\0' && SCOTCH_stratGraphOrder (&stradat, strat) != 0) {
        hegel_health_fail ("stratGraphOrder"); goto cleanup;
    }

    permtab = malloc ((size_t) g.n * sizeof *permtab);
    peritab = malloc ((size_t) g.n * sizeof *peritab);
    rangtab = malloc ((size_t)(g.n + 1) * sizeof *rangtab);
    treetab = malloc ((size_t) g.n * sizeof *treetab);
    if (!permtab || !peritab || !rangtab || !treetab) {
        hegel_health_fail ("alloc"); goto cleanup;
    }

    SCOTCH_Num cblknbr = 0;
    int oi = SCOTCH_graphOrderInit (&grafdat, &ordedat,
                                     permtab, peritab, &cblknbr,
                                     rangtab, treetab);
    HEGEL_ASSERT (oi == 0,
                  "graphOrderInit rc=%d topo=%s n=%ld",
                  oi, csr_topo_name (g.topology), (long) g.n);
    ord_inited = 1;

    int rc = SCOTCH_graphOrderCompute (&grafdat, &ordedat, &stradat);
    HEGEL_ASSERT (rc == 0,
                  "graphOrderCompute strat='%s' rc=%d topo=%s n=%ld",
                  strat, rc, csr_topo_name (g.topology), (long) g.n);

    /* Use the library's own validator. */
    int chk = SCOTCH_graphOrderCheck (&grafdat, &ordedat);
    HEGEL_ASSERT (chk == 0,
                  "graphOrderCheck rejected the ordering strat='%s' rc=%d topo=%s n=%ld baseval=%d",
                  strat, chk, csr_topo_name (g.topology), (long) g.n, g.baseval);

cleanup:
    if (ord_inited)   SCOTCH_graphOrderExit (&grafdat, &ordedat);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    free (treetab); free (rangtab); free (peritab); free (permtab);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_order_compute, 1000);
    return 0;
}
