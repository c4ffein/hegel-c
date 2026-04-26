/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_LET width mismatched with HEGEL_USE width aborts loudly.
**
** LET binds an i64; HEGEL_USE (the int variant) tries to read it.
** The binding kind check at USE time must fire — silent truncation
** would mask user-error bugs that the schema layer is supposed to
** catch up front.
**
** Expected: EXIT NON-ZERO via abort.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 oops;
} Bad;

HEGEL_BINDING (bi);
static hegel_schema_t bad_schema;

static
void
test_mismatch (
hegel_testcase *    tc)
{
  Bad *               b;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, bad_schema, (void **) &b);
  /* Should never reach here — USE(bi) aborts because bi is i64. */
  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  bad_schema = HEGEL_STRUCT (Bad,
      HEGEL_LET (bi, HEGEL_I64 (1, 9)),
      HEGEL_USE (bi));            /* WRONG — should be HEGEL_USE_I64 */

  fprintf (stderr, "About to draw — expect kind-mismatch abort...\n");
  hegel_run_test (test_mismatch);
  /* Should not reach here. */
  fprintf (stderr, "BUG: kind mismatch did not abort\n");
  hegel_schema_free (bad_schema);
  return (1);
}
