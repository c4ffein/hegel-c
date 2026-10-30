/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** test_arch_sub.c with the case drawn from a schema with view callbacks
** instead of hand-written draws — same property, same draws in the same
** order (kind, size parameters, one keep bit per terminal, one swap index
** per shuffle step), so the two versions can be compared on the archSubSingle
** pin (keeping ONE terminal other than 0 writes termtab[ANUMMAX]).
**
** What each dependency becomes:
**   - the size parameters depend on the kind      -> HEGEL_DEPENDS on `kind`;
**   - one keep bit per terminal of the original   -> HEGEL_DEPENDS returning
**     an array whose length is computed from kind and size;
**   - swap j_i in [0, i]                          -> array of HEGEL_DEPENDS
**     elements reading hegel_view_index;
**   - counts                                      -> HEGEL_LEN_OF;
**   - the terminal list itself, kept bits in
**     shuffled order, and the original's size     -> HEGEL_DERIVED.
** The shuffle runs forward (i = 1 .. nk-1) where the original runs backward;
** both are Fisher-Yates over the same index ranges.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"
#include "scotch.h"

#define MAX_TERMS 16

/* ---- The case ---- */

typedef struct {
  int          kind;              /* 0 complete graph, 1 2D mesh, 2 hypercube */
  int          a;                 /* complete: size; mesh: x; hypercube: dimension */
  int          b;                 /* mesh: y; else 0 */
  int *        bits;              /* keep bit per terminal of the original */
  int          nbits;
  int *        swaps;             /* swaps[i - 1] in [0, i] */
  int          nswaps;
  int          orgsize;           /* derived */
  int          nk;                /* derived: terminals kept */
  SCOTCH_Num   keep[MAX_TERMS];   /* derived: kept terminals, shuffled */
} ArchSubCase;

static int
arch_size (int kind, int a, int b)
{
  switch (kind) {
  case 0:  return (a);
  case 1:  return (a * b);
  default: return (1 << a);
  }
}

static int
bits_set (const int * bits, int n)
{
  int k = 0;

  for (int i = 0; i < n; i++)
    k += bits[i];
  return (k);
}

/* ---- Callbacks ---- */

static hegel_schema_t
a_of (const hegel_view * v, void * ctx)
{
  (void) ctx;
  switch (HEGEL_VIEW_FIELD (v, ArchSubCase, kind)) {
  case 0:  return (hegel_schema_int_range (1, MAX_TERMS));
  case 1:  return (hegel_schema_int_range (1, 4));
  default: return (hegel_schema_int_range (0, 4));
  }
}

static hegel_schema_t
b_of (const hegel_view * v, void * ctx)
{
  (void) ctx;
  if (HEGEL_VIEW_FIELD (v, ArchSubCase, kind) == 1)
    return (hegel_schema_int_range (1, 4));
  return (HEGEL_CONST (0));
}

static hegel_schema_t
bits_of (const hegel_view * v, void * ctx)
{
  int n = arch_size (HEGEL_VIEW_FIELD (v, ArchSubCase, kind),
                     HEGEL_VIEW_FIELD (v, ArchSubCase, a),
                     HEGEL_VIEW_FIELD (v, ArchSubCase, b));

  (void) ctx;
  return (hegel_schema_arr_of (HEGEL_CONST (n), hegel_schema_int_range (0, 1)));
}

static hegel_schema_t
swap_at (const hegel_view * v, void * ctx)
{
  (void) ctx;
  return (hegel_schema_int_range (0, hegel_view_index (v) + 1));
}

static hegel_schema_t
swaps_of (const hegel_view * v, void * ctx)
{
  int nk = bits_set (HEGEL_VIEW_FIELD (v, ArchSubCase, bits),
                     HEGEL_VIEW_FIELD (v, ArchSubCase, nbits));

  (void) ctx;
  return (hegel_schema_arr_of (HEGEL_CONST (nk > 1 ? nk - 1 : 0),
                               HEGEL_DEPENDS (int, swap_at, NULL)));
}

static void
orgsize_of (const hegel_view * v, void * out, void * ctx)
{
  const ArchSubCase * c = HEGEL_VIEW_AS (v, ArchSubCase);

  (void) ctx;
  *(int *) out = arch_size (c->kind, c->a, c->b);
}

static void
nk_of (const hegel_view * v, void * out, void * ctx)
{
  const ArchSubCase * c = HEGEL_VIEW_AS (v, ArchSubCase);

  (void) ctx;
  *(int *) out = bits_set (c->bits, c->nbits);
}

/* Kept terminals in drawn order, then the shuffle. */
static void
keep_of (const hegel_view * v, void * out, void * ctx)
{
  const ArchSubCase * c = HEGEL_VIEW_AS (v, ArchSubCase);
  SCOTCH_Num *        keep = out;
  int                 nk = 0;

  (void) ctx;
  for (int t = 0; t < c->nbits; t++)
    if (c->bits[t]) keep[nk++] = t;
  for (int i = 1; i < nk; i++) {
    int        j = c->swaps[i - 1];
    SCOTCH_Num tmp = keep[i]; keep[i] = keep[j]; keep[j] = tmp;
  }
}

static hegel_schema_t case_schema;

static void
init_schema (void)
{
  case_schema = HEGEL_STRUCT (ArchSubCase,
      HEGEL_INT     (0, 2),                                         /* kind */
      HEGEL_DEPENDS (int, a_of, NULL),                              /* a */
      HEGEL_DEPENDS (int, b_of, NULL),                              /* b */
      HEGEL_DEPENDS (int *, bits_of, NULL),                         /* bits */
      HEGEL_LEN_OF  (ArchSubCase, bits),                            /* nbits */
      HEGEL_DEPENDS (int *, swaps_of, NULL),                        /* swaps */
      HEGEL_LEN_OF  (ArchSubCase, swaps),                           /* nswaps */
      HEGEL_DERIVED (int, orgsize_of, NULL),                        /* orgsize */
      HEGEL_DERIVED (int, nk_of, NULL),                             /* nk */
      HEGEL_DERIVED (SCOTCH_Num[MAX_TERMS], keep_of, NULL));        /* keep */
}

/* ---- The property (unchanged from test_arch_sub.c) ---- */

static void test_arch_sub_view (hegel_testcase * tc)
{
  SCOTCH_Arch   org, sub;
  ArchSubCase * c;
  hegel_shape * sh;
  int           rc;

  sh = hegel_schema_draw (tc, case_schema, (void **) &c);
  if (c->nk == 0) { hegel_shape_free (sh); hegel_assume (tc, 0); return; }

  SCOTCH_archInit (&org);
  switch (c->kind) {
  case 0:  rc = SCOTCH_archCmplt (&org, c->a); break;
  case 1:  rc = SCOTCH_archMesh2 (&org, c->a, c->b); break;
  default: rc = SCOTCH_archHcub (&org, c->a); break;
  }
  if (rc != 0) { hegel_health_fail ("building the original architecture"); return; }
  HEGEL_ASSERT (SCOTCH_archSize (&org) == c->orgsize,
                "archSize(org)=%ld, expected %d", (long) SCOTCH_archSize (&org), c->orgsize);

  char msg[160];
  int  len = snprintf (msg, sizeof msg, "arch kind=%d size=%d keep={", c->kind, c->orgsize);
  for (int i = 0; i < c->nk && len < (int) sizeof msg - 8; i++)
    len += snprintf (msg + len, sizeof msg - len, "%s%ld", i ? "," : "", (long) c->keep[i]);
  snprintf (msg + len, sizeof msg - len, "}");
  hegel_note (tc, msg);

  SCOTCH_archInit (&sub);
  rc = SCOTCH_archSub (&sub, &org, c->nk, c->keep);
  HEGEL_ASSERT (rc == 0, "archSub rc=%d", rc);
  HEGEL_ASSERT (SCOTCH_archSize (&sub) == c->nk,
                "archSize(sub)=%ld, kept %d terminals", (long) SCOTCH_archSize (&sub), c->nk);

  for (int t1 = 0; t1 < c->nk; t1++) {
    SCOTCH_ArchDom d1, o1;
    HEGEL_ASSERT (SCOTCH_archDomTerm (&sub, &d1, t1) == 0, "archDomTerm(sub, %d) failed", t1);
    HEGEL_ASSERT (SCOTCH_archDomSize (&sub, &d1) == 1, "sub terminal %d: domain size %ld",
                  t1, (long) SCOTCH_archDomSize (&sub, &d1));
    HEGEL_ASSERT (SCOTCH_archDomNum (&sub, &d1) == t1, "sub terminal %d: domain number %ld",
                  t1, (long) SCOTCH_archDomNum (&sub, &d1));
    SCOTCH_archDomTerm (&org, &o1, c->keep[t1]);
    for (int t2 = 0; t2 < c->nk; t2++) {
      SCOTCH_ArchDom d2, o2;
      SCOTCH_archDomTerm (&sub, &d2, t2);
      SCOTCH_archDomTerm (&org, &o2, c->keep[t2]);
      SCOTCH_Num ds = SCOTCH_archDomDist (&sub, &d1, &d2);
      SCOTCH_Num dorg = SCOTCH_archDomDist (&org, &o1, &o2);
      HEGEL_ASSERT (ds == dorg, "dist_sub(%d,%d)=%ld but dist_org(%ld,%ld)=%ld",
                    t1, t2, (long) ds, (long) c->keep[t1], (long) c->keep[t2], (long) dorg);
    }
  }

  SCOTCH_archExit (&sub);
  SCOTCH_archExit (&org);
  hegel_shape_free (sh);
}

int main (void)
{
  init_schema ();
  hegel_run_test_n (test_arch_sub_view, 500);
  hegel_schema_free (case_schema);
  return 0;
}
