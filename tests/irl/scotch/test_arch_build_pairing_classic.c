/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Classic (deterministic, no hegel) regression test for the
** SCOTCH_archBuild terminal-pairing bug at arch_build.c:309.
** See arch_build_pairing.h for the full bug matrix and oracle design.
**
** Expected results:
**   pristine v7.0.12          RED  (base-1 cases: garbage / mispaired)
**   upstream patch e6c094fc   RED  (sibling oracle fails at base 0 too)
**   v2 fix (mapDomain(&mappdat, tgtvertnum))   GREEN
**
** Three oracles per (n, base) case:
**   1. structural sanity (root size, leaf label bijection)
**   2. sibling-leaf adjacency on a forced-median path
**   3. base invariance: base-0 and base-1 builds of the same path must
**      yield identical trees modulo the label shift
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch_build_pairing.h"

static int
check_one_base (
int                         n,
int                         base,
pairing_arch *              out)                  /* Kept for the base-invariance check */
{
  int  dist[PAIRING_MAX_N - 1];
  char errbuf[256];

  pairing_assign_forced_dists (0, n, 0, NULL, dist);
  if (pairing_build_path_arch (n, base, dist, out, errbuf, sizeof (errbuf)) != 0 ||
      pairing_check_basic     (out, n, base, errbuf, sizeof (errbuf))        != 0 ||
      pairing_check_siblings  (out, n, base, errbuf, sizeof (errbuf))        != 0) {
    printf ("FAIL: %s\n", errbuf);
    return (-1);
  }
  return (0);
}

static int
check_base_invariance (
int                         n,
const pairing_arch *        a0,                   /* Build at base 0 */
const pairing_arch *        a1)                   /* Build at base 1 */
{
  long long i, distnbr;

  if (a0->domvertnbr != a1->domvertnbr) {
    printf ("FAIL: base invariance (n=%d): domvertnbr %lld (base 0) != %lld (base 1)\n",
            n, a0->domvertnbr, a1->domvertnbr);
    return (-1);
  }
  for (i = 0; i < a0->domvertnbr; i ++) {
    if (a0->size[i] != a1->size[i] || a0->wght[i] != a1->wght[i] ||
        (a0->size[i] > 0 && a0->labl[i] + 1 != a1->labl[i])) {
      printf ("FAIL: base invariance (n=%d) at node num=%lld: "
              "base 0 (labl=%lld size=%lld wght=%lld) vs base 1 (labl=%lld size=%lld wght=%lld)\n",
              n, i + 1, a0->labl[i], a0->size[i], a0->wght[i],
              a1->labl[i], a1->size[i], a1->wght[i]);
      return (-1);
    }
  }
  distnbr = (a0->domvertnbr * (a0->domvertnbr - 1)) / 2;
  for (i = 0; i < distnbr; i ++) {
    if (a0->dist[i] != a1->dist[i]) {
      printf ("FAIL: base invariance (n=%d): distance entry %lld: %lld (base 0) != %lld (base 1)\n",
              n, i, a0->dist[i], a1->dist[i]);
      return (-1);
    }
  }
  return (0);
}

int
main (void)
{
  static const int ntab[] = { 3, 4, 6, 8, 12 };
  int              errnbr = 0;
  size_t           i;

  SCOTCH_errorProg ("test_arch_build_pairing_classic");

  for (i = 0; i < sizeof (ntab) / sizeof (ntab[0]); i ++) {
    pairing_arch a0, a1;
    int          n = ntab[i];
    int          rc0, rc1;

    rc0 = check_one_base (n, 0, &a0);
    rc1 = check_one_base (n, 1, &a1);
    if (rc0 != 0 || rc1 != 0)
      errnbr ++;
    else if (check_base_invariance (n, &a0, &a1) != 0)
      errnbr ++;
    else
      printf ("ok: n=%d (base 0 and 1, %lld tree nodes)\n", n, a0.domvertnbr);
    pairing_arch_free (&a0);
    pairing_arch_free (&a1);
  }

  if (errnbr != 0) {
    printf ("test_arch_build_pairing_classic: %d case(s) FAILED\n", errnbr);
    return (1);
  }
  printf ("test_arch_build_pairing_classic: all cases passed\n");
  return (0);
}
