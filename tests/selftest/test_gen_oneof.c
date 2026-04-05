/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_one_of picks from sub-generators correctly.
**
** Property: one_of(int(0,0), int(1,1)) only produces 0 or 1.
** This test should PASS.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
void
testOneOf (
hegel_testcase *            tc)
{
  hegel_gen *          gens[2];
  hegel_gen *          gn;
  int                  result;

  gens[0] = hegel_gen_int (0, 0);
  gens[1] = hegel_gen_int (1, 1);
  gn = hegel_gen_one_of (gens, 2);

  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (result == 0 || result == 1,
                "one_of(int(0,0), int(1,1)) produced %d", result);

  hegel_gen_free (gn);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing gen_one_of...\n");
  hegel_run_test (testOneOf);
  printf ("PASSED\n");

  return (0);
}
