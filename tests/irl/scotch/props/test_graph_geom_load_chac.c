/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_graphGeomLoadChac never corrupts memory, whatever the file.
**
** The input is a Chaco file with in-range neighbour numbers but a header edge
** count that need not match the adjacency lists.  The loader may accept or
** reject it; it must not write outside its arrays, and the graph must stay
** exitable afterwards.
**
** Witness for pin graphGeomLoadChac: the loader sizes edgetax from the header
** (2 * edges arcs) and its overflow guard, `edgenum > edgenbr + 1`, is off by
** one and runs after the write's operands are read, so one arc too many is
** written past the array (graph_io_chac.c:230, ASan heap-buffer-overflow).
** Minimal file: "1 1\n1 1 1\n" (1 vertex, 1 edge declared, 3 arcs listed).
**
** Not checked: a file with FEWER arcs than declared is accepted, and the
** result then fails SCOTCH_graphCheck (the loader's own check is compiled
** only with SCOTCH_DEBUG_GRAPH2).  That's a weaker contract question, kept
** out so the pin's fixed state can be green.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"
#include "scotch.h"

static void test_graph_geom_load_chac (hegel_testcase * tc)
{
  int  nvert = hegel_draw_int (tc, 1, 6);
  int  decl_edges = hegel_draw_int (tc, 0, 6);
  char buf[1024];
  int  pos = snprintf (buf, sizeof buf, "%d %d\n", nvert, decl_edges);

  for (int v = 1; v <= nvert; v++) {
    int deg = hegel_draw_int (tc, 0, 6);
    for (int e = 0; e < deg; e++)
      pos += snprintf (buf + pos, sizeof buf - pos, "%s%d", e ? " " : "",
                       hegel_draw_int (tc, 1, nvert));
    pos += snprintf (buf + pos, sizeof buf - pos, "\n");
  }
  hegel_note (tc, buf);

  SCOTCH_Graph graph;
  SCOTCH_Geom  geom;
  FILE *       f = fmemopen (buf, strlen (buf), "r");
  if (f == NULL) { hegel_health_fail ("fmemopen"); return; }
  SCOTCH_graphInit (&graph);
  SCOTCH_geomInit (&geom);
  (void) SCOTCH_graphGeomLoadChac (&graph, &geom, f, NULL, NULL);
  SCOTCH_geomExit (&geom);
  SCOTCH_graphExit (&graph);
  fclose (f);
}

int main (void)
{
  hegel_run_test_n (test_graph_geom_load_chac, 1000);
  return 0;
}
