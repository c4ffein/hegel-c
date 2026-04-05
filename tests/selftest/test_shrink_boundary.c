/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel detects a failure and shrinks toward the boundary.
**
** Layer 1: clamp99() is supposed to cap values at 99 but is a no-op (bug).
** Layer 2: hegel draws x in [0, 10000] and asserts clamp99(x) < 100.
**          Hegel should shrink to exactly 100 (the smallest failing value).
**
** Expected: EXIT NON-ZERO.
*/
#include <stdio.h>
#include <limits.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Supposed to clamp x to [0, 99] for use as an array index.
** Bug: forgot the actual clamping — returns x unchanged. */

static
int
clamp99 (
int                         x)
{
  return (x);
}

/* ---- Layer 2: hegel test ---- */

static
void
testClampBound (
hegel_testcase *            tc)
{
  int                 x;
  int                 result;

  x = hegel_draw_int (tc, 0, 10000);
  result = clamp99 (x);
  HEGEL_ASSERT (result < 100,
                "clamp99(%d) = %d, expected < 100", x, result);
}

/* ---- Layer 3: runner (see Makefile TESTS_FAIL) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  hegel_run_test (testClampBound);

  return (0);
}
