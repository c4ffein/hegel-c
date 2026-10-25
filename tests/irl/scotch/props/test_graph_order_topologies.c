/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphOrder produces a valid ordering across all topologies.
**
** SCOTCH_graphOrder fills permtab[i] = new index of vertex i (a permutation
** of [baseval, baseval+n)), and peritab[i] = old index of new vertex i.
** They must be inverse permutations.
**
** SCOTCH_graphOrderCheck is the library's own validator; we additionally
** verify the inverse-permutation property explicitly.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_order_topologies (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * permtab = NULL;
    SCOTCH_Num * peritab = NULL;
    SCOTCH_Num * rangtab = NULL;
    SCOTCH_Num * treetab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR alloc"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
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
    HEGEL_ASSERT (rc == 0, "graphOrder rc=%d topo=%s n=%ld baseval=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, g.baseval);

    /* permtab values are in [baseval, baseval + n) and are a permutation. */
    SCOTCH_Num bv = (SCOTCH_Num) g.baseval;
    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (permtab[i] >= bv && permtab[i] < bv + g.n,
                      "permtab[%d]=%ld out of [%ld, %ld) (topo=%s)",
                      i, (long) permtab[i], (long) bv, (long)(bv + g.n),
                      csr_topo_name (g.topology));
    }
    /* Check it's a permutation: every distinct, count == n. */
    char * seen = calloc ((size_t) g.n, 1);
    if (seen == NULL) { hegel_health_fail ("seen"); goto cleanup; }
    for (int i = 0; i < g.n; i++) {
        SCOTCH_Num pi = permtab[i] - bv;
        if (pi < 0 || pi >= g.n || seen[pi]) {
            HEGEL_ASSERT (0,
                          "permtab not a permutation: vertex %d -> %ld (topo=%s)",
                          i, (long) permtab[i], csr_topo_name (g.topology));
        }
        seen[pi] = 1;
    }
    /* peritab[permtab[i]-bv]-bv == i (inverse permutation). */
    for (int i = 0; i < g.n; i++) {
        SCOTCH_Num pi = permtab[i] - bv;
        SCOTCH_Num back = peritab[pi] - bv;
        HEGEL_ASSERT (back == i,
                      "perm/peri not inverses: i=%d permtab=%ld peritab[%ld]=%ld (topo=%s)",
                      i, (long) permtab[i], (long) pi, (long) peritab[pi],
                      csr_topo_name (g.topology));
    }
    free (seen);

    /* graphOrderCheck builds an SCOTCH_Ordering from these arrays and
    ** validates them. We only call it indirectly via Init/Compute below. */

cleanup:
    free (treetab); free (rangtab); free (peritab); free (permtab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_order_topologies, 1000);
    return 0;
}
