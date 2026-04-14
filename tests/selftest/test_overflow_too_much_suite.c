/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Suite-API version of test_overflow_too_much.c.
**
** Verifies that the large_base_example health check surfaces
** correctly when the offending test runs inside a hegel_suite
** (not just hegel_run_test_n directly), and that an earlier
** passing test in the same suite is unaffected.
**
** Suite structure (in this order):
**   - test_passes_first   : trivial passing test (sanity that the
**                           suite is wired correctly and reaches
**                           the second test)
**   - test_huge_array     : same trigger as test_overflow_too_much.c
**
** Expected: hegel_suite_run returns non-zero, stderr contains
** "Health check failure" AND "ran_passes_first ok" (the marker
** the first test prints to prove the suite reached it).
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct Elem { int x; int y; } Elem;

typedef struct Thing {
  int                 a;
  Elem *              items;
  int                 n_items;
} Thing;

/* ---- Layer 2: hegel tests ---- */

static hegel_schema_t thing_schema;

static
void
test_passes_first (
hegel_testcase *            tc)
{
  int                 x;
  static int          marker_printed = 0;

  x = hegel_draw_int (tc, 0, 100);
  HEGEL_ASSERT (x >= 0 && x <= 100, "x=%d out of [0,100]", x);
  /* Print a one-shot marker so the Makefile can verify the suite
  ** actually ran this test before reaching the failing one. */
  if (! marker_printed) {
    fprintf (stderr, "ran_passes_first ok\n");
    marker_printed = 1;
  }
}

static
void
test_huge_array (
hegel_testcase *            tc)
{
  Thing *             t;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, thing_schema, (void **) &t);
  hegel_shape_free (sh);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  hegel_schema_t      elem;
  hegel_suite *       s;
  int                 rc;

  elem = HEGEL_STRUCT (Elem,
      HEGEL_INT (0, 49),
      HEGEL_INT (0, 49));
  thing_schema = HEGEL_STRUCT (Thing,
      HEGEL_INT (0, 50),
      HEGEL_ARRAY_INLINE (elem, sizeof (Elem), 5000, 10000));

  s = hegel_suite_new ();
  hegel_suite_add (s, "test_passes_first", test_passes_first);
  hegel_suite_add (s, "test_huge_array",   test_huge_array);
  rc = hegel_suite_run (s);
  hegel_suite_free (s);

  hegel_schema_free (thing_schema);
  return (rc);
}
