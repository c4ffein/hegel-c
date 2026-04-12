/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Suite-API version of test_filter_too_much.c.
**
** This test verifies three things at once:
**   1. The same filter_too_much trigger surfaces correctly when
**      run inside hegel_suite_new/add/run, not just hegel_run_test_n.
**   2. The suite continues running subsequent tests after an
**      earlier passing one (i.e., the failing test doesn't taint
**      the prior pass).
**   3. The suite reports the failure via its return code AND emits
**      the same "Health check failure" message to stderr that the
**      single-test version produces.
**
** Suite structure (run in this order):
**   - test_passes_first  : trivial passing test (sanity that the
**                          suite is wired correctly)
**   - test_too_strict    : same trigger as test_filter_too_much.c
**
** Expected: hegel_suite_run returns non-zero, stderr contains
** "Health check failure", binary exits non-zero.
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct Thing {
  int                 x;
} Thing;

/* ---- Layer 1: predicate ---- */

static
int
only_accept_zero (
int                         v,
void *                      ctx)
{
  (void) ctx;
  return (v == 0);
}

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
  /* One-shot marker so the Makefile can verify the suite reached
  ** this test before the failing one. */
  if (! marker_printed) {
    fprintf (stderr, "ran_passes_first ok\n");
    marker_printed = 1;
  }
}

static
void
test_too_strict (
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
  hegel_suite *       s;
  int                 rc;

  thing_schema = hegel_schema_struct (sizeof (Thing),
      HEGEL_FILTER_INT (Thing, x,
                        hegel_schema_int_range (-1000000, 1000000),
                        only_accept_zero, NULL));

  s = hegel_suite_new ();
  hegel_suite_add (s, "test_passes_first", test_passes_first);
  hegel_suite_add (s, "test_too_strict",   test_too_strict);
  rc = hegel_suite_run (s);
  hegel_suite_free (s);

  hegel_schema_free (thing_schema);

  /* Suite returns non-zero because test_too_strict triggers
  ** the health check.  Pass that signal up to our exit. */
  return (rc);
}
