/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Edge: SCOTCH_graphPart with partnbr > n. What happens?
**
** Documented behaviour: undefined; some libraries silently pad, others fail
** explicitly. We test that Scotch either:
**   - returns nonzero (rejecting the call), OR
**   - returns 0 with parttab[i] in [0, partnbr).
**
** A SIGSEGV / SIGABRT here is caught by fork mode and reported as a failure
** with a shrunk counterexample.
**
** Also tests partnbr == 0 (degenerate) and very-large partnbr (overflow probe).
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_part_edge_partnbr (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 8) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    /* Cycle through interesting partnbr values: 0, 1, n+1, 2n, large. */
    int variant = HEGEL_DRAW_INT (0, 4);
    int partnbr;
    switch (variant) {
    case 0: partnbr = 0;                       break;     /* degenerate */
    case 1: partnbr = 1;                       break;     /* baseline */
    case 2: partnbr = (int) g.n + 1;           break;     /* one more than n */
    case 3: partnbr = 2 * (int) g.n;           break;     /* much more */
    case 4: partnbr = 100 * (int) g.n;         break;     /* large */
    default: partnbr = (int) g.n;              break;
    }

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

    int rc = SCOTCH_graphPart (&grafdat, (SCOTCH_Num) partnbr, &stradat, parttab);

    /* Two acceptable outcomes:
    **   - rc != 0 (rejected) — fine.
    **   - rc == 0 — every parttab[i] must be in [0, partnbr).
    */
    if (rc == 0 && partnbr > 0) {
        for (int i = 0; i < g.n; i++) {
            HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                          "vertex %d got part %ld out of [0, %d) (topo=%s n=%ld partnbr=%d)",
                          i, (long) parttab[i], partnbr,
                          csr_topo_name (g.topology), (long) g.n, partnbr);
        }
    }
    /* No assertion for rc != 0 — that's the "rejected" branch and is
    ** acceptable. We're mainly probing for crashes. */

cleanup:
    free (parttab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_part_edge_partnbr, 1000);
    return 0;
}
