/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphPart returns success and produces a valid partition
** assignment across all generated topologies, with default strategy.
**
** Property checks:
**   - rc == 0
**   - parttab[i] in [0, partnbr) for every vertex i
**   - when partnbr == 1, every vertex must land in part 0
**
** The topology selector and one-bit-per-edge construction follow the skill's
** "non-local invariant" recipe.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_part_topologies (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 16) != 0) { hegel_health_fail ("CSR alloc"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    int partnbr = HEGEL_DRAW_INT (1, (int) g.n);

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) {
        hegel_health_fail ("graphCheck rejected our graph"); goto cleanup;
    }

    if (SCOTCH_stratInit (&stradat) != 0) {
        hegel_health_fail ("stratInit"); goto cleanup;
    }
    strat_inited = 1;

    parttab = malloc ((size_t) g.n * sizeof *parttab);
    if (parttab == NULL) { hegel_health_fail ("parttab"); goto cleanup; }
    for (int i = 0; i < g.n; i++) parttab[i] = -42;

    int rc = SCOTCH_graphPart (&grafdat, (SCOTCH_Num) partnbr, &stradat, parttab);
    HEGEL_ASSERT (rc == 0,
                  "graphPart rc=%d topology=%s n=%ld partnbr=%d baseval=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, partnbr, g.baseval);

    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "vertex %d got part %ld (partnbr=%d, topology=%s)",
                      i, (long) parttab[i], partnbr, csr_topo_name (g.topology));
    }

    if (partnbr == 1) {
        for (int i = 0; i < g.n; i++) {
            HEGEL_ASSERT (parttab[i] == 0,
                          "partnbr=1 but vertex %d got part %ld",
                          i, (long) parttab[i]);
        }
    }

cleanup:
    free (parttab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_part_topologies, 1000);
    return 0;
}
