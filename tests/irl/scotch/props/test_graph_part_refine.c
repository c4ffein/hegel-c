/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphPart never crashes on a strategy the parser accepts.
** It may succeed or return an error; a valid result has every part in range.
**
** The strategy is a single k-way method.  "f" (FM refinement) and "d"
** (diffusion) refine an existing mapping; on their own there is none.
**
** Witness for pin kgraphRefineNoMapping (expected RED on current Scotch):
** "f" alone dereferences the unset mapping in kgraphCost (kgraph_cost.c:104,
** called from kgraphMapFm), "d" alone in kgraphMapDf (kgraph_map_df.c:246).
** SIGSEGV even without ASan.  Arguably a misuse (the manual uses them after
** an initial method), so the fix we'd suggest is an error, not a mapping.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"
#include "graph_gen.h"

static const char * STRATS[] = { "r{sep=g}", "f", "d" };
#define NSTRATS ((int) (sizeof STRATS / sizeof STRATS[0]))

static void test_graph_part_refine (hegel_testcase * tc)
{
  CSRGraph     g = {0};
  SCOTCH_Graph grafdat;
  SCOTCH_Strat stradat;
  SCOTCH_Num * parttab = NULL;
  int          graf_inited = 0, strat_inited = 0;

  if (csr_gen_any (tc, &g, 10) != 0) { hegel_health_fail ("CSR"); goto cleanup; }
  int          partnbr = hegel_draw_int (tc, 1, 4);
  const char * strat = STRATS[hegel_draw_int (tc, 0, NSTRATS - 1)];

  if (SCOTCH_graphInit (&grafdat) != 0 ||
      SCOTCH_graphBuild (&grafdat, g.baseval, g.n, g.verttab, NULL, NULL, NULL,
                         g.edgenbr, g.edgetab, NULL) != 0) {
    hegel_health_fail ("graphBuild"); goto cleanup;
  }
  graf_inited = 1;
  SCOTCH_stratInit (&stradat);
  strat_inited = 1;
  if (SCOTCH_stratGraphMap (&stradat, strat) != 0) { hegel_health_fail ("strategy"); goto cleanup; }

  parttab = malloc ((size_t) g.n * sizeof *parttab);
  if (parttab == NULL) { hegel_health_fail ("alloc"); goto cleanup; }
  for (int i = 0; i < g.n; i++) parttab[i] = -42;

  if (SCOTCH_graphPart (&grafdat, partnbr, &stradat, parttab) == 0)
    for (int i = 0; i < g.n; i++)
      HEGEL_ASSERT (parttab[i] >= 0 && parttab[i] < partnbr,
                    "strat='%s' partnbr=%d: vertex %d got part %ld (topo=%s n=%ld)",
                    strat, partnbr, i, (long) parttab[i], csr_topo_name (g.topology), (long) g.n);

cleanup:
  free (parttab);
  if (strat_inited) SCOTCH_stratExit (&stradat);
  if (graf_inited)  SCOTCH_graphExit (&grafdat);
  csr_free (&g);
}

int main (void)
{
  hegel_run_test_n (test_graph_part_refine, 300);
  return 0;
}
