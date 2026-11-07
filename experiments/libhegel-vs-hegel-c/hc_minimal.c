/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 1, hegel-c: n in [0, 100] must fit a 5-slot buffer.
*/
#include "hegel_c.h"
#include "common.h"
#include "sut.h"

static void prop (hegel_testcase * tc)
{
  count_exec ();
  int n = hegel_draw_int (tc, 0, 100);
  HEGEL_ASSERT (buffer_fits (n), "n = %d does not fit", n);
}

/* B{ */
int main (void)
{
  hegel_run_test_n (prop, 100);
  return 0;
}
/* }B */
