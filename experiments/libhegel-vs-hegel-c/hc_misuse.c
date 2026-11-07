/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 6, hegel-c: the same bug in the test: len in [0, 10], then idx in
** [0, len - 1], an inverted range when len = 0.  The property holds.
*/
#include "hegel_c.h"
#include "common.h"
#include "sut.h"

static void prop (hegel_testcase * tc)
{
  count_exec ();
  int len = hegel_draw_int (tc, 0, 10);
  int idx = hegel_draw_int (tc, 0, len - 1);
  HEGEL_ASSERT (idx < len, "len = %d, idx = %d", len, idx);
}

/* B{ */
int main (void)
{
  hegel_run_test_n (prop, 100);
  return 0;
}
/* }B */
