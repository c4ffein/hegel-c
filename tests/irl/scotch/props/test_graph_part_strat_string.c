/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphPart with a strategy parsed from a Scotch strategy
** string (via SCOTCH_stratGraphMap) returns 0 and produces a valid partition.
**
** This tests a different code path from stratGraphMapBuild — the actual
** strategy-string parser. Strings here are limited to a handful of known-good
** primitives. If the parser rejects, we discard via hegel_assume (parser
** rejection is a test issue, not a library bug).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

/* Strategy strings the parser accepts AND that work for graph mapping.
** Most of the Scotch user-manual ordering examples are NOT valid mapping
** strategies (different parsers — SCOTCH_stratGraphMap vs
** SCOTCH_stratGraphOrder). Empty = default. */
static const char * STRAT_STRINGS[] = {
    "",                                      /* default */
    "r{sep=g{bal=0.1},asc=b{bnd=f}}",        /* recursive bipart with FM */
    "r{sep=g}",                              /* simple greedy bisection */
    /* NOTE: "f" alone causes SIGSEGV — see REPORT.md Bug #2. */
};
#define NSTRATS (sizeof STRAT_STRINGS / sizeof STRAT_STRINGS[0])

static void test_graph_part_strat_string (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * parttab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }
    int partnbr = HEGEL_DRAW_INT (1, (int) g.n);
    int idx = HEGEL_DRAW_INT (0, (int) NSTRATS - 1);
    const char * sstr = STRAT_STRINGS[idx];

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("build"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
    strat_inited = 1;

    if (sstr[0] != 0) {
        int srb = SCOTCH_stratGraphMap (&stradat, sstr);
        if (srb != 0) {
            /* Parser rejected — discard. */
            hegel_assume (tc, 0); goto cleanup;
        }
    }

    parttab = malloc ((size_t) g.n * sizeof *parttab);
    if (parttab == NULL) { hegel_health_fail ("parttab"); goto cleanup; }

    int rc = SCOTCH_graphPart (&grafdat, (SCOTCH_Num) partnbr, &stradat, parttab);
    HEGEL_ASSERT (rc == 0,
                  "graphPart with strat='%s' rc=%d topo=%s n=%ld partnbr=%d",
                  sstr, rc, csr_topo_name (g.topology), (long) g.n, partnbr);

    for (int i = 0; i < g.n; i++) {
        HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < (SCOTCH_Num) partnbr,
                      "vertex %d got part %ld (partnbr=%d, strat='%s')",
                      i, (long) parttab[i], partnbr, sstr);
    }

cleanup:
    free (parttab);
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_part_strat_string, 1000);
    return 0;
}
