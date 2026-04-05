/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_flat_map_int produces dependent generation.
**
** Property: flat_map(n -> int(0, n)) on int(1,100) always produces
** a result in [0, n] where n is the source draw.
**
** We can't observe n directly in the assertion (it's inside the draw),
** but we CAN assert result <= 100 (the max of the source range).
** The real test is that this doesn't crash or produce garbage.
** This test should PASS.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
hegel_gen *
makeRangeGen (int n, void * ctx)
{
  (void) ctx;
  return (hegel_gen_int (0, n));
}

static
void
testFlatMap (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  result;

  gn = hegel_gen_flat_map_int (hegel_gen_int (1, 100), makeRangeGen, NULL);
  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (result >= 0,
                "flat_map result negative: %d", result);
  HEGEL_ASSERT (result <= 100,
                "flat_map result exceeds source max: %d", result);

  hegel_gen_free (gn);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing gen_flat_map_int...\n");
  hegel_run_test (testFlatMap);
  printf ("PASSED\n");

  return (0);
}
