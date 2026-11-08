/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 6, lite libhegel: a bug in the TEST: len in [0, 10], then idx in
** [0, len - 1], an inverted range when len = 0.  The property holds.
** With the usual "any non-OK draw ends the case as an overrun" (upstream's
** examples treat STOP_TEST that way), the engine's HEGEL_E_INVALID_ARG
** becomes an overrun, and the run ends in an error that blames the input
** size.  Lite prints that error, so at least the run doesn't fail
** silently; the actual cause is in hegel_context_last_error, which lite
** never reads.  See misuse.md.
*/
#include <stdio.h>
#include "hegel.h"
#include "common.h"
#include "sut.h"

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, char * shown)
{
  count_exec ();
  int64_t len, idx;
  if (hegel_generate_integer (ctx, tc, 0, 10, &len)
      || hegel_generate_integer (ctx, tc, 0, len - 1, &idx)) return HEGEL_STATUS_OVERRUN;
  snprintf (shown, 64, "len = %lld, idx = %lld", (long long) len, (long long) idx);
  return idx < len ? HEGEL_STATUS_VALID : HEGEL_STATUS_INTERESTING;
}

/* B{ */
int main (void)
{
  hegel_context_t * ctx = hegel_context_new ();
  hegel_settings_t * s;
  hegel_run_t * run;
  hegel_test_case_t * tc;
  char last[64] = "";
  hegel_settings_new (ctx, &s);
  hegel_settings_set_database (ctx, s, &(hegel_string_t) { "", 0 });
  hegel_run_start (ctx, s, NULL, NULL, &run);
  while (hegel_next_test_case (ctx, run, &tc) == HEGEL_OK && tc) {
    bool capture = false;
    char shown[64];
    hegel_test_case_should_capture (ctx, tc, &capture);
    uint32_t st = prop (ctx, tc, shown);
    if (capture && st == HEGEL_STATUS_INTERESTING) snprintf (last, sizeof last, "%s", shown);
    hegel_mark_complete (ctx, tc, st, NULL);
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
