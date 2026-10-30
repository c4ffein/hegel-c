/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Regression test for the "filter_too_much" health-check path.
**
** When a filter (a draw, then hegel_assume on a predicate) rejects
** too many candidate values, the engine fires a "filter_too_much"
** health check.  This must reach the C user as:
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
**   Layer 2: test_too_strict — draws and assumes with that
**            predicate; the test body never reaches anything
**            meaningful because every draw is filtered out.
**   Layer 3: the Makefile asserts: exit non-zero AND stderr
**            contains "Health check failure".
*/

#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: predicate ---- */

static
int
only_accept_zero (
int                         v)
{
  return (v == 0);
}

/* ---- Layer 2: hegel test ---- */

static
void
test_too_strict (
hegel_testcase *            tc)
{
  int                 x;

  /* A filter: draw, and reject the case when the predicate fails.
  ** The body is intentionally trivial — the interesting behavior is
  ** the engine deciding to abort generation. */
  x = hegel_draw_int (tc, -1000000, 1000000);
  hegel_assume (tc, only_accept_zero (x));
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  hegel_run_test_n (test_too_strict, 100);
  /* Should not reach here — health check should panic out. */
  return (0);
}
