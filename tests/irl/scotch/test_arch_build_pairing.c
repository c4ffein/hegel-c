/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Hegel property test for the SCOTCH_archBuild terminal-pairing bug at
** arch_build.c:309.  See arch_build_pairing.h for the bug matrix and
** oracle design.
**
** Property: for a random path length, base value, and edge-distance
** jitter (distances still forcing contiguous median cuts), the deco 0
** architecture built by SCOTCH_archBuild0 must
**   (a) hold each path vertex in exactly one singleton leaf, and
**   (b) pair sibling singleton leaves with path-ADJACENT labels.
**
** Expected results:
**   pristine v7.0.12          RED  (shrinks towards small n, base=1)
**   upstream patch e6c094fc   RED  (fails at base 0 too)
**   v2 fix (mapDomain(&mappdat, tgtvertnum))   GREEN
**
** Layer 1 (function under test) is pairing_build_path_arch — the
** SCOTCH_archBuild0 wrapper in arch_build_pairing.h.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch_build_pairing.h"
#include "hegel_c.h"

/* ---- Layer 2: hegel property ---- */

static void
prop_arch_build_pairing (
hegel_testcase *            tc)
{
  pairing_arch a;
  int          jitter[PAIRING_MAX_N - 1];
  int          dist[PAIRING_MAX_N - 1];
  char         errbuf[256];
  char         notebuf[320];
  int          n, base, i, rc;

  n    = hegel_draw_int (tc, 4, PAIRING_MAX_N);   /* n >= 4 so both halves of the top split re-split */
  base = hegel_draw_int (tc, 0, 1);
  for (i = 0; i < n - 1; i ++)                    /* Draw everything before touching Scotch */
    jitter[i] = hegel_draw_int (tc, 0, 9);

  pairing_assign_forced_dists (0, n, 0, jitter, dist);

  rc = pairing_build_path_arch (n, base, dist, &a, errbuf, sizeof (errbuf));
  HEGEL_ASSERT (rc == 0, "arch build/parse failed: %s", errbuf);

  rc = pairing_check_basic (&a, n, base, errbuf, sizeof (errbuf));
  if (rc != 0) {
    snprintf (notebuf, sizeof (notebuf), "basic oracle: %s", errbuf);
    hegel_note (tc, notebuf);
  }
  HEGEL_ASSERT (rc == 0, "leaf-label bijection broken (n=%d base=%d)", n, base);

  rc = pairing_check_siblings (&a, n, base, errbuf, sizeof (errbuf));
  if (rc != 0) {
    snprintf (notebuf, sizeof (notebuf), "sibling oracle: %s", errbuf);
    hegel_note (tc, notebuf);
  }
  HEGEL_ASSERT (rc == 0, "sibling leaves not path-adjacent (n=%d base=%d)", n, base);

  pairing_arch_free (&a);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  SCOTCH_errorProg ("test_arch_build_pairing");
  return (hegel_run_test_result_n (prop_arch_build_pairing, 100));
}
