/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_schema_flat_map_int produces dependent generation.
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
#include "hegel_gen.h"

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
hegel_schema_t
make_range_schema (int n, void * ctx)
{
  (void) ctx;
  return (hegel_schema_int_range (0, n));
}

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t  flat_map_schema;

static
void
testFlatMap (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, flat_map_schema);

  HEGEL_ASSERT (in_range (result, 0, 100),
                "flat_map result %d not in [0, 100]", result);

  hegel_shape_free (sh);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  flat_map_schema = hegel_schema_flat_map_int (
      hegel_schema_int_range (1, 100), make_range_schema, NULL);

  printf ("Testing schema flat_map_int...\n");
  hegel_run_test (testFlatMap);
  printf ("PASSED\n");

  hegel_schema_free (flat_map_schema);
  return (0);
}
