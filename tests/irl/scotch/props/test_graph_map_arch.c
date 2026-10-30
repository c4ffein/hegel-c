/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphMap onto a complete-architecture (SCOTCH_archCmplt)
** returns 0 and produces a parttab in [0, partnbr).
**
** When mapped onto a complete graph of size k, the result is equivalent to
** a k-way partition. This exercises the SCOTCH_archCmplt + SCOTCH_graphMap
** path (different from SCOTCH_graphPart which special-cases complete archs
** internally).
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_map_arch (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Arch  archdat;
    SCOTCH_Num * parttab = NULL;
    int graf_inited = 0, strat_inited = 0, arch_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    int partnbr = HEGEL_DRAW_INT (1, (int) g.n);

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_archInit (&archdat) != 0) { hegel_health_fail ("archInit"); goto cleanup; }
    arch_inited = 1;
    if (SCOTCH_archCmplt (&archdat, (SCOTCH_Num) partnbr) != 0) {
        hegel_health_fail ("archCmplt"); goto cleanup;
    }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("stratInit"); goto cleanup; }
    strat_inited = 1;

    parttab = malloc ((size_t) g.n * sizeof *parttab);
    if (parttab == NULL) { hegel_health_fail ("parttab"); goto cleanup; }
    for (int i = 0; i < g.n; i++) parttab[i] = -42;

    int rc = SCOTCH_graphMap (&grafdat, &archdat, &stradat, parttab);
    HEGEL_ASSERT (rc == 0,
                  "graphMap rc=%d topo=%s n=%ld partnbr=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, partnbr);

    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "vertex %d got part %ld (partnbr=%d)",
                      i, (long) parttab[i], partnbr);
    }

cleanup:
    free (parttab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (arch_inited)  SCOTCH_archExit (&archdat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_map_arch, 1000);
    return 0;
}
