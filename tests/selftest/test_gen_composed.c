/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: composing multiple combinators works correctly.
**
** Layer 1: is_divisible_by() checks divisibility.
** Layer 2: chain int(0,50) -> filter(even) -> map(x*3).
**          Result must satisfy is_divisible_by(result, 6) and be in [0, 150].
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Returns non-zero if x is divisible by d. */

static
int
is_divisible_by (
int                         x,
int                         d)
{
  return (x % d == 0);
}

/* ---- combinator callbacks ---- */

static
int
is_even (int val, void * ctx)
{
  (void) ctx;
  return (val % 2 == 0);
}

static
int
triple_it (int val, void * ctx)
{
  (void) ctx;
  return (val * 3);
}

/* ---- Layer 2: hegel test ---- */

static
void
testComposed (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  result;

  /* filter first, then map — both take ownership of source */
  gn = hegel_gen_filter_int (hegel_gen_int (0, 50), is_even, NULL);
  gn = hegel_gen_map_int (gn, triple_it, NULL);

  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (is_divisible_by (result, 6),
                "filter(even) -> map(*3) produced %d, not divisible by 6", result);
  HEGEL_ASSERT (result >= 0 && result <= 150,
                "composed result %d out of range [0,150]", result);

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

  printf ("Testing composed generators (filter -> map)...\n");
  hegel_run_test (testComposed);
  printf ("PASSED\n");

  return (0);
}
