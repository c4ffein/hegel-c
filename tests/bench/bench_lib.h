/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Shared helpers for the bench binaries.
**
** The bench binaries are built twice from the same source — once without
** HEGEL_BENCH_NOFORK (fork mode), once with (nofork mode).  BENCH_RUN_N
** dispatches to the right runner based on that flag so the test body
** stays identical.
**
** BENCH_N_CASES is the number of hegel cases per run; override from the
** Makefile with -DBENCH_N_CASES=5000.  BENCH_SUITE_TESTS is the number
** of tests in the suite binary (same idea).
*/

#ifndef HEGEL_BENCH_LIB_H
#define HEGEL_BENCH_LIB_H

#include "hegel_c.h"

#ifndef BENCH_N_CASES
#define BENCH_N_CASES 1000
#endif

#ifndef BENCH_SUITE_TESTS
#define BENCH_SUITE_TESTS 10
#endif

#ifdef HEGEL_BENCH_NOFORK
#define BENCH_MODE_NAME "nofork"
#define BENCH_RUN_N(fn, n) hegel_run_test_nofork_n ((fn), (n))
#else
#define BENCH_MODE_NAME "fork"
#define BENCH_RUN_N(fn, n) hegel_run_test_n ((fn), (n))
#endif

#endif /* HEGEL_BENCH_LIB_H */
