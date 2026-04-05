/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_one_of picks from sub-generators correctly.
**
** Layer 1: is_binary() checks if x is 0 or 1.
** Layer 2: one_of(int(0,0), int(1,1)) — result must satisfy is_binary().
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Returns non-zero if x is a binary digit (0 or 1). */

static
int
is_binary (
int                         x)
{
  return (x == 0 || x == 1);
}

/* ---- Layer 2: hegel test ---- */

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

  HEGEL_ASSERT (is_binary (result),
                "one_of(int(0,0), int(1,1)) produced %d", result);

  hegel_gen_free (gn);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

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
