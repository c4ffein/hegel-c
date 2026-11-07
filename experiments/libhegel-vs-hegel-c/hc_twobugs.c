/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 5, hegel-c: the same two checks on midpoint (lo, hi): A, the
** result lies in [lo, hi]; B, it is floor ((lo + hi) / 2).  Each
** HEGEL_ASSERT's origin is its file:line.
*/
#include "hegel_c.h"
#include "common.h"
#include "sut.h"

static void prop (hegel_testcase * tc)
{
  count_exec ();
  int lo = hegel_draw_int (tc, INT_MIN, INT_MAX);
  int hi = hegel_draw_int (tc, INT_MIN, INT_MAX);
  if (lo > hi) { int t = lo; lo = hi; hi = t; }
  int m = midpoint (lo, hi);
  HEGEL_ASSERT (m >= lo && m <= hi, "lo = %d, hi = %d, midpoint = %d", lo, hi, m);
  HEGEL_ASSERT (m == floor_half ((int64_t) lo + hi), "lo = %d, hi = %d, midpoint = %d",
                lo, hi, m);
}

/* B{ */
int main (void)
{
  hegel_run_test_n (prop, 100);
  return 0;
}
/* }B */
