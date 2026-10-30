/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphLoad(SCOTCH_graphSave(G)) preserves size and edges.
**
** A canonical roundtrip property: serialize a graph to a stream, parse it
** back, and assert the result has the same vertex count and edge count.
** This exercises both the I/O serialization codepath and the loader on
** generated input topologies.
**
** Uses tmpfile() to round-trip through a real FILE *. graphLoad accepts
** baseval=-1 to "use baseval from file" or any non-negative override.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"
#include "scotch_helpers.h"

static void test_graph_save_load (hegel_testcase * tc)
{
    CSRGraph g = {0};
    SCOTCH_Graph orig;
    SCOTCH_Graph loaded;
    FILE * f = NULL;
    int orig_inited = 0, loaded_inited = 0;

    if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
    if (g.n < 1) { hegel_assume (tc, 0); goto cleanup; }

    if (scotch_build_from_csr (&g, &orig) != 0) {
        hegel_health_fail ("orig build"); goto cleanup;
    }
    orig_inited = 1;
    if (SCOTCH_graphCheck (&orig) != 0) { hegel_health_fail ("orig check"); goto cleanup; }

    f = tmpfile ();
    if (f == NULL) { hegel_health_fail ("tmpfile"); goto cleanup; }

    int sv = SCOTCH_graphSave (&orig, f);
    HEGEL_ASSERT (sv == 0,
                  "graphSave rc=%d topo=%s n=%ld baseval=%d",
                  sv, csr_topo_name (g.topology), (long) g.n, g.baseval);

    rewind (f);

    if (SCOTCH_graphInit (&loaded) != 0) { hegel_health_fail ("loaded init"); goto cleanup; }
    loaded_inited = 1;

    /* baseval -1 = use whatever the file says; flagval 0 = default */
    int ld = SCOTCH_graphLoad (&loaded, f, -1, 0);
    HEGEL_ASSERT (ld == 0,
                  "graphLoad rc=%d topo=%s n=%ld baseval=%d",
                  ld, csr_topo_name (g.topology), (long) g.n, g.baseval);

    /* Loaded graph must pass graphCheck. */
    int chk = SCOTCH_graphCheck (&loaded);
    HEGEL_ASSERT (chk == 0,
                  "loaded graphCheck rc=%d topo=%s",
                  chk, csr_topo_name (g.topology));

    /* Vertex and edge counts must match. */
    SCOTCH_Num gn = 0, gm = 0;
    SCOTCH_graphSize (&loaded, &gn, &gm);
    HEGEL_ASSERT (gn == g.n,
                  "roundtrip n: got %ld, expected %ld (topo=%s)",
                  (long) gn, (long) g.n, csr_topo_name (g.topology));
    HEGEL_ASSERT (gm == g.edgenbr,
                  "roundtrip m: got %ld, expected %ld (topo=%s)",
                  (long) gm, (long) g.edgenbr, csr_topo_name (g.topology));

cleanup:
    if (f != NULL) fclose (f);
    if (loaded_inited) SCOTCH_graphExit (&loaded);
    if (orig_inited)   SCOTCH_graphExit (&orig);
    csr_free (&g);
}

int main (void)
{
    hegel_run_test_n (test_graph_save_load, 1000);
    return 0;
}
