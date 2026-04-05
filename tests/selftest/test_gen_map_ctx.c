/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: map combinator correctly threads void* context.
**
** Layer 1: unbias() removes a known offset from a value.
** Layer 2: map(x -> x + *ctx) where ctx points to 1000.
**          Drawing from the mapped generator then calling unbias()
**          should recover a value in the original source range [0, 10].
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Subtracts a known bias from x — inverse of the offset mapping. */

static
int
unbias (
int                         x,
int                         bias)
{
  return (x - bias);
}

/* ---- map callback ---- */

static
int
add_offset (int val, void * ctx)
{
  int                 offset;

  offset = *(int *) ctx;
  return (val + offset);
}

/* ---- Layer 2: hegel test ---- */

static
void
testMapCtx (
hegel_testcase *            tc)
{
  int                  offset;
  hegel_gen *          gn;
  int                  result;
  int                  original;

  offset = 1000;
  gn = hegel_gen_map_int (hegel_gen_int (0, 10), add_offset, &offset);
  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (result >= 1000 && result <= 1010,
                "map(x+1000) produced %d, expected [1000,1010]", result);

  original = unbias (result, 1000);
  HEGEL_ASSERT (original >= 0 && original <= 10,
                "unbias(%d, 1000) = %d, expected [0,10]", result, original);

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

  printf ("Testing gen_map_int with context...\n");
  hegel_run_test (testMapCtx);
  printf ("PASSED\n");

  return (0);
}
