/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_LET for the same binding twice in one scope aborts loudly.
**
** Two HEGEL_LET (dup_n, ...) entries in the same HEGEL_STRUCT — the
** classic copy-paste mistake.  The re-bind check in
** hegel__draw_ctx_bind_full must abort at draw time rather than let
** the second LET silently win.  Since HEGEL_LET stringifies the
** binding, the abort message names it ("HEGEL_LET (dup_n) appears
** twice"), not just the opaque __COUNTER__ id.
**
** Expected: EXIT NON-ZERO via abort (caught by fork isolation).
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 a;
  int                 b;
} Dup;

HEGEL_BINDING (dup_n);
static hegel_schema_t dup_schema;

static
void
test_relet (
hegel_testcase *    tc)
{
  Dup *               d;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, dup_schema, (void **) &d);
  /* Should never reach here — the second LET (dup_n) aborts. */
  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  dup_schema = HEGEL_STRUCT (Dup,
      HEGEL_LET (dup_n, HEGEL_INT (0, 9)),
      HEGEL_USE (dup_n),
      HEGEL_LET (dup_n, HEGEL_INT (0, 9)),   /* WRONG — dup_n again */
      HEGEL_USE (dup_n));

  fprintf (stderr, "About to draw — expect re-LET abort naming dup_n...\n");
  hegel_run_test (test_relet);
  /* Should not reach here. */
  fprintf (stderr, "BUG: duplicate HEGEL_LET did not abort\n");
  hegel_schema_free (dup_schema);
  return (1);
}
