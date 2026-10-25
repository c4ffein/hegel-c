/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphPart produces parts whose sizes are roughly balanced.
**
** Default strategy in Scotch aims at uniform partitioning. For n vertices and
** k parts, every part should have at most ceil(n / k) + slack vertices. We
** use a generous slack (3x) — the goal isn't to test optimization quality,
** it's to catch wild imbalances (e.g. one part empty + one part = n) that
** would point at a real bug.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_part_balance (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    SCOTCH_Num * counts  = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 16) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 4) { hegel_assume (tc, 0); goto cleanup; }     /* balance tests need volume */

    int partnbr = HEGEL_DRAW_INT (2, (int) g.n);

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("build"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
    strat_inited = 1;

    parttab = malloc ((size_t) g.n * sizeof *parttab);
    counts  = calloc ((size_t) partnbr, sizeof *counts);
    if (parttab == NULL || counts == NULL) { hegel_health_fail ("alloc"); goto cleanup; }

    int rc = SCOTCH_graphPart (&grafdat, (SCOTCH_Num) partnbr, &stradat, parttab);
    HEGEL_ASSERT (rc == 0, "graphPart rc=%d", rc);

    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "out-of-range part assignment");
        counts[parttab[i]]++;
    }

    /* Total accounted for. */
    SCOTCH_Num tot = 0;
    for (int p = 0; p < partnbr; p++) tot += counts[p];
    HEGEL_ASSERT (tot == g.n, "totals: %ld != %ld", (long) tot, (long) g.n);

    /* Loose balance: max part size <= 3 * ceil(n/partnbr) + 1.
    ** Default Scotch is supposed to balance — wild imbalance is suspicious. */
    SCOTCH_Num target = (g.n + partnbr - 1) / partnbr;
    SCOTCH_Num maxc = 0;
    for (int p = 0; p < partnbr; p++) if (counts[p] > maxc) maxc = counts[p];
    SCOTCH_Num bound = 3 * target + 1;
    HEGEL_ASSERT (maxc <= bound,
                  "balance: max part size %ld > %ld (target=%ld, n=%ld, partnbr=%d, topo=%s)",
                  (long) maxc, (long) bound, (long) target, (long) g.n,
                  partnbr, csr_topo_name (g.topology));

cleanup:
    free (counts); free (parttab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_part_balance, 500);
    return 0;
}
