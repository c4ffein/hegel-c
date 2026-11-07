/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 7, libhegel only: raw_minimal.c plus replay of a printed blob
** (REPRO=<blob> in the environment runs hegel_run_start_blob instead of
** exploring), the counterpart of hegel-c's HEGEL_C_REPRODUCE.
** Lines between B{ and }B markers are boilerplate.
*/
#include "raw_check.h"
#include "common.h"
#include "sut.h"

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, bool show,
                      const char ** origin)
{
  count_exec ();
  int64_t n;
  /* B{ */
  if (hegel_generate_integer (ctx, tc, 0, 100, &n) != HEGEL_OK)
    return HEGEL_STATUS_OVERRUN;
  /* }B */
  if (show) printf ("n = %lld\n", (long long) n);
  if (!buffer_fits ((int) n)) {
    *origin = "buffer_fits (n)";
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
  const char * repro = getenv ("REPRO");
  if (repro)
    CHECK (hegel_run_start_blob, ctx, s, STR (repro), NULL, NULL, &run);
  else
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
    hegel_string_t given = repro ? STR (repro) : (hegel_string_t) { 0 };
    if (!blob && repro) blob = &given;       /* a replayed failure carries no blob */
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
  if (repro && status == HEGEL_RUN_STATUS_PASSED) printf ("blob did not reproduce\n");
  return status == HEGEL_RUN_STATUS_PASSED ? 0 : 1;
}
/* }B */
