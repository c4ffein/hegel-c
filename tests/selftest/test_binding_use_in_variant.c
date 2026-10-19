/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_USE inside HEGEL_VARIANT case structs.
**
** Variant cases are full struct schemas drawn via hegel__draw_struct
** with the enclosing struct's ctx as parent — so a HEGEL_USE inside a
** case struct must resolve a HEGEL_LET from the struct that holds the
** variant.  Was listed as "plumbing is probably there, but untested"
** (TODO.md, binding-system item 3).
**
** Exercises:
**   1. Both variant cases read the outer LET through the scope chain
**   2. A case struct with its own LET shadows the outer binding
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ================================================================
** Test 1: variant cases read the outer LET
** ================================================================ */

typedef struct { int val; }            CaseA;
typedef struct { int val; int extra; } CaseB;

/* The variant cluster (int tag + void * ptr) is pointer-aligned as a
** unit, so it cannot sit right after a lone int the way a bare C
** `int tag; void * value;` pair could (C would put tag at offset 4;
** the cluster starts at 8).  Cluster first, scalars after — the
** sizeof assert in HEGEL_STRUCT catches the wrong order at build
** time. */
typedef struct {
  int                 tag;
  void *              value;
  int                 n;
} VBox;

HEGEL_BINDING (vn);
static hegel_schema_t vbox_schema;

static
void
init_vbox_schema (void)
{
  hegel_schema_t case_a = HEGEL_STRUCT (CaseA,
      HEGEL_USE (vn));
  hegel_schema_t case_b = HEGEL_STRUCT (CaseB,
      HEGEL_USE (vn),
      HEGEL_INT (0, 5));
  vbox_schema = HEGEL_STRUCT (VBox,
      HEGEL_LET     (vn, HEGEL_INT (0, 50)),
      HEGEL_VARIANT (case_a, case_b),
      HEGEL_USE     (vn));
}

static
void
test_variant_use (
hegel_testcase *    tc)
{
  VBox *              b;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, vbox_schema, (void **) &b);

  HEGEL_ASSERT (b->n >= 0 && b->n <= 50, "n=%d", b->n);
  HEGEL_ASSERT (b->tag == 0 || b->tag == 1, "tag=%d", b->tag);
  HEGEL_ASSERT (b->value != NULL, "value is NULL");
  if (b->tag == 0) {
    CaseA * c = (CaseA *) b->value;
    HEGEL_ASSERT (c->val == b->n,
                  "case A: USE(vn)=%d != outer n=%d", c->val, b->n);
  } else {
    CaseB * c = (CaseB *) b->value;
    HEGEL_ASSERT (c->val == b->n,
                  "case B: USE(vn)=%d != outer n=%d", c->val, b->n);
    HEGEL_ASSERT (c->extra >= 0 && c->extra <= 5, "extra=%d", c->extra);
  }

  hegel_shape_free (sh);
}

/* ================================================================
** Test 2: case struct with its own LET shadows the outer binding
** ================================================================ */

typedef struct { int inner_val; } WCaseA;   /* case-local LET */
typedef struct { int inner_val; } WCaseB;   /* outer LET via walk */

typedef struct {
  int                 tag;
  void *              value;
  int                 outer_val;
} WBox;

HEGEL_BINDING (wm);
static hegel_schema_t wbox_schema;

static
void
init_wbox_schema (void)
{
  /* Outer wm in [100,200]; case A re-LETs wm in [1,5] — disjoint
  ** ranges make it observable which scope a USE resolved. */
  hegel_schema_t case_a = HEGEL_STRUCT (WCaseA,
      HEGEL_LET (wm, HEGEL_INT (1, 5)),
      HEGEL_USE (wm));
  hegel_schema_t case_b = HEGEL_STRUCT (WCaseB,
      HEGEL_USE (wm));
  wbox_schema = HEGEL_STRUCT (WBox,
      HEGEL_LET     (wm, HEGEL_INT (100, 200)),
      HEGEL_VARIANT (case_a, case_b),
      HEGEL_USE     (wm));
}

static
void
test_variant_case_local_shadow (
hegel_testcase *    tc)
{
  WBox *              b;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, wbox_schema, (void **) &b);

  HEGEL_ASSERT (b->outer_val >= 100 && b->outer_val <= 200,
                "outer_val=%d", b->outer_val);
  HEGEL_ASSERT (b->value != NULL, "value is NULL");
  if (b->tag == 0) {
    WCaseA * c = (WCaseA *) b->value;
    HEGEL_ASSERT (c->inner_val >= 1 && c->inner_val <= 5,
                  "case A must read the case-local LET: inner_val=%d",
                  c->inner_val);
  } else {
    WCaseB * c = (WCaseB *) b->value;
    HEGEL_ASSERT (c->inner_val == b->outer_val,
                  "case B must walk to the outer LET: inner_val=%d outer=%d",
                  c->inner_val, b->outer_val);
  }

  hegel_shape_free (sh);
}

/* ================================================================
** Runner
** ================================================================ */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  init_vbox_schema ();
  printf ("  USE in variant cases...\n");
  hegel_run_test (test_variant_use);
  printf ("    PASSED\n");

  init_wbox_schema ();
  printf ("  variant case-local LET shadowing...\n");
  hegel_run_test (test_variant_case_local_shadow);
  printf ("    PASSED\n");

  hegel_schema_free (vbox_schema);
  hegel_schema_free (wbox_schema);
  return (0);
}
