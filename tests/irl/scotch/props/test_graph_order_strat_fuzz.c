/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphOrder with a fuzzed strategy (constructed via
** SCOTCH_stratGraphOrderBuild with random flag combinations) returns 0 and
** produces a permtab that is a proper permutation.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_order_strat_fuzz (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * permtab = NULL;
    SCOTCH_Num * peritab = NULL;
    SCOTCH_Num * rangtab = NULL;
    SCOTCH_Num * treetab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    int srb = scotch_strat_order_build (tc, &stradat);
    if (srb != 0) { hegel_assume (tc, 0); goto cleanup; }
    strat_inited = 1;

    permtab = malloc ((size_t) g.n * sizeof *permtab);
    peritab = malloc ((size_t) g.n * sizeof *peritab);
    rangtab = malloc ((size_t)(g.n + 1) * sizeof *rangtab);
    treetab = malloc ((size_t) g.n * sizeof *treetab);
    if (!permtab || !peritab || !rangtab || !treetab) {
        hegel_health_fail ("alloc"); goto cleanup;
    }

    SCOTCH_Num cblknbr = 0;
    int rc = SCOTCH_graphOrder (&grafdat, &stradat, permtab, peritab,
                                  &cblknbr, rangtab, treetab);
    HEGEL_ASSERT (rc == 0,
                  "graphOrder fuzz rc=%d topo=%s n=%ld",
                  rc, csr_topo_name (g.topology), (long) g.n);

    SCOTCH_Num bv = (SCOTCH_Num) g.baseval;
    char * seen = calloc ((size_t) g.n, 1);
    if (seen == NULL) { hegel_health_fail ("seen"); goto cleanup; }
    for (int i = 0; i < g.n; i++) {
        SCOTCH_Num pi = permtab[i] - bv;
        HEGEL_ASSERT (pi >= 0 && pi < g.n && !seen[pi],
                      "permtab not a permutation: vertex %d -> %ld (topo=%s)",
                      i, (long) permtab[i], csr_topo_name (g.topology));
        seen[pi] = 1;
    }
    free (seen);

cleanup:
    free (treetab); free (rangtab); free (peritab); free (permtab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_order_strat_fuzz, 1000);
    return 0;
}
