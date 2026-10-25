/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: the "k" ordering method (order by k-way partition) with its
** default parameters gives a valid ordering, like any method the parser
** accepts.
**
** Strategy "k" or "k{siz=N}": partition into vnohnbr / siz parts, then number
** each part as one column block (hgraph_order_kp.c).  The ordering must pass
** SCOTCH_graphOrderCheck.
**
** Witness for pin hgraphOrderKpDefault (expected RED on current Scotch): the
** method's default partitioning strategy is the empty strategy (stratdummy,
** hgraph_order_st.c:123), so kgraphMapSt maps nothing, mapTerm returns -1
** for every vertex and `ordetab[parttax[vertnum]] ++` updates ordetab[-1]
** (hgraph_order_kp.c:141, ASan heap-buffer-overflow).  Needs a graph with at
** least 2 * siz vertices (siz defaults to 1), else the method falls back to
** simple ordering.  No fix proposed: the right default strategy is upstream's
** call.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"

static void test_graph_order_kp (hegel_testcase * tc)
{
  CSRGraph        g = {0};
  SCOTCH_Graph    grafdat;
  SCOTCH_Strat    stradat;
  SCOTCH_Ordering ordedat;
  SCOTCH_Num *    permtab = NULL, * peritab = NULL, * rangtab = NULL, * treetab = NULL;
  int             graf_inited = 0, strat_inited = 0, ord_inited = 0;
  char            strat[32] = "k";

  if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
  if (hegel_draw_bool (tc))
    snprintf (strat, sizeof strat, "k{siz=%d}", hegel_draw_int (tc, 1, 4));

  if (SCOTCH_graphInit (&grafdat) != 0 ||
      SCOTCH_graphBuild (&grafdat, g.baseval, g.n, g.verttab, NULL, NULL, NULL,
                         g.edgenbr, g.edgetab, NULL) != 0) {
    hegel_health_fail ("graphBuild"); goto cleanup;
  }
  graf_inited = 1;
  SCOTCH_stratInit (&stradat);
  strat_inited = 1;
  if (SCOTCH_stratGraphOrder (&stradat, strat) != 0) { hegel_health_fail ("strategy"); goto cleanup; }

  permtab = malloc ((size_t) g.n * sizeof *permtab);
  peritab = malloc ((size_t) g.n * sizeof *peritab);
  rangtab = malloc ((size_t) (g.n + 1) * sizeof *rangtab);
  treetab = malloc ((size_t) g.n * sizeof *treetab);
  if (!permtab || !peritab || !rangtab || !treetab) { hegel_health_fail ("alloc"); goto cleanup; }

  SCOTCH_Num cblknbr = 0;
  if (SCOTCH_graphOrderInit (&grafdat, &ordedat, permtab, peritab, &cblknbr, rangtab, treetab) != 0) {
    hegel_health_fail ("graphOrderInit"); goto cleanup;
  }
  ord_inited = 1;

  int rc = SCOTCH_graphOrderCompute (&grafdat, &ordedat, &stradat);
  HEGEL_ASSERT (rc == 0, "graphOrderCompute strat='%s' rc=%d topo=%s n=%ld",
                strat, rc, csr_topo_name (g.topology), (long) g.n);
  int chk = SCOTCH_graphOrderCheck (&grafdat, &ordedat);
  HEGEL_ASSERT (chk == 0, "graphOrderCheck strat='%s' rc=%d topo=%s n=%ld",
                strat, chk, csr_topo_name (g.topology), (long) g.n);

cleanup:
  if (ord_inited)   SCOTCH_graphOrderExit (&grafdat, &ordedat);
  if (strat_inited) SCOTCH_stratExit (&stradat);
  if (graf_inited)  SCOTCH_graphExit (&grafdat);
  free (permtab); free (peritab); free (rangtab); free (treetab);
  csr_free (&g);
}

int main (void)
{
  hegel_run_test_n (test_graph_order_kp, 300);
  return 0;
}
