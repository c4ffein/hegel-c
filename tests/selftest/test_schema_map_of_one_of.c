/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a combinator whose source is HEGEL_ONE_OF must recurse into
** the chosen alternative at draw time.
**
** Regression guard: before the kind-dispatch fix to draw_integer_into /
** draw_fp_into, a MAP_INT or FILTER_INT wrapping a ONE_OF_SCALAR source
** silently wrote 0 (fell through the default arm), because the helper
** only knew INTEGER / MAP_* / FILTER_* / FLAT_MAP_*.  `map(one_of(...))`
** then always produced fn(0), hiding the branch entirely.
**
** Strategy: use HEGEL_ONE_OF of two single-value ranges so the "no
** choice" failure mode is obvious: without ONE_OF support, the draw
** produces 0, map_fn returns fn(0) = 0, and the assertion fails.
**
** Layer 1: identity_int returns its input.
** Layer 2: map(identity, one_of(int(1,1), int(100,100))) ∈ {1, 100}.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: function under test ---- */

static
int
identity_int (int val, void * ctx)
{
  (void) ctx;
  return (val);
}

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t  map_of_one_of_int;
static hegel_schema_t  filter_of_one_of_int;

static
int
is_large (int val, void * ctx)
{
  (void) ctx;
  return (val >= 50);
}

static
void
testMapOfOneOf (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, map_of_one_of_int);

  HEGEL_ASSERT (result == 1 || result == 100,
                "map(identity, one_of(int(1,1), int(100,100))) produced %d,"
                " expected 1 or 100", result);

  hegel_shape_free (sh);
}

static
void
testFilterOfOneOf (
hegel_testcase *            tc)
{
  int                  result = 0;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, filter_of_one_of_int);

  HEGEL_ASSERT (result == 100,
                "filter(>=50, one_of(int(1,1), int(100,100))) produced %d,"
                " expected 100", result);

  hegel_shape_free (sh);
}

/* ---- Layer 3: runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  map_of_one_of_int = hegel_schema_map_int (
      HEGEL_ONE_OF (hegel_schema_int_range (1, 1),
                    hegel_schema_int_range (100, 100)),
      identity_int, NULL);

  filter_of_one_of_int = hegel_schema_filter_int (
      HEGEL_ONE_OF (hegel_schema_int_range (1, 1),
                    hegel_schema_int_range (100, 100)),
      is_large, NULL);

  printf ("  map(identity, one_of(int(1,1), int(100,100)))...\n");
  hegel_run_test (testMapOfOneOf);
  printf ("    PASSED\n");

  printf ("  filter(>=50, one_of(int(1,1), int(100,100)))...\n");
  hegel_run_test (testFilterOfOneOf);
  printf ("    PASSED\n");

  hegel_schema_free (map_of_one_of_int);
  hegel_schema_free (filter_of_one_of_int);
  return (0);
}
