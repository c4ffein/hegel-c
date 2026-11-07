/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 2, hegel-c fork mode (the default runner): name_len (id) > 0 for
** id in [0, 100]; name_len dereferences NULL for id >= 42.  Each case
** runs in a forked child, so the crash is a shrinkable failure.
*/
#include "hegel_c.h"
#include "common.h"
#include "sut.h"

static void prop (hegel_testcase * tc)
{
  count_exec ();
  int id = hegel_draw_int (tc, 0, 100);
  HEGEL_ASSERT (name_len (id) > 0, "empty name for id %d", id);
}

/* B{ */
int main (void)
{
  hegel_run_test_n (prop, 100);
  return 0;
}
/* }B */
