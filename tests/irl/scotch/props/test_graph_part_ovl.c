/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphPartOvl produces a valid overlap partitioning.
**
** Per the user manual, parttab[i] in [-1, partnbr): -1 means "in the overlap
** zone", otherwise it's a regular part id. We assert the looser constraint:
** parttab[i] >= -1 and parttab[i] < partnbr.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_part_ovl (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 2) { hegel_assume (tc, 0); goto cleanup; }     /* overlap requires >=2 verts */

    int partnbr = HEGEL_DRAW_INT (2, (int) g.n);     /* >=2 for "overlap" to be meaningful */

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
    strat_inited = 1;

    parttab = malloc ((size_t) g.n * sizeof *parttab);
    if (parttab == NULL) { hegel_health_fail ("parttab"); goto cleanup; }
    for (int i = 0; i < g.n; i++) parttab[i] = -42;

    int rc = SCOTCH_graphPartOvl (&grafdat, (SCOTCH_Num) partnbr, &stradat, parttab);
    HEGEL_ASSERT (rc == 0,
                  "graphPartOvl rc=%d topo=%s n=%ld partnbr=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, partnbr);

    /* Property: parttab[i] in [-1, partnbr). -1 means overlap. */
    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= -1 && parttab[i] < (SCOTCH_Num) partnbr,
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
    hegel_run_test_n (test_graph_part_ovl, 1000);
    return 0;
}
