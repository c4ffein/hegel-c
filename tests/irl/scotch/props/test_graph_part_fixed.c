/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphPartFixed honors fixed-vertex assignments.
**
** Given a parttab where some entries are pre-assigned to a specific part
** (>= 0) and the rest are -1 (free), graphPartFixed must:
**   - return 0
**   - leave fixed entries unchanged
**   - assign every free entry to a part in [0, partnbr)
**
** "Free entry must end in [0, partnbr)" is documented in the Scotch user
** manual; "fixed entries unchanged" is the entire point of the API.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_part_fixed (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    SCOTCH_Num * fixed_copy = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR alloc"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    int partnbr = HEGEL_DRAW_INT (1, (int) g.n);

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
    strat_inited = 1;

    parttab    = malloc ((size_t) g.n * sizeof *parttab);
    fixed_copy = malloc ((size_t) g.n * sizeof *fixed_copy);
    if (parttab == NULL || fixed_copy == NULL) {
        hegel_health_fail ("alloc"); goto cleanup;
    }

    /* Draw a fixed pattern: each vertex independently is fixed (with a value
    ** in [0, partnbr)) or free (-1). */
    for (int i = 0; i < g.n; i++) {
        int do_fix = HEGEL_DRAW_INT (0, 1);
        if (do_fix) {
            parttab[i] = HEGEL_DRAW_INT (0, partnbr - 1);
        } else {
            parttab[i] = -1;
        }
        fixed_copy[i] = parttab[i];
    }

    int rc = SCOTCH_graphPartFixed (&grafdat, (SCOTCH_Num) partnbr, &stradat, parttab);
    HEGEL_ASSERT (rc == 0,
                  "graphPartFixed rc=%d topology=%s n=%ld partnbr=%d",
                  rc, csr_topo_name (g.topology), (long) g.n, partnbr);

    /* Property 1: every parttab[i] is in [0, partnbr). */
    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "vertex %d got part %ld out of partnbr=%d (topo=%s)",
                      i, (long) parttab[i], partnbr, csr_topo_name (g.topology));
    }

    /* Property 2: fixed entries (>= 0 in fixed_copy) survived unchanged. */
    for (int i = 0; i < g.n; i++) {
        if (fixed_copy[i] >= 0) {
            HEGEL_ASSERT (parttab[i] == fixed_copy[i],
                          "fixed vertex %d: was %ld, now %ld",
                          i, (long) fixed_copy[i], (long) parttab[i]);
        }
    }

cleanup:
    free (fixed_copy); free (parttab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_part_fixed, 1000);
    return 0;
}
