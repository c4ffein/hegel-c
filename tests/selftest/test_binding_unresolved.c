/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Negative test: HEGEL_USE referencing a binding that was never
** HEGEL_LET must abort loudly, not silently write garbage.
**
** Unresolved-REF is a schema authoring error (user's fault, can't be
** shrunk around), so we prefer a hard hegel__abort() over a silent
** assume(0) / discard.  Under fork mode the abort fires SIGABRT in
** the child; the parent catches it and exits non-zero.
**
** Expected: EXIT non-zero (classified as CRASH — like test_crash_abort).
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 x;
} OnlyUse;

HEGEL_BINDING (never_bound);

static hegel_schema_t schema;

static
void
test_unresolved_use (
hegel_testcase *    tc)
{
  OnlyUse *           p;
  hegel_shape *       sh;

  /* The draw should never return — the HEGEL_USE lookup fails and
  ** hegel__abort fires.  If we ever reach the printf below, the
  ** abort was skipped (a silent-write regression) and the test
  ** should fail the PASS/CRASH classification. */
  sh = hegel_schema_draw (tc, schema, (void **) &p);
  printf ("UNREACHABLE: draw returned x=%d\n", p->x);
  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  schema = HEGEL_STRUCT (OnlyUse, HEGEL_USE (never_bound));

  printf ("Testing HEGEL_USE of unbound name (must abort)...\n");
  hegel_run_test (test_unresolved_use);

  /* Belt-and-suspenders: if hegel_run_test returns here despite the
  ** child's abort (the Rust panic path can unwind back to main), we
  ** still exit non-zero to honor the CRASH classification.  Matches
  ** the pattern in test_crash_abort. */
  printf ("BUG: hegel_run_test returned after unresolved HEGEL_USE\n");
  return (1);
}
