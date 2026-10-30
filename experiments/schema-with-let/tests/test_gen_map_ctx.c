/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: schema map combinator correctly threads void* context.
**
** Layer 1: unbias() removes a known offset from a value.
** Layer 2: map(x -> x + *ctx) where ctx points to 1000.
**          Drawing from the mapped schema then calling unbias()
**          should recover a value in the original source range [0, 10].
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

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

static int             offset = 1000;
static hegel_schema_t  map_ctx_schema;

static
void
testMapCtx (
hegel_testcase *            tc)
{
  int                  result = 0;
  int                  original;
  hegel_shape *        sh;

  sh = HEGEL_DRAW (&result, map_ctx_schema);

  HEGEL_ASSERT (result >= 1000 && result <= 1010,
                "map(x+1000) produced %d, expected [1000,1010]", result);

  original = unbias (result, 1000);
  HEGEL_ASSERT (original >= 0 && original <= 10,
                "unbias(%d, 1000) = %d, expected [0,10]", result, original);

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

  map_ctx_schema = hegel_schema_map_int (
      hegel_schema_int_range (0, 10), add_offset, &offset);

  printf ("Testing schema map_int with context...\n");
  hegel_run_test (testMapCtx);
  printf ("PASSED\n");

  hegel_schema_free (map_ctx_schema);
  return (0);
}
