/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_optional produces present or absent values.
**
** Layer 1: value_or() returns val if present, fallback otherwise.
** Layer 2: optional(int(42,42)) — draw optional, then use value_or()
**          to resolve.  Result must be 42 or the fallback.
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Returns val when present is non-zero, fallback otherwise. */

static
int
value_or (
int                         present,
int                         val,
int                         fallback)
{
  return (present ? val : fallback);
}

/* ---- Layer 2: hegel test ---- */

static
void
testOptional (
hegel_testcase *            tc)
{
  hegel_gen *          gn;
  int                  present;
  int                  out;
  int                  resolved;

  gn = hegel_gen_optional (hegel_gen_int (42, 42));

  out = -1;
  present = hegel_gen_draw_optional_int (tc, gn, &out);

  HEGEL_ASSERT (present == 0 || present == 1,
                "draw_optional_int returned %d, expected 0 or 1", present);

  resolved = value_or (present, out, -1);

  HEGEL_ASSERT (resolved == 42 || resolved == -1,
                "value_or gave %d, expected 42 or -1", resolved);

  if (present) {
    HEGEL_ASSERT (out == 42,
                  "optional present but value=%d, expected 42", out);
  }

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

  printf ("Testing gen_optional...\n");
  hegel_run_test (testOptional);
  printf ("PASSED\n");

  return (0);
}
