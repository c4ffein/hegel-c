/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_filter_int only produces values satisfying the predicate.
**
** Property: filter(x -> x % 3 == 0) on int(0,99) always produces
** multiples of 3.
** This test should PASS.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
int
divisibleBy3 (int val, void * ctx)
{
  (void) ctx;
  return (val % 3 == 0);
}

static
void
testFilterDiv3 (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  result;

  gn = hegel_gen_filter_int (hegel_gen_int (0, 99), divisibleBy3, NULL);
  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (result % 3 == 0,
                "filter(x%%3==0) produced %d", result);
  HEGEL_ASSERT (result >= 0 && result <= 99,
                "filter result out of source range: %d", result);

  hegel_gen_free (gn);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing gen_filter_int...\n");
  hegel_run_test (testFilterDiv3);
  printf ("PASSED\n");

  return (0);
}
