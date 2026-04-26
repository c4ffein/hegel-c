/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: per-instance shadowing inside HEGEL_ARR_OF of structs.
**
** Outer struct has HEGEL_LET(n) in outer scope.  Element struct
** of an ARR_OF re-LETs the SAME binding id with a disjoint range.
** Each element gets its own draw, and the outer's n is unaffected.
**
** Property: `r->outer_n` stays in [100, 200] and equals across draws
** consistent with the outer LET.  Each `r->groups[i]->elem_n` is
** in [1, 9] — the element scope's independent draw.  Outer n and
** all element ns are mutually independent.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 elem_n;
} Element;

typedef struct {
  int                 outer_n;
  Element **          groups;
} Root;

HEGEL_BINDING (n);          /* outer's binding */

static hegel_schema_t root_schema;

static
void
test_arr_shadow (
hegel_testcase *    tc)
{
  Root *              r;
  hegel_shape *       sh;
  int                 i;

  sh = hegel_schema_draw (tc, root_schema, (void **) &r);

  /* Outer's n is in outer's range. */
  HEGEL_ASSERT (r->outer_n >= 100 && r->outer_n <= 200,
                "outer_n=%d out of [100,200]", r->outer_n);

  /* Each element's shadowed n is in the element's own range, not
  ** outer's.  Element ranges [1,9] and outer [100,200] are disjoint
  ** to catch any leak: if an element ever saw outer's value, this
  ** would fire. */
  for (i = 0; i < 3; i ++) {
    Element * e = r->groups[i];
    HEGEL_ASSERT (e != NULL, "groups[%d] NULL", i);
    HEGEL_ASSERT (e->elem_n >= 1 && e->elem_n <= 9,
                  "groups[%d].elem_n=%d out of [1,9] — shadow leaked",
                  i, e->elem_n);
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

  hegel_schema_t elem_schema = HEGEL_STRUCT (Element,
      HEGEL_LET (n, HEGEL_INT (1, 9)),   /* shadows outer's n */
      HEGEL_USE (n));                     /* elem_n in [1,9] */

  root_schema = HEGEL_STRUCT (Root,
      HEGEL_LET    (n, HEGEL_INT (100, 200)),
      HEGEL_USE    (n),                          /* outer_n in [100,200] */
      HEGEL_ARR_OF (HEGEL_CONST (3), elem_schema));

  printf ("Testing array-of-structs with shadowed LET...\n");
  hegel_run_test (test_arr_shadow);
  printf ("  PASSED\n");

  hegel_schema_free (root_schema);
  return (0);
}
