/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Suite-mode bench: BENCH_SUITE_TESTS trivial tests in a single
** binary.  Measures amortized-startup overhead — the realistic
** multi-test shape.
**
** In fork mode the suite API shares one server across all tests.
** In nofork mode there is no suite runner, so we call
** hegel_run_test_nofork sequentially.  The hegeltest crate keeps a
** process-wide singleton server, so startup is amortized across
** the sequential calls the same way the suite API amortizes it
** across forked children.
**
** Both variants rely on hegeltest's default case count per test —
** hegel_suite_run does not accept a case-count override, so using
** the default on both sides keeps the comparison apples-to-apples.
** BENCH_N_CASES is therefore only consulted by the single-test
** bench, not here. */

#include "bench_lib.h"

static void bench_t0 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t1 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t2 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t3 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t4 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t5 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t6 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t7 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t8 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }
static void bench_t9 (hegel_testcase * tc) { (void) hegel_draw_int (tc, 0, 100); }

typedef void (*bench_fn) (hegel_testcase *);

static const bench_fn bench_fns[BENCH_SUITE_TESTS] = {
  bench_t0, bench_t1, bench_t2, bench_t3, bench_t4,
  bench_t5, bench_t6, bench_t7, bench_t8, bench_t9,
};

#ifdef HEGEL_BENCH_NOFORK

int main (void) {
  for (int i = 0; i < BENCH_SUITE_TESTS; i++) {
    hegel_run_test_nofork (bench_fns[i]);
  }
  return 0;
}

#else

static const char * bench_names[BENCH_SUITE_TESTS] = {
  "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
};

int main (void) {
  hegel_suite * s = hegel_suite_new ();
  for (int i = 0; i < BENCH_SUITE_TESTS; i++) {
    hegel_suite_add (s, bench_names[i], bench_fns[i]);
  }
  int rc = hegel_suite_run (s);
  hegel_suite_free (s);
  return rc;
}

#endif
