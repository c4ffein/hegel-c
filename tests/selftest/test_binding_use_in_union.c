/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_USE inside HEGEL_UNION / HEGEL_UNION_UNTAGGED cases.
**
** Union case bodies are drawn through hegel__draw_struct_into_slot,
** which chains the case's local ctx to the enclosing struct's — so a
** HEGEL_USE inside a HEGEL_CASE must resolve a HEGEL_LET from the
** outer struct.  Was listed as "plumbing is probably there, but
** untested" (TODO.md, binding-system item 4).
**
** Exercises:
**   1. Tagged union — USE(n) in both cases reads the outer LET
**   2. Untagged union — same, tag recovered from the shape tree
**   3. Case-local LET shadowing — a LET inside one CASE shadows the
**      outer binding for that case only; the other case walks out
**      to the enclosing scope
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ================================================================
** Test 1: tagged union — cases read the outer LET
** ================================================================ */

typedef struct {
  int                 n;
  int                 tag;
  union {
    struct { int a; }         one;
    struct { int x; int y; }  two;
  } u;
} UBox;

HEGEL_BINDING (un);
static hegel_schema_t ubox_schema;

static
void
init_ubox_schema (void)
{
  ubox_schema = HEGEL_STRUCT (UBox,
      HEGEL_LET   (un, HEGEL_INT (0, 50)),
      HEGEL_USE   (un),
      HEGEL_UNION (
          HEGEL_CASE (HEGEL_USE (un)),
          HEGEL_CASE (HEGEL_USE (un), HEGEL_INT (0, 5))));
}

static
void
test_union_tagged_use (
hegel_testcase *    tc)
{
  UBox *              b;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, ubox_schema, (void **) &b);

  HEGEL_ASSERT (b->n >= 0 && b->n <= 50, "n=%d", b->n);
  HEGEL_ASSERT (b->tag == 0 || b->tag == 1, "tag=%d", b->tag);
  if (b->tag == 0) {
    HEGEL_ASSERT (b->u.one.a == b->n,
                  "case 0: USE(un)=%d != outer n=%d", b->u.one.a, b->n);
  } else {
    HEGEL_ASSERT (b->u.two.x == b->n,
                  "case 1: USE(un)=%d != outer n=%d", b->u.two.x, b->n);
    HEGEL_ASSERT (b->u.two.y >= 0 && b->u.two.y <= 5, "y=%d", b->u.two.y);
  }

  hegel_shape_free (sh);
}

/* ================================================================
** Test 2: untagged union — tag from the shape tree
** ================================================================ */

typedef struct {
  int                 n;
  union {
    struct { int a; }         one;
    struct { int x; int y; }  two;
  } u;
} RBox;

HEGEL_BINDING (rn);
static hegel_schema_t rbox_schema;

static
void
init_rbox_schema (void)
{
  rbox_schema = HEGEL_STRUCT (RBox,
      HEGEL_LET (rn, HEGEL_INT (0, 50)),
      HEGEL_USE (rn),
      HEGEL_UNION_UNTAGGED (
          HEGEL_CASE (HEGEL_USE (rn)),
          HEGEL_CASE (HEGEL_USE (rn), HEGEL_INT (0, 5))));
}

static
void
test_union_untagged_use (
hegel_testcase *    tc)
{
  RBox *              b;
  hegel_shape *       sh;
  int                 tag;

  sh = hegel_schema_draw (tc, rbox_schema, (void **) &b);

  /* Positional shape fields mirror schema children: 0 = LET (NULL),
  ** 1 = USE, 2 = the union. */
  tag = hegel_shape_tag (hegel_shape_field (sh, 2));
  HEGEL_ASSERT (tag == 0 || tag == 1, "tag=%d", tag);
  if (tag == 0) {
    HEGEL_ASSERT (b->u.one.a == b->n,
                  "case 0: USE(rn)=%d != outer n=%d", b->u.one.a, b->n);
  } else {
    HEGEL_ASSERT (b->u.two.x == b->n,
                  "case 1: USE(rn)=%d != outer n=%d", b->u.two.x, b->n);
  }

  hegel_shape_free (sh);
}

/* ================================================================
** Test 3: case-local LET shadows the outer binding for its case only
** ================================================================ */

typedef struct {
  int                 outer_val;
  int                 tag;
  union {
    struct { int inner_val; }             shadowed;  /* case-local LET */
    struct { int inner_val; int extra; }  walked;    /* outer LET via walk */
  } u;
} SBox;

HEGEL_BINDING (sm);
static hegel_schema_t sbox_schema;

static
void
init_sbox_schema (void)
{
  /* Outer sm in [100,200]; case 0 re-LETs sm in [1,5] — disjoint
  ** ranges make it observable which scope a USE resolved. */
  sbox_schema = HEGEL_STRUCT (SBox,
      HEGEL_LET   (sm, HEGEL_INT (100, 200)),
      HEGEL_USE   (sm),
      HEGEL_UNION (
          HEGEL_CASE (HEGEL_LET (sm, HEGEL_INT (1, 5)),
                      HEGEL_USE (sm)),
          HEGEL_CASE (HEGEL_USE (sm), HEGEL_INT (0, 5))));
}

static
void
test_union_case_local_shadow (
hegel_testcase *    tc)
{
  SBox *              b;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, sbox_schema, (void **) &b);

  HEGEL_ASSERT (b->outer_val >= 100 && b->outer_val <= 200,
                "outer_val=%d", b->outer_val);
  if (b->tag == 0) {
    HEGEL_ASSERT (b->u.shadowed.inner_val >= 1 && b->u.shadowed.inner_val <= 5,
                  "case 0 must read the case-local LET: inner_val=%d",
                  b->u.shadowed.inner_val);
  } else {
    HEGEL_ASSERT (b->u.walked.inner_val == b->outer_val,
                  "case 1 must walk to the outer LET: inner_val=%d outer=%d",
                  b->u.walked.inner_val, b->outer_val);
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

  init_ubox_schema ();
  printf ("  USE in tagged union cases...\n");
  hegel_run_test (test_union_tagged_use);
  printf ("    PASSED\n");

  init_rbox_schema ();
  printf ("  USE in untagged union cases...\n");
  hegel_run_test (test_union_untagged_use);
  printf ("    PASSED\n");

  init_sbox_schema ();
  printf ("  case-local LET shadowing...\n");
  hegel_run_test (test_union_case_local_shadow);
  printf ("    PASSED\n");

  hegel_schema_free (ubox_schema);
  hegel_schema_free (rbox_schema);
  hegel_schema_free (sbox_schema);
  return (0);
}
