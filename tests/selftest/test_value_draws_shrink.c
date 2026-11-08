/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Value draws shrink like values: a failure that depends on a date or a
** datetime shrinks to the boundary of the failing region, not to some
** arbitrary member of it.
**
** EXPECTED_SHRINK: 2038-01-19T03:14:08 1999-12-31
**
** The engine shrinks a date toward 2000-01-01 by distance, as one value,
** not field by field: of the failing dates (every March, June,
** September and December) the nearest to 2000-01-01 is 1999-12-31, a
** day before it; a field-by-field shrinker would stop at 2000-03-01.
** Same result on 10 seeds (libhegel 0.50.0).
**
** Three layers:
**   1. Functions under test — epoch32() converts a datetime to seconds
**      since 1970 in an int32_t (the year-2038 bug: it wraps negative
**      after 2038-01-19T03:14:07); quarter_of() maps a date to its
**      calendar quarter (bug: off by one on the last month of each
**      quarter, so March gives Q2)
**   2. Hegel test           — epoch32 is non-negative for datetimes from
**      1970 on; the month lies in the quarter quarter_of names
**   3. Makefile runner      — TESTS_SHRINK; both properties fail, run
**      nofork (the last failing value is the shrunk one), main prints
**      them on one "shrunk:" line and exits 1 */

#include "hegel_c.h"

#include <stdio.h>

/* ---- Layer 1: functions under test ---- */

static int64_t days_from_civil (int64_t y, int m, int d)
{
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * ((m + 9) % 12) + 2) / 5 + d - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static int32_t epoch32 (hegel_datetime dt)
{
  int64_t days = days_from_civil (dt.date.year, dt.date.month, dt.date.day);
  int64_t secs = ((days * 24 + dt.time.hour) * 60 + dt.time.minute) * 60 + dt.time.second;
  return (int32_t) (uint32_t) secs;    /* BUG: time_t squeezed into 32 bits */
}

static int quarter_of (hegel_date d)
{
  return d.month / 3 + 1;              /* BUG: should be (month - 1) / 3 + 1 */
}

/* ---- Layer 2: properties ---- */

static hegel_datetime last_dt;
static hegel_date     last_date;

static void prop_epoch32 (hegel_testcase * tc)
{
  static const hegel_datetime from_1970 = { { 1970, 1, 1 }, { 0, 0, 0, 0 } };
  hegel_datetime dt = hegel_draw_datetime (tc, &from_1970, NULL);
  last_dt = dt;
  HEGEL_ASSERT (epoch32 (dt) >= 0, "epoch32 went negative");
}

static void prop_quarter (hegel_testcase * tc)
{
  hegel_date d = hegel_draw_date (tc, NULL, NULL);
  last_date = d;
  int q = quarter_of (d);
  HEGEL_ASSERT (q >= 1 && q <= 4 && d.month >= 3 * q - 2 && d.month <= 3 * q,
                "month %u is not in Q%d", d.month, q);
}

/* ---- Layer 3: runner (see Makefile TESTS_SHRINK) ---- */

int main (void)
{
  hegel_run_options o = { .test_cases = 1000 };
  int failed = 0;
  failed += hegel_run_test_nofork_with (prop_epoch32, &o);
  hegel_datetime dt = last_dt;
  failed += hegel_run_test_nofork_with (prop_quarter, &o);
  printf ("shrunk: %04d-%02u-%02uT%02u:%02u:%02u %04d-%02u-%02u\n",
          (int) dt.date.year, dt.date.month, dt.date.day,
          dt.time.hour, dt.time.minute, dt.time.second,
          (int) last_date.year, last_date.month, last_date.day);
  return failed == 2 ? 1 : 0;
}
