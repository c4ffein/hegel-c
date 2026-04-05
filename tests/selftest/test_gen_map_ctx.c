/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: map combinator correctly threads void* context.
**
** Property: map(x -> x + *ctx) where ctx points to an int with value 1000.
** Result should always be in [1000, 1010].
** This test should PASS.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
int
addOffset (int val, void * ctx)
{
  int                 offset;

  offset = *(int *) ctx;
  return (val + offset);
}

static
void
testMapCtx (
hegel_testcase *            tc)
{
  int                  offset;
  hegel_gen *          gn;
  int                  result;

  offset = 1000;
  gn = hegel_gen_map_int (hegel_gen_int (0, 10), addOffset, &offset);
  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (result >= 1000 && result <= 1010,
                "map(x+1000) produced %d, expected [1000,1010]", result);

  hegel_gen_free (gn);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing gen_map_int with context...\n");
  hegel_run_test (testMapCtx);
  printf ("PASSED\n");

  return (0);
}
