/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Draw-loop bench: a small number of cases, many draws per case.
** Isolates the per-draw IPC cost from the per-case fork-setup cost.
**
** The existing test_bench_single runs BENCH_N_CASES cases × 2 draws/case,
** so fork setup dominates and per-draw cost is hard to read off the
** wall time.  This bench inverts the ratio: few cases, many draws.
**
** (fork_wall - nofork_wall) / (BENCH_N_CASES * BENCH_N_DRAWS)
**   ≈ per-draw pipe roundtrip cost (child↔parent proxy).
**
** Built twice, like the other bench sources: once without
** HEGEL_BENCH_NOFORK (fork mode), once with.  BENCH_RUN_N in
** bench_lib.h dispatches. */

#include "bench_lib.h"

#ifndef BENCH_N_DRAWS
#define BENCH_N_DRAWS 1000
#endif

static void bench_drawloop (hegel_testcase * tc) {
  /* volatile sink prevents the compiler from dropping the loop. */
  volatile int sink = 0;
  for (int i = 0; i < BENCH_N_DRAWS; i++) {
    sink += hegel_draw_int (tc, 0, 100);
  }
  (void) sink;
}

int main (void) {
  BENCH_RUN_N (bench_drawloop, BENCH_N_CASES);
  return 0;
}
