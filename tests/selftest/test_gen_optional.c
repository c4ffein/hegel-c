/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_optional produces present or absent values.
**
** Property: optional(int(42,42)) produces either nothing (return 0)
** or the value 42 (return 1, *out == 42). No other values.
** This test should PASS.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
void
testOptional (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  present;
  int                  out;

  gn = hegel_gen_optional (hegel_gen_int (42, 42));

  out = -1;
  present = hegel_gen_draw_optional_int (tc, gn, &out);

  HEGEL_ASSERT (present == 0 || present == 1,
                "draw_optional_int returned %d, expected 0 or 1", present);

  if (present) {
    HEGEL_ASSERT (out == 42,
                  "optional(int(42,42)) present but value=%d, expected 42", out);
  }

  hegel_gen_free (gn);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing gen_optional...\n");
  hegel_run_test (testOptional);
  printf ("PASSED\n");

  return (0);
}
