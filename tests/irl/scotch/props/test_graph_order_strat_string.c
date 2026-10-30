/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphOrder with a strategy parsed via
** SCOTCH_stratGraphOrder (string-form) produces a valid permutation.
**
** This exercises the strategy-string parser for *ordering* (separate from
** the partitioning parser tested in test_graph_part_strat_string). Strings
** are picked from a small whitelist of forms documented in the user manual
** and known to work. Parser rejection is treated as a discard, not a failure.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

/* Ordering strategy whitelist. Empty = default. The non-empty entries are
** built from primitives described in the Scotch user manual chapter 7.5
** (graphOrder strategies). */
static const char * STRAT_STRINGS[] = {
    "",                                         /* default */
    "c{rat=0.7,cpr=h{pass=10}|n{sep=g}}",       /* compress + nested dissection */
    "n{sep=(g|m{type=h,vert=80,low=h{pass=10}}),ole=s,ose=s}",  /* nested dissection (manual ex.) */
    "o",                                        /* plain "o" (a 2026-04 claim said its output fails graphOrderCheck) */
};
#define NSTRATS (sizeof STRAT_STRINGS / sizeof STRAT_STRINGS[0])

static void test_graph_order_strat_string (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    SCOTCH_Strat stradat;
    SCOTCH_Num * permtab = NULL;
    SCOTCH_Num * peritab = NULL;
    SCOTCH_Num * rangtab = NULL;
    SCOTCH_Num * treetab = NULL;
    int graf_inited = 0, strat_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    int idx = HEGEL_DRAW_INT (0, (int) NSTRATS - 1);
    const char * sstr = STRAT_STRINGS[idx];

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("graphBuild"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    if (SCOTCH_stratInit (&stradat) != 0) { hegel_health_fail ("strat"); goto cleanup; }
    strat_inited = 1;

    if (sstr[0] != 0) {
        int srb = SCOTCH_stratGraphOrder (&stradat, sstr);
        if (srb != 0) { hegel_assume (tc, 0); goto cleanup; }   /* parser rejected */
    }

    permtab = malloc ((size_t) g.n * sizeof *permtab);
    peritab = malloc ((size_t) g.n * sizeof *peritab);
    rangtab = malloc ((size_t)(g.n + 1) * sizeof *rangtab);
    treetab = malloc ((size_t) g.n * sizeof *treetab);
    if (!permtab || !peritab || !rangtab || !treetab) {
        hegel_health_fail ("alloc"); goto cleanup;
    }

    SCOTCH_Num cblknbr = 0;
    int rc = SCOTCH_graphOrder (&grafdat, &stradat, permtab, peritab,
                                  &cblknbr, rangtab, treetab);
    HEGEL_ASSERT (rc == 0,
                  "graphOrder strat='%s' rc=%d topo=%s n=%ld",
                  sstr, rc, csr_topo_name (g.topology), (long) g.n);

    /* permtab is a permutation of [baseval, baseval+n) */
    SCOTCH_Num bv = (SCOTCH_Num) g.baseval;
    char * seen = calloc ((size_t) g.n, 1);
    if (seen == NULL) { hegel_health_fail ("seen"); goto cleanup; }
    for (int i = 0; i < g.n; i++) {
        SCOTCH_Num pi = permtab[i] - bv;
        HEGEL_ASSERT (pi >= 0 && pi < g.n && !seen[pi],
                      "permtab not a permutation: vertex %d -> %ld (strat='%s', topo=%s)",
                      i, (long) permtab[i], sstr, csr_topo_name (g.topology));
        seen[pi] = 1;
    }
    free (seen);

cleanup:
    if (strat_inited) SCOTCH_stratExit (&stradat);
    if (graf_inited)  SCOTCH_graphExit (&grafdat);
    free (treetab); free (rangtab); free (peritab); free (permtab);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_order_strat_string, 1000);
    return 0;
}
