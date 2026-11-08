/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 2, lite libhegel: name_len (id) > 0 for id in [0, 100]; name_len
** dereferences NULL for id >= 42.  The shortest correct program has to
** fork itself: without that the first crashing input kills the run, so
** nothing is shrunk or reported (raw_crash.c shows it).  The draw happens
** in the engine's process, the call under test in a child; a signal or a
** non-zero exit is a failure.  See crash.md.
*/
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include "hegel.h"
#include "common.h"
#include "sut.h"

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, char * shown)
{
  count_exec ();
  int64_t id;
  int ws;
  if (hegel_generate_integer (ctx, tc, 0, 100, &id)) return HEGEL_STATUS_OVERRUN;
  snprintf (shown, 64, "id = %lld", (long long) id);
  pid_t pid = fork ();
  if (pid == 0) _exit (name_len ((int) id) > 0 ? 0 : 1);
  waitpid (pid, &ws, 0);
  return WIFEXITED (ws) && WEXITSTATUS (ws) == 0 ? HEGEL_STATUS_VALID : HEGEL_STATUS_INTERESTING;
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
