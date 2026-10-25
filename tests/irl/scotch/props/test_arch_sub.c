/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Property: SCOTCH_archSub keeps exactly the requested terminals.
**
** For an original architecture (complete graph, 2D mesh or hypercube) and a
** non-empty set of distinct terminals, the sub-architecture must:
**   - build (rc 0), with archSize == number of kept terminals;
**   - give each sub terminal t a terminal domain of size 1 numbered t;
**   - keep distances: dist_sub(t1, t2) == dist_org(keep[t1], keep[t2])
**     (sub terminal t stands for original terminal keep[t], arch_sub.c).
**
** Witness for pin archSubSingle: keeping ONE terminal other than 0 makes
** archSubArchBuild() write termtab[ANUMMAX] (arch_sub.c:345).
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "scotch.h"

#define MAX_TERMS 16

static void test_arch_sub (hegel_testcase * tc)
{
  SCOTCH_Arch org, sub;
  SCOTCH_Num  keep[MAX_TERMS];
  int         kind = hegel_draw_int (tc, 0, 2);
  int         rc;

  SCOTCH_archInit (&org);
  switch (kind) {
  case 0:  rc = SCOTCH_archCmplt (&org, hegel_draw_int (tc, 1, MAX_TERMS)); break;
  case 1: {
    int x = hegel_draw_int (tc, 1, 4), y = hegel_draw_int (tc, 1, 4);
    rc = SCOTCH_archMesh2 (&org, x, y);
    break;
  }
  default: rc = SCOTCH_archHcub (&org, hegel_draw_int (tc, 0, 4)); break;
  }
  if (rc != 0) { hegel_health_fail ("building the original architecture"); return; }

  /* Distinct terminals, in drawn order: one keep/skip bit per terminal, then
  ** a shuffle so the order of vnumtab is exercised too. */
  int orgsize = (int) SCOTCH_archSize (&org), nk = 0;
  for (int t = 0; t < orgsize; t++)
    if (hegel_draw_bool (tc)) keep[nk++] = t;
  if (nk == 0) { SCOTCH_archExit (&org); hegel_assume (tc, 0); return; }
  for (int i = nk - 1; i > 0; i--) {
    int j = hegel_draw_int (tc, 0, i);
    SCOTCH_Num tmp = keep[i]; keep[i] = keep[j]; keep[j] = tmp;
  }

  char msg[160];
  int  len = snprintf (msg, sizeof msg, "arch kind=%d size=%d keep={", kind, orgsize);
  for (int i = 0; i < nk && len < (int) sizeof msg - 8; i++)
    len += snprintf (msg + len, sizeof msg - len, "%s%ld", i ? "," : "", (long) keep[i]);
  snprintf (msg + len, sizeof msg - len, "}");
  hegel_note (tc, msg);

  SCOTCH_archInit (&sub);
  rc = SCOTCH_archSub (&sub, &org, nk, keep);
  HEGEL_ASSERT (rc == 0, "archSub rc=%d", rc);
  HEGEL_ASSERT (SCOTCH_archSize (&sub) == nk,
                "archSize(sub)=%ld, kept %d terminals", (long) SCOTCH_archSize (&sub), nk);

  for (int t1 = 0; t1 < nk; t1++) {
    SCOTCH_ArchDom d1, o1;
    HEGEL_ASSERT (SCOTCH_archDomTerm (&sub, &d1, t1) == 0, "archDomTerm(sub, %d) failed", t1);
    HEGEL_ASSERT (SCOTCH_archDomSize (&sub, &d1) == 1, "sub terminal %d: domain size %ld",
                  t1, (long) SCOTCH_archDomSize (&sub, &d1));
    HEGEL_ASSERT (SCOTCH_archDomNum (&sub, &d1) == t1, "sub terminal %d: domain number %ld",
                  t1, (long) SCOTCH_archDomNum (&sub, &d1));
    SCOTCH_archDomTerm (&org, &o1, keep[t1]);
    for (int t2 = 0; t2 < nk; t2++) {
      SCOTCH_ArchDom d2, o2;
      SCOTCH_archDomTerm (&sub, &d2, t2);
      SCOTCH_archDomTerm (&org, &o2, keep[t2]);
      SCOTCH_Num ds = SCOTCH_archDomDist (&sub, &d1, &d2);
      SCOTCH_Num dorg = SCOTCH_archDomDist (&org, &o1, &o2);
      HEGEL_ASSERT (ds == dorg, "dist_sub(%d,%d)=%ld but dist_org(%ld,%ld)=%ld",
                    t1, t2, (long) ds, (long) keep[t1], (long) keep[t2], (long) dorg);
    }
  }

  SCOTCH_archExit (&sub);
  SCOTCH_archExit (&org);
}

int main (void)
{
  hegel_run_test_n (test_arch_sub, 500);
  return 0;
}
