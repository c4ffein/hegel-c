/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphStat returns plausible graph statistics.
**
** Signature (see library_graph.c):
**   SCOTCH_graphStat(graph,
**       velomin, velomax, velosum, veloavg, velodlt,   -- vertex load
**       degrmin, degrmax, degravg, degrdlt,            -- degrees
**       edlomin, edlomax, edlosum, edloavg, edlodlt)   -- arc load
**
** Invariants we check:
**   - velomin <= velomax; veloavg in [velomin, velomax]
**   - velosum == n (unweighted vertices have load 1 each)
**   - degmin <= degmax; degavg in [degmin, degmax]
**   - degavg * n approximately equals edgenbr (sum of degrees = 2*|edges|)
**   - edlosum == edgenbr (unweighted arcs have load 1 each)
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_stat (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph grafdat;
    int graf_inited = 0;

    if (csr_gen_any (tc, &g, 12) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &grafdat) != 0) {
        hegel_health_fail ("build"); goto cleanup;
    }
    graf_inited = 1;
    if (SCOTCH_graphCheck (&grafdat) != 0) { hegel_health_fail ("check"); goto cleanup; }

    SCOTCH_Num velomin = -42, velomax = -42, velosum = -42;
    double     veloavg = -1.0, velodlt = -1.0;
    SCOTCH_Num degrmin = -42, degrmax = -42;
    double     degravg = -1.0, degrdlt = -1.0;
    SCOTCH_Num edlomin = -42, edlomax = -42, edlosum = -42;
    double     edloavg = -1.0, edlodlt = -1.0;

    SCOTCH_graphStat (&grafdat,
                      &velomin, &velomax, &velosum, &veloavg, &velodlt,
                      &degrmin, &degrmax, &degravg, &degrdlt,
                      &edlomin, &edlomax, &edlosum, &edloavg, &edlodlt);

    /* Vertex load (no velotab supplied, all vertices have load 1) -- velosum==n */
    HEGEL_ASSERT (velosum == g.n,
                  "velosum=%ld != n=%ld (topo=%s)",
                  (long) velosum, (long) g.n, csr_topo_name (g.topology));
    HEGEL_ASSERT (velomin <= velomax,
                  "velomin=%ld > velomax=%ld (topo=%s)",
                  (long) velomin, (long) velomax, csr_topo_name (g.topology));
    HEGEL_ASSERT (veloavg >= (double) velomin - 1e-6 &&
                  veloavg <= (double) velomax + 1e-6,
                  "veloavg=%f outside [%ld, %ld] (topo=%s)",
                  veloavg, (long) velomin, (long) velomax, csr_topo_name (g.topology));

    /* Degree stats */
    HEGEL_ASSERT (degrmin >= 0,
                  "degrmin=%ld < 0 (topo=%s)", (long) degrmin, csr_topo_name (g.topology));
    HEGEL_ASSERT (degrmin <= degrmax,
                  "degrmin=%ld > degrmax=%ld (topo=%s)",
                  (long) degrmin, (long) degrmax, csr_topo_name (g.topology));
    HEGEL_ASSERT (degravg >= (double) degrmin - 1e-6,
                  "degravg=%f < degrmin=%ld (topo=%s)",
                  degravg, (long) degrmin, csr_topo_name (g.topology));
    HEGEL_ASSERT (degravg <= (double) degrmax + 1e-6,
                  "degravg=%f > degrmax=%ld (topo=%s)",
                  degravg, (long) degrmax, csr_topo_name (g.topology));

    /* sum of degrees = total number of arcs = edgenbr (each undirected edge counts twice) */
    double expected = (double) g.edgenbr;
    double actual = degravg * (double) g.n;
    HEGEL_ASSERT (fabs (actual - expected) < 1e-3,
                  "sum-of-degrees: degravg*n=%f != edgenbr=%f (topo=%s n=%ld)",
                  actual, expected, csr_topo_name (g.topology), (long) g.n);

    /* FINDING: For unweighted graphs (edlotab==NULL), Scotch sets
    ** edlosum = edgenbr / 2 (integer division), i.e. it interprets
    ** "edge load sum" as undirected-edge count, not arc-load sum.
    ** See library_graph.c. We codify the actual behaviour. */
    HEGEL_ASSERT (edlosum == g.edgenbr / 2,
                  "edlosum=%ld != edgenbr/2=%ld (topo=%s) -- "
                  "Scotch sets edlosum=edgenbr/2 for unweighted graphs",
                  (long) edlosum, (long) (g.edgenbr / 2), csr_topo_name (g.topology));

cleanup:
    if (graf_inited) SCOTCH_graphExit (&grafdat);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_stat, 1000);
    return 0;
}
