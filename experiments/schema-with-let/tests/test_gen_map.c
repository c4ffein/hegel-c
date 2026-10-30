/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_schema_map_int produces correctly transformed values.
**
** Layer 1: is_even() checks whether a number is even.
** Layer 2: map(x -> x*2) on int(0, 50) — result must always be even
**          and in [0, 100].  Validates that the map combinator produces
**          correctly transformed values.
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: function under test ----
** Returns non-zero if x is even. */

static
int
is_even (
int                         x)
{
  return (x % 2 == 0);
}

/* ---- map callback ---- */

static
int
double_it (int val, void * ctx)
{
  (void) ctx;
  return (val * 2);
}

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t  map_schema;

static
void
testMapDouble (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, map_schema);

  HEGEL_ASSERT (is_even (result),
                "map(x->x*2) produced odd number: %d", result);
  HEGEL_ASSERT (result >= 0 && result <= 100,
                "map(x->x*2) out of range: %d", result);

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

  map_schema = hegel_schema_map_int (
      hegel_schema_int_range (0, 50), double_it, NULL);

  printf ("Testing schema map_int...\n");
  hegel_run_test (testMapDouble);
  printf ("PASSED\n");

  hegel_schema_free (map_schema);
  return (0);
}
