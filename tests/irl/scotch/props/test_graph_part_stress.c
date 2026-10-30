/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Stress tier: SCOTCH_graphPart on 50-200 vertex graphs across topologies.
**
** The "small" tests (n <= 16) are great at catching edge-case bugs but miss
** scaling bugs — integer overflow in intermediate sums, allocator pressure,
** quadratic blow-ups. This test runs on bigger inputs at lower case counts
** to surface those.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_part_stress (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 80) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 4) { hegel_assume (tc, 0); goto cleanup; }

    int partnbr = HEGEL_DRAW_INT (2, (int) g.n / 2);
    if (partnbr < 2) { hegel_assume (tc, 0); goto cleanup; }

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
    HEGEL_ASSERT (rc == 0,
                  "graphPart rc=%d topology=%s n=%ld partnbr=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, partnbr);

    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "vertex %d got part %ld (partnbr=%d, topology=%s)",
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
    /* 200 cases on up-to-80-vertex graphs — slower per case but catches
    ** scaling pathologies that 12-vertex tests miss. */
    hegel_run_test_n (test_graph_part_stress, 200);
    return 0;
}
