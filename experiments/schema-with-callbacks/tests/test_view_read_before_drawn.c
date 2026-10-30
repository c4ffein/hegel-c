/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Negative test: a HEGEL_DEPENDS callback that reads a field declared
** AFTER its own must abort loudly, not see a plausible 0.
**
** Fields draw in declaration order, so `hi` is not drawn yet when the
** callback for `lo` runs.  HEGEL_VIEW_FIELD checks the struct's drawn
** bitmap and hegel__abort()s — a schema authoring error, like an
** unresolved HEGEL_USE.  Under fork mode the abort fires SIGABRT in
** the child; the parent catches it and exits non-zero.
**
** Expected: EXIT non-zero (classified as CRASH — like test_binding_unresolved).
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 lo;
  int                 hi;
} Range;

static
hegel_schema_t
lo_below_hi (
const hegel_view *  v,
void *              ctx)
{
  (void) ctx;
  return (hegel_schema_int_range (0, HEGEL_VIEW_FIELD (v, Range, hi)));
}

static hegel_schema_t schema;

static
void
test_read_before_drawn (
hegel_testcase *    tc)
{
  Range *             r;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, schema, (void **) &r);
  printf ("UNREACHABLE: lo=%d hi=%d\n", r->lo, r->hi);
  hegel_shape_free (sh);
}

int
main (void)
{
  schema = HEGEL_STRUCT (Range,
      HEGEL_DEPENDS (int, lo_below_hi, NULL),
      HEGEL_INT (0, 100));
  hegel_run_test (test_read_before_drawn);
  hegel_schema_free (schema);
  return (0);
}
