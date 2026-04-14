/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Regression test for the "large_base_example" health-check path —
** the case where every test case overflows the byte budget because
** the schema's minimum-size input is too big.
**
** This test sits next to test_filter_too_much.c.  They cover
** different Hypothesis health checks but share the same code path
** in hegel-c: a panic out of tc.draw() inside parent_serve, caught
** by the catch_unwind we added in run_forked, propagated cleanly
** to the user as a non-zero exit + visible "Health check failure"
** message.
**
** Why both tests are needed: the orphan-leak bug we fixed was
** specifically observed via __HEGEL_STOP_TEST.  Both health checks
** here surface as ordinary panics (NOT __HEGEL_STOP_TEST), meaning
** they take the runner.rs:902 "else" branch (TestCaseResult::
** Interesting → reported as fatal failure).  Together, this pair
** of tests pins down that our parent_serve catch_unwind handles
** every panic flavor, not just the original sentinel.
**
** Schema: HEGEL_ARRAY_INLINE with min_len=5000 of 2-int structs.
** Per case minimum: 1 (length) + 5000*2 = 10001 int draws.
** Hypothesis's default byte budget is around 8 KB and each int
** draw consumes several bytes, so the smallest possible input
** already exceeds the budget — every case overflows.
**
** Three-layer pattern:
**   Layer 1: Thing/Elem types — a struct holding a malloc'd inline
**            array of (x, y) pairs.
**   Layer 2: test_huge_array — draws Thing, never reaches anything
**            meaningful because every draw overflows.
**   Layer 3: Makefile asserts non-zero exit AND "Health check
**            failure" in stderr (TESTS_HEALTH category).
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

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t thing_schema;

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

  elem = HEGEL_STRUCT (Elem,
      HEGEL_INT (0, 49),
      HEGEL_INT (0, 49));
  thing_schema = HEGEL_STRUCT (Thing,
      HEGEL_INT (0, 50),
      HEGEL_ARRAY_INLINE (elem, sizeof (Elem), 5000, 10000));
  hegel_run_test_n (test_huge_array, 20);
  /* Should not reach here — health check should panic out. */
  hegel_schema_free (thing_schema);
  return (0);
}
