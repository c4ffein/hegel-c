/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 2, hegel-c nofork mode + HEGEL_ISOLATED: the body runs in the
** runner's process (like raw_crash.c) and only the code under test is
** forked, after the draws (like raw_crash_fork.c).
*/
#include "hegel_c.h"
#include "common.h"
#include "sut.h"

static void prop (hegel_testcase * tc)
{
  count_exec ();
  int id = hegel_draw_int (tc, 0, 100);
  HEGEL_ISOLATED (tc) {
    HEGEL_ASSERT (name_len (id) > 0, "empty name for id %d", id);
  }
}

/* B{ */
int main (void)
{
  hegel_run_test_nofork_n (prop, 100);
  return 0;
}
/* }B */
