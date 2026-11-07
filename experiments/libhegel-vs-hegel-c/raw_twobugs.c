/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 5, libhegel only: two checks on midpoint (lo, hi), lo and hi
** over the whole int range (swapped into order): A, the result lies in
** [lo, hi] (fails when lo + hi wraps); B, it is floor ((lo + hi) / 2)
** (fails for a negative odd sum).  Each check passes its own origin, as
** hegel.h asks; report_multiple_failures is left at the engine default.
** Lines between B{ and }B markers are boilerplate.
*/
#include "raw_check.h"
#include "common.h"
#include "sut.h"

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, bool show,
                      const char ** origin)
{
  count_exec ();
  int64_t lo, hi;
  /* B{ */
  if (hegel_generate_integer (ctx, tc, INT32_MIN, INT32_MAX, &lo) != HEGEL_OK
      || hegel_generate_integer (ctx, tc, INT32_MIN, INT32_MAX, &hi) != HEGEL_OK)
    return HEGEL_STATUS_OVERRUN;
  /* }B */
  if (lo > hi) { int64_t t = lo; lo = hi; hi = t; }
  int m = midpoint ((int) lo, (int) hi);
  if (show) printf ("lo = %lld, hi = %lld, midpoint = %d\n", (long long) lo, (long long) hi, m);
  if (m < lo || m > hi) {
    *origin = "midpoint in [lo, hi]";
    return HEGEL_STATUS_INTERESTING;
  }
  if (m != floor_half (lo + hi)) {
    *origin = "midpoint is floor ((lo + hi) / 2)";
    return HEGEL_STATUS_INTERESTING;
  }
  return HEGEL_STATUS_VALID;
}

/* B{ */
int main (void)
{
  hegel_context_t * ctx = hegel_context_new ();
  hegel_settings_t * s;
  CHECK (hegel_settings_new, ctx, &s);
  CHECK (hegel_settings_set_test_cases, ctx, s, 100);
  CHECK (hegel_settings_set_database, ctx, s, &STR (""));

  hegel_run_t * run;
  CHECK (hegel_run_start, ctx, s, NULL, NULL, &run);
  for (;;) {
    hegel_test_case_t * tc;
    CHECK (hegel_next_test_case, ctx, run, &tc);
    if (!tc) break;
    const char * origin = NULL;
    uint32_t st = prop (ctx, tc, false, &origin);
    CHECK (hegel_mark_complete, ctx, tc, st, origin ? &STR (origin) : NULL);
    CHECK (hegel_test_case_free, ctx, tc);
  }

  hegel_run_result_t * r;
  hegel_run_status_t status;
  size_t nf;
  CHECK (hegel_run_result, ctx, run, &r);
  CHECK (hegel_run_result_status, ctx, r, &status);
  if (status == HEGEL_RUN_STATUS_ERROR) {
    const hegel_string_t * err;
    CHECK (hegel_run_result_error, ctx, r, &err);
    fprintf (stderr, "run error: %.*s\n", (int) err->len, err->data);
  }
  CHECK (hegel_run_result_failure_count, ctx, r, &nf);
  for (size_t i = 0; i < nf; i++) {
    hegel_failure_t * f;
    hegel_string_t origin;
    const hegel_string_t * blob;
    CHECK (hegel_run_result_failure, ctx, r, i, &f);
    CHECK (hegel_failure_origin, ctx, f, &origin);
    CHECK (hegel_failure_reproduction_blob, ctx, f, &blob);
    printf ("FAILED: %.*s\n", (int) origin.len, origin.data);
    if (blob) {
      hegel_test_case_t * tc;
      const char * o = NULL;
      CHECK (hegel_test_case_from_blob, ctx, s, *blob, NULL, NULL, &tc);
      uint32_t st = prop (ctx, tc, true, &o);
      CHECK (hegel_mark_complete, ctx, tc, st, o ? &STR (o) : NULL);
      CHECK (hegel_test_case_free, ctx, tc);
      printf ("blob: %.*s\n", (int) blob->len, blob->data);
    }
    CHECK (hegel_failure_free, ctx, f);
  }
  CHECK (hegel_run_result_free, ctx, r);
  CHECK (hegel_run_free, ctx, run);
  CHECK (hegel_settings_free, ctx, s);
  CHECK (hegel_context_free, ctx);
  return status == HEGEL_RUN_STATUS_PASSED ? 0 : 1;
}
/* }B */
