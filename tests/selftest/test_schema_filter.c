/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_schema_filter_int only produces values satisfying the predicate.
**
** Layer 1: divides_evenly() checks if x is exactly divisible by d.
** Layer 2: filter(x % 3 == 0) on int(0, 99) — result must satisfy
**          divides_evenly(result, 3) and be in [0, 99].
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: function under test ----
** Returns non-zero if x divides evenly by d (no remainder). */

static
int
divides_evenly (
int                         x,
int                         d)
{
  return (x == (x / d) * d);
}

/* ---- filter predicate ---- */

static
int
mod3_eq0 (int val, void * ctx)
{
  (void) ctx;
  return (val % 3 == 0);
}

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t  filter_schema;

static
void
testFilterDiv3 (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, filter_schema);

  HEGEL_ASSERT (divides_evenly (result, 3),
                "filter(x%%3==0) produced %d", result);
  HEGEL_ASSERT (result >= 0 && result <= 99,
                "filter result out of source range: %d", result);

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

  filter_schema = hegel_schema_filter_int (
      hegel_schema_int_range (0, 99), mod3_eq0, NULL);

  printf ("Testing schema filter_int...\n");
  hegel_run_test (testFilterDiv3);
  printf ("PASSED\n");

  hegel_schema_free (filter_schema);
  return (0);
}
