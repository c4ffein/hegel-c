/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Value draws refuse bad arguments as health failures (the test setup is
** broken, not the code under test): a bound that is not a real date,
** inverted time bounds, a UUID version over 15, a domain max_length under
** 4.  Each is its own run, in fork and in nofork mode; every one must be
** refused.
**
** Three layers:
**   1. Function under test  — days_in_february(), whose result a broken
**      generator below uses as a bound without checking the year
**   2. Hegel tests          — four broken generators
**   3. Makefile runner      — TESTS_HEALTH: exit non-zero with "Health
**      check failure" in the output (main exits 1 only if all four runs
**      were refused) */

#include "hegel_c.h"

#include <stdio.h>

/* ---- Layer 1: function under test ---- */

static int days_in_february (int year)
{
  (void) year;
  return 30;                           /* wrong for every year */
}

/* ---- Layer 2: broken generators ---- */

static void test_bad_date_bound (hegel_testcase * tc)
{
  hegel_date lo = { 2001, 2, 1 };
  hegel_date hi = { 2001, 2, (uint8_t) days_in_february (2001) };   /* 2001-02-30 */
  hegel_date d = hegel_draw_date (tc, &lo, &hi);
  HEGEL_ASSERT (d.month == 2, "unreachable");
}

static void test_inverted_time (hegel_testcase * tc)
{
  hegel_time lo = { 18, 0, 0, 0 }, hi = { 9, 0, 0, 0 };
  hegel_time t = hegel_draw_time (tc, &lo, &hi);
  HEGEL_ASSERT (t.hour < 24, "unreachable");
}

static void test_bad_uuid_version (hegel_testcase * tc)
{
  hegel_uuid u = hegel_draw_uuid (tc, 16);
  HEGEL_ASSERT (u.bytes[0] || !u.bytes[0], "unreachable");
}

static void test_bad_domain_length (hegel_testcase * tc)
{
  char buf[16];
  int n = hegel_draw_domain (tc, 3, buf, sizeof buf);
  HEGEL_ASSERT (n >= 0, "unreachable");
}

/* ---- Layer 3: runner (see Makefile TESTS_HEALTH) ---- */

int main (void)
{
  int refused = 0;
  refused += hegel_run_test_nofork_with (test_bad_date_bound, NULL);
  refused += hegel_run_test_with (test_inverted_time, NULL);
  refused += hegel_run_test_with (test_bad_uuid_version, NULL);
  refused += hegel_run_test_nofork_with (test_bad_domain_length, NULL);
  printf ("%d of 4 misuses refused\n", refused);
  return refused == 4 ? 1 : 0;
}
