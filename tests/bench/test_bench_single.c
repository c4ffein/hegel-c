/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Single-test bench: one trivial test, BENCH_N_CASES iterations.
** Deliberately minimal test body so the measured time is dominated
** by framework overhead, not user code.  Built twice: once as
** test_bench_single (fork mode), once as test_bench_single_nofork
** via -DHEGEL_BENCH_NOFORK. */

#include "bench_lib.h"

static void bench_trivial (hegel_testcase * tc) {
  int x = hegel_draw_int (tc, 0, 100);
  int y = hegel_draw_int (tc, 0, 100);
  HEGEL_ASSERT (x + y <= 200, "unreachable: %d + %d", x, y);
}

int main (void) {
  BENCH_RUN_N (bench_trivial, BENCH_N_CASES);
  return 0;
}
