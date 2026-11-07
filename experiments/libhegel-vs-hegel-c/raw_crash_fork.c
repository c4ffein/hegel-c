/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 2, libhegel only, with hand-rolled crash isolation: the draws
** happen in the engine's process (they must: a forked child's draws
** would go to a copy of the engine), then the code under test runs in a
** fork()ed child; a child killed by a signal is an INTERESTING case with
** an origin naming the signal, so the engine shrinks it.  The same
** design as hegel-c's HEGEL_ISOLATED, minus its pipe for notes and
** asserts from inside the child.
** Lines between B{ and }B markers are boilerplate.
*/
#include <signal.h>
#include <sys/wait.h>

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
  if (show) printf ("id = %lld\n", (long long) n);
  /* B{ */
  fflush (stdout);
  pid_t pid = fork ();
  if (pid == 0)
  /* }B */
    _exit (name_len ((int) n) > 0 ? 0 : 1);
  /* B{ */
  int ws;
  waitpid (pid, &ws, 0);
  if (WIFSIGNALED (ws)) {
    static char sig[64];
    snprintf (sig, sizeof sig, "crash: %s", strsignal (WTERMSIG (ws)));
    if (show) printf ("%s\n", sig);
    *origin = sig;
    return HEGEL_STATUS_INTERESTING;
  }
  /* }B */
  if (WEXITSTATUS (ws) != 0) {
    *origin = "name_len (id) > 0";
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
