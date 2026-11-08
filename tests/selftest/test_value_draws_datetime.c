/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Date / time / datetime draws: every value is a real calendar date or
** time of day, inside the bounds asked for.  Run PASSES (exit 0).
**
** Three layers:
**   1. Function under test  — date_to_days / days_to_date, a civil
**      calendar <-> day-number conversion (Howard Hinnant's algorithm),
**      plus the validity checks it relies on
**   2. Hegel test           — default-bounded draws are valid and round-
**      trip through the conversion; draws between two drawn bounds stay
**      inside them; the engine's full year range (+-999999) works
**   3. Makefile runner      — TESTS_PASS; each property runs in fork
**      mode AND in nofork mode, must exit 0 */

#include "hegel_c.h"

#include <stdio.h>

/* ---- Layer 1: functions under test ---- */

static int is_leap (int64_t y)
{
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month (int64_t y, int m)
{
  static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  return m == 2 && is_leap (y) ? 29 : d[m - 1];
}

static int date_is_valid (hegel_date d)
{
  return d.month >= 1 && d.month <= 12 && d.day >= 1 && d.day <= days_in_month (d.year, d.month);
}

static int time_is_valid (hegel_time t)
{
  return t.hour <= 23 && t.minute <= 59 && t.second <= 59 && t.nanosecond <= 999999999u;
}

/* Days since 1970-01-01 (proleptic Gregorian, any year). */
static int64_t date_to_days (hegel_date d)
{
  int64_t y = (int64_t) d.year - (d.month <= 2);
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t mp = (d.month + 9) % 12;
  int64_t doy = (153 * mp + 2) / 5 + d.day - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static hegel_date days_to_date (int64_t z)
{
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t d = doy - (153 * mp + 2) / 5 + 1;
  int64_t m = mp < 10 ? mp + 3 : mp - 9;
  hegel_date r = { (int32_t) (yoe + era * 400 + (m <= 2)), (uint8_t) m, (uint8_t) d };
  return r;
}

static int date_cmp (hegel_date a, hegel_date b)
{
  int64_t x = date_to_days (a), y = date_to_days (b);
  return x < y ? -1 : x > y;
}

static int time_cmp (hegel_time a, hegel_time b)
{
  uint64_t x = ((a.hour * 60u + a.minute) * 60u + a.second) * 1000000000ull + a.nanosecond;
  uint64_t y = ((b.hour * 60u + b.minute) * 60u + b.second) * 1000000000ull + b.nanosecond;
  return x < y ? -1 : x > y;
}

static int datetime_cmp (hegel_datetime a, hegel_datetime b)
{
  int c = date_cmp (a.date, b.date);
  return c ? c : time_cmp (a.time, b.time);
}

/* ---- Layer 2: properties ---- */

#define ASSERT_DATE(d, what)                                                            \
  HEGEL_ASSERT (date_is_valid (d), "%s %d-%u-%u is not a date", what, (int) (d).year, \
                (d).month, (d).day)

static void prop_dates (hegel_testcase * tc)
{
  /* Default bounds: 0001-01-01 .. 9999-12-31. */
  hegel_date d = hegel_draw_date (tc, NULL, NULL);
  ASSERT_DATE (d, "default-bounded");
  HEGEL_ASSERT (d.year >= 1 && d.year <= 9999, "default year %d outside 1..9999", (int) d.year);
  hegel_date back = days_to_date (date_to_days (d));
  HEGEL_ASSERT (date_cmp (back, d) == 0 && back.year == d.year && back.month == d.month
                && back.day == d.day, "day-number round trip lost %d-%u-%u", (int) d.year,
                d.month, d.day);

  /* Drawn bounds: the value stays in [lo, hi]. */
  hegel_date a = hegel_draw_date (tc, NULL, NULL);
  hegel_date b = hegel_draw_date (tc, NULL, NULL);
  hegel_date lo = date_cmp (a, b) <= 0 ? a : b, hi = date_cmp (a, b) <= 0 ? b : a;
  hegel_date in = hegel_draw_date (tc, &lo, &hi);
  ASSERT_DATE (in, "bounded");
  HEGEL_ASSERT (date_cmp (lo, in) <= 0 && date_cmp (in, hi) <= 0, "date outside its bounds");

  /* One bound given, the other the default. */
  hegel_date from = hegel_draw_date (tc, &lo, NULL);
  HEGEL_ASSERT (date_cmp (lo, from) <= 0 && from.year <= 9999, "half-bounded date out of range");

  /* The engine's whole year range, BCE included. */
  static const hegel_date wide_lo = { -999999, 1, 1 }, wide_hi = { 999999, 12, 31 };
  hegel_date w = hegel_draw_date (tc, &wide_lo, &wide_hi);
  ASSERT_DATE (w, "wide-range");
  hegel_date wback = days_to_date (date_to_days (w));
  HEGEL_ASSERT (date_cmp (wback, w) == 0, "wide round trip lost year %d", (int) w.year);
}

static void prop_times (hegel_testcase * tc)
{
  hegel_time t = hegel_draw_time (tc, NULL, NULL);
  HEGEL_ASSERT (time_is_valid (t), "invalid time %u:%u:%u.%u", t.hour, t.minute, t.second,
                (unsigned) t.nanosecond);

  hegel_time a = hegel_draw_time (tc, NULL, NULL);
  hegel_time b = hegel_draw_time (tc, NULL, NULL);
  hegel_time lo = time_cmp (a, b) <= 0 ? a : b, hi = time_cmp (a, b) <= 0 ? b : a;
  hegel_time in = hegel_draw_time (tc, &lo, &hi);
  HEGEL_ASSERT (time_is_valid (in), "invalid bounded time");
  HEGEL_ASSERT (time_cmp (lo, in) <= 0 && time_cmp (in, hi) <= 0, "time outside its bounds");

  /* A degenerate range has one value. */
  hegel_time one = hegel_draw_time (tc, &in, &in);
  HEGEL_ASSERT (time_cmp (one, in) == 0, "single-value time range gave another time");
}

static void prop_datetimes (hegel_testcase * tc)
{
  hegel_datetime dt = hegel_draw_datetime (tc, NULL, NULL);
  ASSERT_DATE (dt.date, "datetime");
  HEGEL_ASSERT (time_is_valid (dt.time), "datetime with invalid time");

  hegel_datetime a = hegel_draw_datetime (tc, NULL, NULL);
  hegel_datetime b = hegel_draw_datetime (tc, NULL, NULL);
  hegel_datetime lo = datetime_cmp (a, b) <= 0 ? a : b, hi = datetime_cmp (a, b) <= 0 ? b : a;
  hegel_datetime in = hegel_draw_datetime (tc, &lo, &hi);
  ASSERT_DATE (in.date, "bounded datetime");
  HEGEL_ASSERT (time_is_valid (in.time), "bounded datetime with invalid time");
  HEGEL_ASSERT (datetime_cmp (lo, in) <= 0 && datetime_cmp (in, hi) <= 0,
                "datetime outside its bounds");
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int main (void)
{
  hegel_run_options o = { .test_cases = 300 };
  int failed = 0;
  failed += hegel_run_test_with (prop_dates, &o);
  failed += hegel_run_test_nofork_with (prop_dates, &o);
  failed += hegel_run_test_with (prop_times, &o);
  failed += hegel_run_test_nofork_with (prop_times, &o);
  failed += hegel_run_test_with (prop_datetimes, &o);
  failed += hegel_run_test_nofork_with (prop_datetimes, &o);
  printf ("%d of 6 runs failed\n", failed);
  return failed != 0;
}
