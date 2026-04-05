/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_flat_map_int produces dependent generation.
**
** Layer 1: in_range() checks if x is within [lo, hi].
** Layer 2: flat_map(n -> int(0, n)) on int(1, 100) — result must
**          satisfy in_range(result, 0, 100).  The real test is that
**          dependent generation doesn't crash or produce garbage.
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Returns non-zero if x is in [lo, hi] (inclusive). */

static
int
in_range (
int                         x,
int                         lo,
int                         hi)
{
  return (x >= lo && x <= hi);
}

/* ---- flat_map callback ---- */

static
hegel_gen *
make_range_gen (int n, void * ctx)
{
  (void) ctx;
  return (hegel_gen_int (0, n));
}

/* ---- Layer 2: hegel test ---- */

static
void
testFlatMap (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  result;

  gn = hegel_gen_flat_map_int (hegel_gen_int (1, 100), make_range_gen, NULL);
  result = hegel_gen_draw_int (tc, gn);

  HEGEL_ASSERT (in_range (result, 0, 100),
                "flat_map result %d not in [0, 100]", result);

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

  printf ("Testing gen_flat_map_int...\n");
  hegel_run_test (testFlatMap);
  printf ("PASSED\n");

  return (0);
}
