/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphRepart accepts a starting partition and returns
** a valid new partition.
**
** SCOTCH_graphRepart takes an old parttab (any seed values), a migration cost
** factor, optional vmlotab (cost-of-moving-each-vertex), and computes a new
** partition. We assert: rc == 0 and parttab[i] in [0, partnbr).
**
** This exercises a code path mostly used in adaptive mesh refinement and
** that other partitioning tests don't cover.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_repart (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 12) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    int partnbr = HEGEL_DRAW_INT (1, (int) g.n);

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
    strat_inited = 1;

    parttab = malloc ((size_t) g.n * sizeof *parttab);
    if (parttab == NULL) { hegel_health_fail ("parttab"); goto cleanup; }

    /* Seed parttab with valid initial assignments — choice of values
    ** influences the migration heuristic. Each vertex independently. */
    for (int i = 0; i < g.n; i++) {
        parttab[i] = (SCOTCH_Num) HEGEL_DRAW_INT (0, partnbr - 1);
    }

    /* Migration cost factor: 0.0 = ignore, larger = penalize moves. */
    double emraval = (double) HEGEL_DRAW_INT (0, 100) / 10.0;
    /* Vertex migration costs: NULL = uniform. */
    SCOTCH_Num * vmlotab = NULL;

    /* signature: SCOTCH_graphRepart(graph, partnbr, parttab, emraval, vmlotab, strat, partarrtab) */
    /* Per scotch.h, the last param is again parttab as output ... actually, let me
    ** double-check by rereading the doc. The signature is:
    **   int SCOTCH_graphRepart(SCOTCH_Graph *, const SCOTCH_Num, SCOTCH_Num *,
    **       const double, const SCOTCH_Num *, SCOTCH_Strat *, SCOTCH_Num *)
    ** so probably parttab + parotab (old partition). */

    /* Save the seed before passing it as the "old" partition. */
    SCOTCH_Num * parotab = malloc ((size_t) g.n * sizeof *parotab);
    if (parotab == NULL) { free (parttab); hegel_health_fail ("parotab"); goto cleanup; }
    for (int i = 0; i < g.n; i++) parotab[i] = parttab[i];

    /* Some Scotch versions document SCOTCH_graphRepart as
    **   (graph, archnbr, parotab, emraval, vmlotab, strat, parttab)
    ** with parotab read-only and parttab the output. */
    int rc = SCOTCH_graphRepart (&grafdat, (SCOTCH_Num) partnbr,
                                  parotab, emraval, vmlotab,
                                  &stradat, parttab);
    free (parotab);
    HEGEL_ASSERT (rc == 0,
                  "graphRepart rc=%d topo=%s n=%ld partnbr=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, partnbr);

    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "vertex %d got part %ld (partnbr=%d, topo=%s)",
                      i, (long) parttab[i], partnbr, csr_topo_name (g.topology));
    }

cleanup:
    free (parttab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_repart, 1000);
    return 0;
}
