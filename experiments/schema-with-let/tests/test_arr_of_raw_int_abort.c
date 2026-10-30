/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Negative test: HEGEL_ARR_OF length must be HEGEL_USE(name) or
** HEGEL_CONST(N).  Passing raw HEGEL_INT(lo, hi) directly as the
** length must abort at schema-build time, not silently work.
**
** The rule: every array length is either explicitly named (so the
** value is referenceable elsewhere via HEGEL_USE) or trivially a
** literal (HEGEL_CONST).  Anonymous random lengths are forbidden —
** users wrap in HEGEL_LET first.  Forces the common count+array
** coherence pattern to be the natural way to write things, with no
** silent gap if the length later needs to be referenced.
**
** Schema construction here aborts in main() before any test runs;
** TESTS_CRASH classification just checks for non-zero exit.
**
** Expected: EXIT non-zero (classified as CRASH).
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct { int * items; } Foo;

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing raw HEGEL_INT in HEGEL_ARR_OF length (must abort)...\n");
  /* This call should not return — hegel_schema_arr_of catches the
  ** raw HEGEL_INT length and aborts with an actionable message. */
  (void) HEGEL_STRUCT (Foo,
      HEGEL_ARR_OF (HEGEL_INT (3, 3), HEGEL_INT (0, 100)));

  printf ("BUG: schema construction returned despite raw HEGEL_INT length\n");
  return (1);
}
