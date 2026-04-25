/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: scope-chain lookup + shadowing semantics.
**
** Three sub-tests:
**
**   A. Inner shadows outer.  Outer LET(n)=40..50, inner LET(n)=1..5
**      (same compile-time id).  Inner USE(n) must see inner's value
**      (first-match-wins), not outer's.  Outer's post-inner USE(n)
**      must still see outer's value (inner's scope already popped).
**
**   B. Deep chain walk.  Grandparent LET(g), middle struct defines
**      nothing, child USE(g) — lookup walks up past middle to
**      grandparent.  Depth doesn't matter.
**
**   C. Sibling isolation.  Outer LET(n).  Two HEGEL_INLINE siblings
**      each re-LET n independently.  Each sibling's USE(n) sees its
**      OWN drawn value; siblings never see each other's.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ========================================================
** Test A: inner-shadows-outer
** ======================================================== */

typedef struct {
  int                 inner_n;
} ShadowInner;

typedef struct {
  int                 outer_n_before;
  ShadowInner         inner;
  int                 outer_n_after;
} ShadowOuter;

HEGEL_BINDING (sn);
static hegel_schema_t  shadow_schema;

static
void
test_shadow (
hegel_testcase *    tc)
{
  ShadowOuter *       o;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, shadow_schema, (void **) &o);

  /* Outer's two USE(sn) entries both read outer's bound value —
  ** inner's shadow lasts only inside the HEGEL_INLINE block. */
  HEGEL_ASSERT (o->outer_n_before >= 40 && o->outer_n_before <= 50,
                "outer_n_before=%d out of [40,50]", o->outer_n_before);
  HEGEL_ASSERT (o->outer_n_after == o->outer_n_before,
                "outer_n_after=%d != outer_n_before=%d",
                o->outer_n_after, o->outer_n_before);
  HEGEL_ASSERT (o->inner.inner_n >= 1 && o->inner.inner_n <= 5,
                "inner_n=%d out of [1,5] — shadow broken",
                o->inner.inner_n);

  hegel_shape_free (sh);
}

/* ========================================================
** Test B: deep chain walk (grandparent LET, child USE)
** ======================================================== */

typedef struct {
  int                 child_g;
} DeepChild;

typedef struct {
  int                 middle_pad;
  DeepChild           child;
} DeepMiddle;

typedef struct {
  DeepMiddle          middle;
} DeepGrandparent;

HEGEL_BINDING (gp);
static hegel_schema_t deep_schema;

static
void
test_deep (
hegel_testcase *    tc)
{
  DeepGrandparent *   gp_val;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, deep_schema, (void **) &gp_val);

  HEGEL_ASSERT (gp_val->middle.middle_pad >= 0
                && gp_val->middle.middle_pad <= 100,
                "middle_pad=%d", gp_val->middle.middle_pad);
  /* child_g must equal what grandparent LET'd.  We can't read the
  ** grandparent's value directly (no slot for it) so the best we
  ** can do is range-check. */
  HEGEL_ASSERT (gp_val->middle.child.child_g >= 7
                && gp_val->middle.child.child_g <= 9,
                "child_g=%d out of [7,9] — chain walk broken",
                gp_val->middle.child.child_g);

  hegel_shape_free (sh);
}

/* ========================================================
** Test C: sibling isolation
** ======================================================== */

typedef struct {
  int                 a_n;
} SibA;

typedef struct {
  int                 b_n;
} SibB;

typedef struct {
  int                 outer_shared;
  SibA                a;
  SibB                b;
} SibRoot;

HEGEL_BINDING (sib_n);
static hegel_schema_t sibling_schema;

static
void
test_sibling_isolation (
hegel_testcase *    tc)
{
  SibRoot *           r;
  hegel_shape *       sh;
  int                 i;

  /* Run many cases by hand via _nofork_n would be ideal, but
  ** one draw is enough — we just check the invariant holds per
  ** draw.  Test file runs under the default retry count. */
  sh = hegel_schema_draw (tc, sibling_schema, (void **) &r);

  /* outer_shared is outer's LET(sib_n), range 100..200.  Sibling a
  ** re-LETs sib_n in 1..10; sibling b re-LETs in 50..60.  Their
  ** bound values are independent, and neither equals outer's. */
  HEGEL_ASSERT (r->outer_shared >= 100 && r->outer_shared <= 200,
                "outer_shared=%d out of [100,200]", r->outer_shared);
  HEGEL_ASSERT (r->a.a_n >= 1 && r->a.a_n <= 10,
                "a.a_n=%d out of [1,10] — sibling A leaked?", r->a.a_n);
  HEGEL_ASSERT (r->b.b_n >= 50 && r->b.b_n <= 60,
                "b.b_n=%d out of [50,60] — sibling B leaked?", r->b.b_n);

  /* Suppress unused-var warning. */
  i = 0; (void) i;

  hegel_shape_free (sh);
}

/* ========================================================
** Runner
** ======================================================== */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  /* Test A: inner shadows outer. */
  shadow_schema = HEGEL_STRUCT (ShadowOuter,
      HEGEL_LET    (sn, HEGEL_INT (40, 50)),    /* outer's LET */
      HEGEL_USE    (sn),                         /* outer_n_before */
      HEGEL_INLINE (ShadowInner,
          HEGEL_LET (sn, HEGEL_INT (1, 5)),     /* inner shadow */
          HEGEL_USE (sn)),                       /* inner sees shadow */
      HEGEL_USE    (sn));                        /* outer_n_after — back to outer's */

  /* Test B: deep chain walk.  Grandparent defines gp, middle has
  ** no binding for it, child reads it anyway via the chain. */
  deep_schema = HEGEL_STRUCT (DeepGrandparent,
      HEGEL_LET    (gp, HEGEL_INT (7, 9)),
      HEGEL_INLINE (DeepMiddle,
          HEGEL_INT (0, 100),                    /* middle_pad */
          HEGEL_INLINE (DeepChild,
              HEGEL_USE (gp))));                 /* reaches grandparent */

  /* Test C: sibling isolation via two separate HEGEL_INLINE siblings
  ** that each re-LET the same binding id. */
  sibling_schema = HEGEL_STRUCT (SibRoot,
      HEGEL_LET    (sib_n, HEGEL_INT (100, 200)),
      HEGEL_USE    (sib_n),                      /* outer_shared */
      HEGEL_INLINE (SibA,
          HEGEL_LET (sib_n, HEGEL_INT (1, 10)),
          HEGEL_USE (sib_n)),                    /* a.a_n */
      HEGEL_INLINE (SibB,
          HEGEL_LET (sib_n, HEGEL_INT (50, 60)),
          HEGEL_USE (sib_n)));                   /* b.b_n */

  printf ("Testing inner shadows outer...\n");
  hegel_run_test (test_shadow);
  printf ("  PASSED\n");

  printf ("Testing deep chain walk (grandparent → child)...\n");
  hegel_run_test (test_deep);
  printf ("  PASSED\n");

  printf ("Testing sibling isolation...\n");
  hegel_run_test (test_sibling_isolation);
  printf ("  PASSED\n");

  hegel_schema_free (shadow_schema);
  hegel_schema_free (deep_schema);
  hegel_schema_free (sibling_schema);
  return (0);
}
