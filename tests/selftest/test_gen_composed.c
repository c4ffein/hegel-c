/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: composing multiple combinators works correctly.
**
** Chain: int(0,50) -> filter(even) -> map(x*3) -> result
** Property: result is always divisible by 6 (even * 3) and in [0, 150].
** This test should PASS.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
int
isEven (int val, void * ctx)
{
  (void) ctx;
  return (val % 2 == 0);
}

static
int
tripleIt (int val, void * ctx)
{
  (void) ctx;
  return (val * 3);
}

static
void
testComposed (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  result;

  /* filter first, then map — both take ownership of source */
  gn = hegel_gen_filter_int (hegel_gen_int (0, 50), isEven, NULL);
  gn = hegel_gen_map_int (gn, tripleIt, NULL);

  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (result % 6 == 0,
                "filter(even) -> map(*3) produced %d, not divisible by 6", result);
  HEGEL_ASSERT (result >= 0 && result <= 150,
                "composed result %d out of range [0,150]", result);

  hegel_gen_free (gn);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing composed generators (filter -> map)...\n");
  hegel_run_test (testComposed);
  printf ("PASSED\n");

  return (0);
}
