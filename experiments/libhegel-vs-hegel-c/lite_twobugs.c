/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 5, lite libhegel: midpoint (lo, hi) over the whole int range; bug
** A (lo + hi wraps: result outside [lo, hi]) and bug B (C division rounds
** toward zero).  Two checks need two origins, or the engine sees one bug
** and shrinks toward whichever it meets: the origin is the only thing
** that tells failures apart.  Lite prints one counterexample (the last
** stamped failing case), which is right by default (one failure per
** run) but shows only one of the two under
** HEGEL_REPORT_MULTIPLE_FAILURES=1.  See twobugs.md.
*/
#include <stdio.h>
#include <limits.h>
#include "hegel.h"
#include "common.h"
#include "sut.h"

/* Static: the engine reads the origin at mark_complete, after prop returns. */
static const hegel_string_t BUG_A = { "midpoint in [lo, hi]", 20 };
static const hegel_string_t BUG_B = { "midpoint is floor ((lo + hi) / 2)", 33 };

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, char * shown,
                      const hegel_string_t ** origin)
{
  count_exec ();
  int64_t lo, hi;
  if (hegel_generate_integer (ctx, tc, INT_MIN, INT_MAX, &lo)
      || hegel_generate_integer (ctx, tc, INT_MIN, INT_MAX, &hi)) return HEGEL_STATUS_OVERRUN;
  if (lo > hi) { int64_t t = lo; lo = hi; hi = t; }
  int m = midpoint ((int) lo, (int) hi);
  snprintf (shown, 128, "lo = %lld, hi = %lld, midpoint = %d", (long long) lo, (long long) hi, m);
  if (m < lo || m > hi) *origin = &BUG_A;
  else if (m != floor_half (lo + hi)) *origin = &BUG_B;
  return *origin ? HEGEL_STATUS_INTERESTING : HEGEL_STATUS_VALID;
}

/* B{ */
int main (void)
{
  hegel_context_t * ctx = hegel_context_new ();
  hegel_settings_t * s;
  hegel_run_t * run;
  hegel_test_case_t * tc;
  char last[128] = "";
  hegel_settings_new (ctx, &s);
  hegel_settings_set_database (ctx, s, &(hegel_string_t) { "", 0 });
  hegel_run_start (ctx, s, NULL, NULL, &run);
  while (hegel_next_test_case (ctx, run, &tc) == HEGEL_OK && tc) {
    bool capture = false;
    char shown[128];
    const hegel_string_t * origin = NULL;
    hegel_test_case_should_capture (ctx, tc, &capture);
    uint32_t st = prop (ctx, tc, shown, &origin);
    if (capture && st == HEGEL_STATUS_INTERESTING) snprintf (last, sizeof last, "%s", shown);
    hegel_mark_complete (ctx, tc, st, origin);
    hegel_test_case_free (ctx, tc);
  }
  hegel_run_result_t * r;
  hegel_run_status_t status;
  hegel_run_result (ctx, run, &r);
  hegel_run_result_status (ctx, r, &status);
  if (status == HEGEL_RUN_STATUS_FAILED) printf ("FAILED: %s\n", last);
  if (status == HEGEL_RUN_STATUS_ERROR) {
    const hegel_string_t * err;
    hegel_run_result_error (ctx, r, &err);
    printf ("run error: %.*s\n", (int) err->len, err->data);
  }
  return status == HEGEL_RUN_STATUS_PASSED ? 0 : 1;
}
/* }B */
