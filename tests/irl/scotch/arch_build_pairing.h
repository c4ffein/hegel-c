/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Shared oracle for the SCOTCH_archBuild (deco 0) terminal-pairing bug
** at src/libscotch/arch_build.c:309.
**
** Bug matrix this oracle discriminates (see the Makefile target
** test-archbuild-pairing for how to run it against each variant):
**
**   - pristine v7.0.12:  `mapDomain (&mappdat, tgtvertnum - baseval)`
**     double-subtracts the base (parttax is already rebased at
**     arch_build.c:186).  Base-0 graphs are fine; base-1 graphs read
**     parttax[-1] (heap garbage) for the first vertex and the WRONG
**     vertex's domain for all others.            -> RED (base-1 cases)
**
**   - upstream patch e6c094fc ("use proper domain for distance
**     computations"): `&mappdat.domntab[termdomnum]` assumes domain
**     index i is the domain of terminal vertex i.  Domain indices are
**     assigned in split-creation order (LIFO job stack), not vertex
**     order, so terminal labels get attached to the wrong leaves of
**     the decomposition tree at ANY base.        -> RED (base-0 too)
**
**   - v2 fix: `mapDomain (&mappdat, tgtvertnum)` (parttax is based;
**     every other mapDomain call site passes based indices)
**                                                -> GREEN
**
** Oracle idea: build the architecture from a weighted path graph
** whose edge distances FORCE the recursive bisection:
**   - all distances lie in [100, 199], i.e. within a factor of 2, so
**     (after graphIelo inversion) cutting any single edge is cheaper
**     than cutting two: every bipartition keeps parts contiguous;
**   - within every contiguous interval the median edge carries the
**     strictly largest distance (=> strictly cheapest cut), so each
**     split severs the median edge.
** Under a CORRECT build, two singleton leaves that are siblings in
** the decomposition tree (node numbers 2k and 2k+1) are the two ends
** of the last edge cut, hence ADJACENT path vertices: their labels
** differ by exactly 1.  A mispaired build breaks this.
**
** The tree is read back through the public API: SCOTCH_archSave for a
** "deco" architecture emits, per tree node num (1-based, children of
** k are 2k / 2k+1):  labl size wght.
*/

#ifndef ARCH_BUILD_PAIRING_H
#define ARCH_BUILD_PAIRING_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <scotch.h>

typedef struct {
  long long  termdomnbr;                          /* Number of terminals (leaves)           */
  long long  domvertnbr;                          /* Number of tree nodes (max terminal no) */
  long long *labl;                                /* Per node num-1: terminal label         */
  long long *size;                                /* Per node num-1: leaves in subtree      */
  long long *wght;                                /* Per node num-1: accumulated weight     */
  long long *dist;                                /* Triangular distance array              */
} pairing_arch;

#define PAIRING_MAX_N        16                   /* Max path length the oracle supports  */
#define PAIRING_MAX_NODES  4096                   /* Absurdity cap when parsing the save  */

/* Assign forced distances to path edges: the median edge of every
** interval [lo,hi) gets the strictly largest distance of the interval.
** Levels are 30 apart; jitter (0..9 per edge) keeps a >= 20 margin.
** Depth <= 3 for n <= 16 keeps everything in [100, 199]: within a
** factor of 2, so single-edge (contiguous) cuts always win. */
static void
pairing_assign_forced_dists (
int                         lo,
int                         hi,
int                         depth,
const int *                 jitter,               /* n-1 entries in [0, 9], may be NULL */
int *                       dist)                 /* n-1 entries, filled                */
{
  int mid;

  if (hi - lo < 2)
    return;
  mid = lo + (hi - lo) / 2;                       /* Cut edge mid-1 -- mid    */
  dist[mid - 1] = 199 - 30 * depth - (jitter != NULL ? jitter[mid - 1] : 0);
  pairing_assign_forced_dists (lo,  mid, depth + 1, jitter, dist);
  pairing_assign_forced_dists (mid, hi,  depth + 1, jitter, dist);
}

/* Layer 1 — the function under test: build a deco 0 architecture from
** a weighted path graph via SCOTCH_archBuild0 and parse it back.
** Returns 0 on success, -1 on failure (message in errbuf). */
static int
pairing_build_path_arch (
int                         n,                    /* Path length, 2 <= n <= PAIRING_MAX_N */
int                         base,                 /* Graph base value, 0 or 1             */
const int *                 dist,                 /* n-1 forced edge distances            */
pairing_arch *              out,
char *                      errbuf,
size_t                      errsz)
{
  SCOTCH_Graph  grafdat;
  SCOTCH_Strat  stradat;
  SCOTCH_Arch   archdat;
  SCOTCH_Num    verttab[PAIRING_MAX_N + 1];
  SCOTCH_Num    edgetab[2 * (PAIRING_MAX_N - 1)];
  SCOTCH_Num    edlotab[2 * (PAIRING_MAX_N - 1)];
  char *        savbuf = NULL;
  size_t        savsiz = 0;
  FILE *        stream;
  char          clasname[32];
  long long     decotype;
  long long     i, distnbr;
  int           nedge, rc;

  memset (out, 0, sizeof (*out));

  if (n < 2 || n > PAIRING_MAX_N) {
    snprintf (errbuf, errsz, "bad n=%d", n);
    return (-1);
  }

  nedge = 0;                                      /* Compact CSR for the path */
  for (i = 0; i < n; i ++) {
    verttab[i] = nedge + base;
    if (i > 0) {                                  /* Edge to previous vertex */
      edgetab[nedge] = (SCOTCH_Num) (i - 1 + base);
      edlotab[nedge] = dist[i - 1];
      nedge ++;
    }
    if (i < n - 1) {                              /* Edge to next vertex */
      edgetab[nedge] = (SCOTCH_Num) (i + 1 + base);
      edlotab[nedge] = dist[i];
      nedge ++;
    }
  }
  verttab[n] = nedge + base;

  SCOTCH_graphInit (&grafdat);
  if (SCOTCH_graphBuild (&grafdat, base, n, verttab, NULL, NULL, NULL,
                         nedge, edgetab, edlotab) != 0) {
    snprintf (errbuf, errsz, "SCOTCH_graphBuild failed (n=%d base=%d)", n, base);
    return (-1);
  }

  SCOTCH_stratInit (&stradat);                    /* Default bipartitioning strategy */
  SCOTCH_archInit  (&archdat);
  SCOTCH_randomReset ();                          /* Scotch's RNG is process-global: reset so each
                                                  ** build is deterministic regardless of how many
                                                  ** builds ran before (base-invariance oracle,
                                                  ** hegel replay stability)                       */
  rc = SCOTCH_archBuild0 (&archdat, &grafdat, 0, NULL, &stradat);
  if (rc != 0) {
    snprintf (errbuf, errsz, "SCOTCH_archBuild0 failed (n=%d base=%d)", n, base);
    goto fail_arch;
  }

  stream = open_memstream (&savbuf, &savsiz);     /* Read the tree back through the public API */
  if (stream == NULL || SCOTCH_archSave (&archdat, stream) != 0) {
    if (stream != NULL)
      fclose (stream);
    snprintf (errbuf, errsz, "SCOTCH_archSave failed (n=%d base=%d)", n, base);
    goto fail_arch;
  }
  fclose (stream);

  stream = fmemopen (savbuf, savsiz, "r");
  if (stream == NULL ||
      fscanf (stream, "%31s %lld %lld %lld",
              clasname, &decotype, &out->termdomnbr, &out->domvertnbr) != 4 ||
      strcmp (clasname, "deco") != 0 || decotype != 1) {
    snprintf (errbuf, errsz, "unparsable arch save header (n=%d base=%d): %.60s",
              n, base, savbuf != NULL ? savbuf : "(null)");
    goto fail_stream;
  }
  if (out->termdomnbr != n ||
      out->domvertnbr < out->termdomnbr || out->domvertnbr > PAIRING_MAX_NODES) {
    snprintf (errbuf, errsz,
              "absurd deco header (n=%d base=%d): termdomnbr=%lld domvertnbr=%lld",
              n, base, out->termdomnbr, out->domvertnbr);
    goto fail_stream;
  }

  out->labl = (long long *) calloc ((size_t) out->domvertnbr, sizeof (long long));
  out->size = (long long *) calloc ((size_t) out->domvertnbr, sizeof (long long));
  out->wght = (long long *) calloc ((size_t) out->domvertnbr, sizeof (long long));
  distnbr   = (out->domvertnbr * (out->domvertnbr - 1)) / 2;
  out->dist = (long long *) calloc ((size_t) (distnbr + 1), sizeof (long long));
  if (out->labl == NULL || out->size == NULL || out->wght == NULL || out->dist == NULL) {
    snprintf (errbuf, errsz, "out of memory parsing arch");
    goto fail_stream;
  }
  for (i = 0; i < out->domvertnbr; i ++) {
    if (fscanf (stream, "%lld %lld %lld", &out->labl[i], &out->size[i], &out->wght[i]) != 3) {
      snprintf (errbuf, errsz, "truncated domain table (n=%d base=%d, node %lld)", n, base, i + 1);
      goto fail_stream;
    }
  }
  for (i = 0; i < distnbr; i ++) {
    if (fscanf (stream, "%lld", &out->dist[i]) != 1) {
      snprintf (errbuf, errsz, "truncated distance table (n=%d base=%d, entry %lld)", n, base, i);
      goto fail_stream;
    }
  }

  fclose (stream);
  free (savbuf);
  SCOTCH_archExit  (&archdat);
  SCOTCH_stratExit (&stradat);
  SCOTCH_graphExit (&grafdat);
  return (0);

fail_stream:
  if (stream != NULL)
    fclose (stream);
fail_arch:
  free (savbuf);
  SCOTCH_archExit  (&archdat);
  SCOTCH_stratExit (&stradat);
  SCOTCH_graphExit (&grafdat);
  return (-1);
}

static void
pairing_arch_free (
pairing_arch *              a)
{
  free (a->labl); free (a->size); free (a->wght); free (a->dist);
  memset (a, 0, sizeof (*a));
}

/* Oracle 1 — structural sanity: the root holds all n terminals, and the
** n singleton leaves carry each path vertex label exactly once. */
static int
pairing_check_basic (
const pairing_arch *        a,
int                         n,
int                         base,
char *                      errbuf,
size_t                      errsz)
{
  int       seen[PAIRING_MAX_N];
  long long i, leafnbr;

  if (a->size[0] != n) {
    snprintf (errbuf, errsz, "root size %lld != n=%d (base=%d)", a->size[0], n, base);
    return (-1);
  }
  memset (seen, 0, sizeof (seen));
  for (i = 0, leafnbr = 0; i < a->domvertnbr; i ++) {
    if (a->size[i] != 1)
      continue;
    leafnbr ++;
    if (a->labl[i] < base || a->labl[i] >= base + n) {
      snprintf (errbuf, errsz, "leaf num=%lld has out-of-range labl=%lld (n=%d base=%d)",
                i + 1, a->labl[i], n, base);
      return (-1);
    }
    if (seen[a->labl[i] - base] ++ != 0) {
      snprintf (errbuf, errsz, "duplicate leaf labl=%lld (n=%d base=%d)", a->labl[i], n, base);
      return (-1);
    }
  }
  if (leafnbr != n) {
    snprintf (errbuf, errsz, "%lld singleton leaves, expected n=%d (base=%d)", leafnbr, n, base);
    return (-1);
  }
  return (0);
}

/* Oracle 2 — pairing: with distances forcing contiguous median cuts,
** sibling singleton leaves (nums 2k / 2k+1) are the two ends of the
** last edge cut => path-adjacent => labels differ by exactly 1. */
static int
pairing_check_siblings (
const pairing_arch *        a,
int                         n,
int                         base,
char *                      errbuf,
size_t                      errsz)
{
  long long k, pairnbr;
  long long d;

  for (k = 1, pairnbr = 0; 2 * k + 1 <= a->domvertnbr; k ++) {
    if (a->size[2 * k - 1] != 1 || a->size[2 * k] != 1) /* Nodes 2k and 2k+1 */
      continue;
    pairnbr ++;
    d = a->labl[2 * k - 1] - a->labl[2 * k];
    if (d != 1 && d != -1) {
      snprintf (errbuf, errsz,
                "sibling leaves %lld/%lld carry non-adjacent labls %lld and %lld (n=%d base=%d)"
                " — terminal labels are attached to the wrong decomposition-tree leaves",
                2 * k, 2 * k + 1, a->labl[2 * k - 1], a->labl[2 * k], n, base);
      return (-1);
    }
  }
  if (pairnbr == 0) {                             /* n >= 2 always splits some 2-vertex domain */
    snprintf (errbuf, errsz, "no singleton sibling pair found (n=%d base=%d)", n, base);
    return (-1);
  }
  return (0);
}

#endif /* ARCH_BUILD_PAIRING_H */
