/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_USE_PATH — explicit-path resolution.
**
** Three sub-tests, each exercises a path shape USE / USE_AT can't:
**
**   A. Shadow-skip on a scalar.  Outer LET(sn)=40..50, inner LET(sn)=1..5.
**      Plain HEGEL_USE(sn) gets inner's shadow; HEGEL_USE_PATH(HEGEL_PARENT, sn)
**      reaches outer's value.  Verifies skip semantics.
**
**   B. Path-resolved scalar with no skip.  HEGEL_USE_PATH(sn) walks the
**      scope chain like HEGEL_USE — sanity check that no-PARENT works.
**
**   C. Path-resolved array element.  HEGEL_LET_ARR in outer; inner uses
**      HEGEL_USE_PATH(HEGEL_PARENT, sizes, HEGEL_INDEX_HERE) to read
**      sizes[outer_iter] — same as HEGEL_USE_AT but spelled out as a
**      path.  Sanity check the array branch.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ========================================================
** A. Shadow skip
** ======================================================== */

typedef struct {
  int                 sees_outer;     /* USE_PATH(PARENT, sn) */
  int                 sees_shadow;    /* USE(sn) — inner's shadow */
} ShadowChild;

typedef struct {
  int                 outer_n;        /* USE(sn) */
  ShadowChild         child;          /* inline, with own LET(sn) */
} ShadowOuter;

HEGEL_BINDING (sn);
static hegel_schema_t shadow_schema;

static
void
test_shadow_skip (
hegel_testcase *    tc)
{
  ShadowOuter *       o;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, shadow_schema, (void **) &o);

  HEGEL_ASSERT (o->outer_n >= 40 && o->outer_n <= 50,
                "outer_n=%d out of [40,50]", o->outer_n);
  HEGEL_ASSERT (o->child.sees_outer == o->outer_n,
                "sees_outer=%d != outer_n=%d — PARENT skip didn't reach outer",
                o->child.sees_outer, o->outer_n);
  HEGEL_ASSERT (o->child.sees_shadow >= 1 && o->child.sees_shadow <= 5,
                "sees_shadow=%d out of [1,5] — shadow not seen",
                o->child.sees_shadow);

  hegel_shape_free (sh);
}

/* ========================================================
** B. No-skip USE_PATH on scalar
** ======================================================== */

typedef struct {
  int                 a;              /* USE_PATH(no_skip_n) */
} NoSkip;

HEGEL_BINDING (no_skip_n);
static hegel_schema_t noskip_schema;

static
void
test_no_skip (
hegel_testcase *    tc)
{
  NoSkip *            n;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, noskip_schema, (void **) &n);
  HEGEL_ASSERT (n->a >= 1 && n->a <= 9, "a=%d out of [1,9]", n->a);
  hegel_shape_free (sh);
}

/* ========================================================
** C. USE_PATH array indexing (parity with USE_AT)
** ======================================================== */

typedef struct {
  int                 my_size;        /* USE_PATH(PARENT, sizes, INDEX_HERE) */
} ArrChild;

typedef struct {
  ArrChild **         groups;
  int                 n;
} ArrOuter;

HEGEL_BINDING (an);
HEGEL_BINDING (sizes);

static hegel_schema_t arr_schema;

static
void
test_arr_path (
hegel_testcase *    tc)
{
  ArrOuter *          o;
  hegel_shape *       sh;
  int                 i;

  sh = hegel_schema_draw (tc, arr_schema, (void **) &o);

  HEGEL_ASSERT (o->n >= 1 && o->n <= 4, "n=%d out of [1,4]", o->n);
  for (i = 0; i < o->n; i ++) {
    ArrChild * c = o->groups[i];
    HEGEL_ASSERT (c != NULL, "groups[%d] NULL", i);
    HEGEL_ASSERT (c->my_size >= 0 && c->my_size <= 5,
                  "groups[%d].my_size=%d out of [0,5]", i, c->my_size);
  }

  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  /* A */
  hegel_schema_t child_a = HEGEL_STRUCT (ShadowChild,
      HEGEL_LET (sn, HEGEL_INT (1, 5)),                  /* shadow */
      HEGEL_USE_PATH (HEGEL_PARENT, sn),                 /* sees_outer */
      HEGEL_USE      (sn));                              /* sees_shadow */
  shadow_schema = HEGEL_STRUCT (ShadowOuter,
      HEGEL_LET    (sn, HEGEL_INT (40, 50)),
      HEGEL_USE    (sn),                                  /* outer_n */
      HEGEL_INLINE_REF (ShadowChild, child_a));

  /* B */
  noskip_schema = HEGEL_STRUCT (NoSkip,
      HEGEL_LET      (no_skip_n, HEGEL_INT (1, 9)),
      HEGEL_USE_PATH (no_skip_n));                        /* same as USE */

  /* C */
  hegel_schema_t child_c = HEGEL_STRUCT (ArrChild,
      HEGEL_USE_PATH (HEGEL_PARENT, sizes, HEGEL_INDEX_HERE));
  arr_schema = HEGEL_STRUCT (ArrOuter,
      HEGEL_LET     (an, HEGEL_INT (1, 4)),
      HEGEL_LET_ARR (sizes, HEGEL_USE (an), HEGEL_INT (0, 5)),
      HEGEL_ARR_OF  (HEGEL_USE (an), child_c),
      HEGEL_USE     (an));

  printf ("Testing HEGEL_USE_PATH shadow skip...\n");
  hegel_run_test (test_shadow_skip);
  printf ("  PASSED\n");

  printf ("Testing HEGEL_USE_PATH no-skip scalar...\n");
  hegel_run_test (test_no_skip);
  printf ("  PASSED\n");

  printf ("Testing HEGEL_USE_PATH array indexing...\n");
  hegel_run_test (test_arr_path);
  printf ("  PASSED\n");

  hegel_schema_free (shadow_schema);
  hegel_schema_free (noskip_schema);
  hegel_schema_free (arr_schema);
  return (0);
}
