/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphMapFixed honors fixed-vertex assignments when
** mapping onto an architecture.
**
** Companion of test_graph_part_fixed but using SCOTCH_graphMapFixed +
** SCOTCH_archCmplt. Documented contract: parttab[i] == -1 means "free",
** anything in [0, archSize) is a fixed assignment that must survive.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_map_fixed (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Arch archdat;
    SCOTCH_Num * parttab = NULL;
    SCOTCH_Num * fixed_copy = NULL;
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

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
    strat_inited = 1;

    parttab    = malloc ((size_t) g.n * sizeof *parttab);
    fixed_copy = malloc ((size_t) g.n * sizeof *fixed_copy);
    if (parttab == NULL || fixed_copy == NULL) {
        hegel_health_fail ("alloc"); goto cleanup;
    }

    /* Initialise: each vertex independently fixed or free. */
    for (int i = 0; i < g.n; i++) {
        int do_fix = HEGEL_DRAW_INT (0, 1);
        if (do_fix) {
            parttab[i] = HEGEL_DRAW_INT (0, partnbr - 1);
        } else {
            parttab[i] = -1;
        }
        fixed_copy[i] = parttab[i];
    }

    int rc = SCOTCH_graphMapFixed (&grafdat, &archdat, &stradat, parttab);
    HEGEL_ASSERT (rc == 0,
                  "graphMapFixed rc=%d topo=%s n=%ld partnbr=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, partnbr);

    /* Property 1: every parttab[i] in [0, partnbr). */
    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "vertex %d got part %ld (partnbr=%d, topo=%s)",
                      i, (long) parttab[i], partnbr, csr_topo_name (g.topology));
    }

    /* Property 2: fixed entries survive unchanged. */
    for (int i = 0; i < g.n; i++) {
        if (fixed_copy[i] >= 0) {
            HEGEL_ASSERT (parttab[i] == fixed_copy[i],
                          "fixed vertex %d: was %ld, now %ld (topo=%s)",
                          i, (long) fixed_copy[i], (long) parttab[i],
                          csr_topo_name (g.topology));
        }
    }

cleanup:
    free (fixed_copy); free (parttab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (arch_inited)  SCOTCH_archExit (&archdat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_map_fixed, 1000);
    return 0;
}
