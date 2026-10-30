/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_USE_AT outside any ARR_OF iteration aborts loudly.
**
** The struct LET_ARRs `sizes` then USE_ATs it directly as a struct
** field — but no ARR_OF is iterating in this scope, so there's no
** current_index to use.  This is a schema authoring error that must
** surface as a hard abort, not a silent zero / out-of-bounds read.
**
** Expected: EXIT NON-ZERO via abort.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 lonely;
} Bad;

HEGEL_BINDING (sizes);
static hegel_schema_t bad_schema;

static
void
test_no_iter (
hegel_testcase *    tc)
{
  Bad *               b;
  hegel_shape *       sh = hegel_schema_draw (tc, bad_schema, (void **) &b);
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
      HEGEL_LET_ARR (sizes, HEGEL_INT (1, 3), HEGEL_INT (0, 9)),
      HEGEL_USE_AT  (sizes));      /* no ARR_OF iterating here */

  fprintf (stderr, "About to draw — expect USE_AT-without-iter abort...\n");
  hegel_run_test (test_no_iter);
  fprintf (stderr, "BUG: USE_AT without iteration did not abort\n");
  hegel_schema_free (bad_schema);
  return (1);
}
