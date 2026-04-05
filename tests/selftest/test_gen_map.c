/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_map_int produces correctly transformed values.
**
** Property: map(x -> x*2) on int(0,50) always produces even numbers.
** This test should PASS.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
int
doubleIt (int val, void * ctx)
{
  (void) ctx;
  return (val * 2);
}

static
void
testMapDouble (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  result;

  gn = hegel_gen_map_int (hegel_gen_int (0, 50), doubleIt, NULL);
  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (result % 2 == 0,
                "map(x->x*2) produced odd number: %d", result);
  HEGEL_ASSERT (result >= 0 && result <= 100,
                "map(x->x*2) out of range: %d", result);

  hegel_gen_free (gn);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing gen_map_int...\n");
  hegel_run_test (testMapDouble);
  printf ("PASSED\n");

  return (0);
}
