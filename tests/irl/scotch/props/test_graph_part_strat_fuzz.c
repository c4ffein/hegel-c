/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphPart with a fuzzed strategy (constructed via
** SCOTCH_stratGraphMapBuild with random flag combinations) still returns 0
** and produces a valid partition.
**
** This is the "configuration sweep" pattern from the bug-hunting skill.
** Each case draws a topology, a partnbr, and a random subset of the
** SCOTCH_STRAT* flag bits. The shrinker will minimize the flag set and
** the topology when something fails.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_part_strat_fuzz (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 12) != 0) { hegel_health_fail ("CSR alloc"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    int partnbr = HEGEL_DRAW_INT (1, (int) g.n);

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) {
        hegel_health_fail ("graphCheck"); goto cleanup;
    }

    int srb = scotch_strat_part_build (tc, &stradat, (SCOTCH_Num) partnbr);
    if (srb != 0) {
        /* Strategy parser rejected — discard, not a library bug. */
        hegel_assume (tc, 0); goto cleanup;
    }
    strat_inited = 1;

    parttab = malloc ((size_t) g.n * sizeof *parttab);
    if (parttab == NULL) { hegel_health_fail ("parttab"); goto cleanup; }
    for (int i = 0; i < g.n; i++) parttab[i] = -42;

    int rc = SCOTCH_graphPart (&grafdat, (SCOTCH_Num) partnbr, &stradat, parttab);
    HEGEL_ASSERT (rc == 0,
                  "graphPart with fuzzed strat rc=%d topology=%s n=%ld partnbr=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, partnbr);

    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "fuzz: vertex %d got part %ld (partnbr=%d, topology=%s)",
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
    /* 5000, not 1000: the kgraphMapFm over-read (pin kgraphMapFmDomnnbr)
    ** needs SCOTCH_STRATBALANCE, a balance >= 0.67 and one of about half
    ** the other flag sets, on a graph and part count where the mapping
    ** goes out of balance — rare enough that 1000 cases found it on only
    ** ~70% of runs (10 of 14, 2026-10-30).  Once triggered, ASan catches
    ** it every time and it shrinks to the same case. */
    hegel_run_test_n (test_graph_part_strat_fuzz, 5000);
    return 0;
}
