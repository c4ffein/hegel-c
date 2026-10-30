/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Negative test: exceeding HEGEL__MAX_BINDINGS_PER_SCOPE (16)
** must abort loudly, not silently truncate.
**
** 17 distinct HEGEL_BINDING declarations, all HEGEL_LET'd in one
** struct.  The 17th call to hegel__draw_ctx_bind trips the
** capacity check.
**
** If the limit is raised, this test still documents the contract:
** whatever the cap is, going over it is a hard abort.  Adjust
** the N below to limit + 1 if you bump HEGEL__MAX_BINDINGS_PER_SCOPE.
**
** Expected: EXIT non-zero (CRASH category — abort fires in child).
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* 17 int fields — one per binding.  Struct layout: contiguous ints. */
typedef struct {
  int b0, b1, b2, b3, b4, b5, b6, b7, b8,
      b9, b10, b11, b12, b13, b14, b15, b16;
} TooMany;

HEGEL_BINDING (t0);  HEGEL_BINDING (t1);  HEGEL_BINDING (t2);
HEGEL_BINDING (t3);  HEGEL_BINDING (t4);  HEGEL_BINDING (t5);
HEGEL_BINDING (t6);  HEGEL_BINDING (t7);  HEGEL_BINDING (t8);
HEGEL_BINDING (t9);  HEGEL_BINDING (t10); HEGEL_BINDING (t11);
HEGEL_BINDING (t12); HEGEL_BINDING (t13); HEGEL_BINDING (t14);
HEGEL_BINDING (t15); HEGEL_BINDING (t16);

static hegel_schema_t schema;

static
void
test_overflow (
hegel_testcase *    tc)
{
  TooMany *           p;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, schema, (void **) &p);
  printf ("UNREACHABLE: draw returned\n");
  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  /* 17 LETs + 17 USEs = 34 entries.  Struct has 17 int fields.
  ** LETs are non-positional, so layout matches the 17 USE entries. */
  schema = HEGEL_STRUCT (TooMany,
      HEGEL_LET (t0,  HEGEL_INT (0, 1)),
      HEGEL_LET (t1,  HEGEL_INT (0, 1)),
      HEGEL_LET (t2,  HEGEL_INT (0, 1)),
      HEGEL_LET (t3,  HEGEL_INT (0, 1)),
      HEGEL_LET (t4,  HEGEL_INT (0, 1)),
      HEGEL_LET (t5,  HEGEL_INT (0, 1)),
      HEGEL_LET (t6,  HEGEL_INT (0, 1)),
      HEGEL_LET (t7,  HEGEL_INT (0, 1)),
      HEGEL_LET (t8,  HEGEL_INT (0, 1)),
      HEGEL_LET (t9,  HEGEL_INT (0, 1)),
      HEGEL_LET (t10, HEGEL_INT (0, 1)),
      HEGEL_LET (t11, HEGEL_INT (0, 1)),
      HEGEL_LET (t12, HEGEL_INT (0, 1)),
      HEGEL_LET (t13, HEGEL_INT (0, 1)),
      HEGEL_LET (t14, HEGEL_INT (0, 1)),
      HEGEL_LET (t15, HEGEL_INT (0, 1)),
      HEGEL_LET (t16, HEGEL_INT (0, 1)),
      HEGEL_USE (t0),  HEGEL_USE (t1),  HEGEL_USE (t2),
      HEGEL_USE (t3),  HEGEL_USE (t4),  HEGEL_USE (t5),
      HEGEL_USE (t6),  HEGEL_USE (t7),  HEGEL_USE (t8),
      HEGEL_USE (t9),  HEGEL_USE (t10), HEGEL_USE (t11),
      HEGEL_USE (t12), HEGEL_USE (t13), HEGEL_USE (t14),
      HEGEL_USE (t15), HEGEL_USE (t16));

  printf ("Testing 17-binding overflow (must abort)...\n");
  hegel_run_test (test_overflow);

  /* Belt-and-suspenders: if hegel_run_test returns here despite
  ** the child's abort, exit non-zero anyway. */
  printf ("BUG: hegel_run_test returned\n");
  return (1);
}
