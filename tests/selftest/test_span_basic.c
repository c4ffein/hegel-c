/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: spans wrap structured draws without breaking generation.
**
** Layer 1: pair_sum returns x + y (trivially correct).
** Layer 2: hegel draws a list of (x, y) pairs.  Each pair is wrapped
**          in a HEGEL_SPAN_LIST_ELEMENT span, the whole list in a
**          HEGEL_SPAN_LIST span.  The property is the identity
**          pair_sum(x, y) == x + y, so the test should PASS — its job
**          is to verify that hegel_start_span / hegel_stop_span work
**          in fork mode (the IPC path) and do not break draws or
**          interfere with the test runner.
**
** Expected: EXIT 0.
*/
#include <stdio.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
int
pair_sum (
int                         a,
int                         b)
{
  return (a + b);
}

/* ---- Layer 2: hegel test ---- */

static
void
testSpans (
hegel_testcase *            tc)
{
  int                 n;
  int                 i;
  int                 x;
  int                 y;

  hegel_start_span (tc, HEGEL_SPAN_LIST);
  n = hegel_draw_int (tc, 0, 5);
  for (i = 0; i < n; i ++) {
    hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);
    x = hegel_draw_int (tc, 0, 100);
    y = hegel_draw_int (tc, 0, 100);
    HEGEL_ASSERT (pair_sum (x, y) == x + y,
                  "pair_sum(%d, %d) != %d", x, y, x + y);
    hegel_stop_span (tc, 0);
  }
  hegel_stop_span (tc, 0);
}

/* ---- Layer 3: runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing spans (manual list of pairs)...\n");
  hegel_run_test (testSpans);
  printf ("PASSED\n");

  return (0);
}
