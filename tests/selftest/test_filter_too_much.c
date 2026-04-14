/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Regression test for the "filter_too_much" health-check path.
**
** When a HEGEL_FILTER_INT predicate rejects too many candidate
** values, hegel-c's underlying Hypothesis-style engine fires a
** "filter_too_much" health check.  This must reach the C user as:
**   1. A non-zero exit code (the test does NOT silently pass).
**   2. A clear stderr message naming the cause and the suppress
**      escape hatch (so the user knows what to fix).
**
** This file is the SINGLE-test version (calls hegel_run_test_n
** directly).  See test_filter_too_much_suite.c for the suite-API
** version.
**
** Why this matters: the orphan-leak fix in rust-version/src/lib.rs
** wraps parent_serve in catch_unwind to handle the __HEGEL_STOP_TEST
** sentinel cleanly.  filter_too_much takes the SAME panic path but
** with a different sentinel string.  This test pins down that the
** fix correctly handles all parent_serve panic types, not just the
** one we originally diagnosed.
**
** Three-layer pattern (selftest convention):
**   Layer 1: only_accept_zero — predicate rejecting ~99.9999% of
**            inputs from a [-1_000_000, 1_000_000] range.
**   Layer 2: test_too_strict — uses HEGEL_FILTER_INT with that
**            predicate; the test body never reaches anything
**            meaningful because every draw is filtered out.
**   Layer 3: the Makefile asserts: exit non-zero AND stderr
**            contains "Health check failure".
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

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t thing_schema;

static
void
test_too_strict (
hegel_testcase *            tc)
{
  Thing *             t;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, thing_schema, (void **) &t);
  /* Test body is intentionally trivial — the interesting behavior
  ** is in the engine deciding to abort generation, not in any
  ** assertion the test could make. */
  hegel_shape_free (sh);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  thing_schema = HEGEL_STRUCT (Thing,
      HEGEL_FILTER_INT (hegel_schema_int_range (-1000000, 1000000),
                        only_accept_zero, NULL));
  hegel_run_test_n (test_too_strict, 100);
  /* Should not reach here — health check should panic out. */
  hegel_schema_free (thing_schema);
  return (0);
}
