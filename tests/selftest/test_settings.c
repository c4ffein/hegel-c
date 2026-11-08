/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: the engine settings in hegel_run_options — derandomize,
** stop_on_first_failure, show_statistics, print_blob, the settings
** string, the test location — and that leaving them at zero keeps the
** runner's defaults.
**
** Layer 1: two small functions under test — mix() (folds a drawn value
**          into a run fingerprint) and below_half() (the property, false
**          from 500000 up: about half of all draws fail it).
** Layer 2: bodies that count their cases and fingerprint their draws in
**          memory shared with the fork children, so every check runs in
**          fork AND nofork mode:
**   - defaults:     all-zero options run 100 cases, print no statistics,
**                   end a failure with a reproduce line;
**   - derandomize:  two runs see the same cases (and without it, with no
**                   seed, they don't);
**   - stop_on_first_failure OFF: generation goes on past the first
**                   failure (more cases than the budget), ON / default
**                   stops there;
**   - show_statistics: the engine's statistics block appears on stderr
**                   (also from HEGEL_STATISTICS=1), not by default;
**   - print_blob OFF: no reproduce line (also from HEGEL_PRINT_BLOB=false);
**   - settings:     "key=value" pairs set test_cases, win over
**                   HEGEL_TEST_CASES, lose to the typed field; a bad key
**                   fails the run;
**   - test location: under ANTITHESIS_OUTPUT_DIR the engine reports the
**                   run as "<binary>::<function> passes properties" at
**                   the file:line HEGEL_TEST_LOCATION recorded.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "hegel_c.h"

/* ---- Layer 1: functions under test ---- */

static
uint64_t
mix (
uint64_t            acc,
int                 x)
{
  return (acc * 1000003u + (uint64_t) x);
}

static
int
below_half (
int                 x)
{
  return (x < 500000);
}

/* ---- Layer 2: observed bodies ---- */

typedef struct {
  int                 n_calls;
  uint64_t            fingerprint;
} observed;

static observed *     obs;              /* MAP_SHARED: fork children write it */

static
void
test_count (
hegel_testcase *    tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 1000000);
  obs->n_calls ++;
  obs->fingerprint = mix (obs->fingerprint, x);
}

static
void
test_half (
hegel_testcase *    tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 1000000);
  obs->n_calls ++;
  HEGEL_ASSERT (below_half (x), "x=%d", x);
}

/* ---- Layer 3: runner ---- */

static int            failures;

static
void
check (
int                 ok,
const char *        mode,
const char *        what)
{
  printf ("  [%-6s] %-58s %s\n", mode, what, ok ? "ok" : "FAILED");
  if (!ok)
    failures ++;
}

typedef struct {
  int                 rc;
  int                 n_calls;
  uint64_t            fingerprint;
  int                 statistics;       /* lines starting "Statistics" */
  int                 reproduce;        /* "reproduce:" lines */
  int                 settings_error;   /* "hegel-c: settings:" lines */
} run_out;

/* One run with stderr captured. */
static
run_out
run (
void                (*fn) (hegel_testcase *),
const hegel_run_options *   o,
int                 use_fork)
{
  run_out             r;
  FILE *              tmp = tmpfile ();
  int                 saved = dup (2);
  char                line[2048];

  memset (&r, 0, sizeof (r));
  obs->n_calls = 0;
  obs->fingerprint = 0;
  fflush (stdout);
  fflush (stderr);
  dup2 (fileno (tmp), 2);
  r.rc = use_fork ? hegel_run_test_with (fn, o) : hegel_run_test_nofork_with (fn, o);
  fflush (stdout);
  fflush (stderr);
  dup2 (saved, 2);
  close (saved);
  r.n_calls = obs->n_calls;
  r.fingerprint = obs->fingerprint;
  rewind (tmp);
  while (fgets (line, sizeof (line), tmp) != NULL) {
    if (strncmp (line, "Statistics", 10) == 0)
      r.statistics ++;
    if (strncmp (line, "reproduce:", 10) == 0)
      r.reproduce ++;
    if (strncmp (line, "hegel-c: settings:", 18) == 0)
      r.settings_error ++;
  }
  fclose (tmp);
  return (r);
}

static
void
check_mode (
int                 use_fork)
{
  const char *        m = use_fork ? "fork" : "nofork";
  run_out             a, b;

  {
    hegel_run_options zero;
    memset (&zero, 0, sizeof (zero));
    a = run (test_count, &zero, use_fork);
    check (a.rc == 0 && a.n_calls == 100, m, "defaults: 100 cases");
    check (a.statistics == 0, m, "defaults: no statistics block");
    hegel_run_options seeded = { .has_seed = 1, .seed = 5 };
    a = run (test_half, &seeded, use_fork);
    check (a.rc == 1 && a.reproduce == 1, m, "defaults: a failure ends with a reproduce line");
  }

  {
    hegel_run_options d = { .test_cases = 30, .derandomize = HEGEL_ON };
    hegel_run_options r = { .test_cases = 30, .derandomize = HEGEL_OFF };
    a = run (test_count, &d, use_fork);
    b = run (test_count, &d, use_fork);
    check (a.n_calls == 30 && a.fingerprint == b.fingerprint, m,
           "derandomize ON: two runs, the same cases");
    a = run (test_count, &r, use_fork);
    b = run (test_count, &r, use_fork);
    check (a.fingerprint != b.fingerprint, m, "derandomize OFF: two runs differ");
  }

  {
    hegel_run_options dflt = { .test_cases = 200, .has_seed = 1, .seed = 3 };
    hegel_run_options on = { .test_cases = 200, .has_seed = 1, .seed = 3,
                             .stop_on_first_failure = HEGEL_ON };
    hegel_run_options off = { .test_cases = 200, .has_seed = 1, .seed = 3,
                              .stop_on_first_failure = HEGEL_OFF };
    run_out             c;
    a = run (test_half, &dflt, use_fork);
    b = run (test_half, &on, use_fork);
    c = run (test_half, &off, use_fork);
    check (a.rc == 1 && b.rc == 1 && c.rc == 1, m, "stop_on_first_failure: every run fails");
    check (a.n_calls == b.n_calls, m, "stop_on_first_failure: ON is the default");
    check (c.n_calls > 200 && c.n_calls > b.n_calls, m,
           "stop_on_first_failure OFF: the whole budget, then shrinking");
  }

  {
    hegel_run_options st = { .test_cases = 10, .show_statistics = HEGEL_ON };
    a = run (test_count, &st, use_fork);
    check (a.rc == 0 && a.statistics == 1, m, "show_statistics ON: the block is printed");
    setenv ("HEGEL_STATISTICS", "1", 1);
    hegel_run_options plain = { .test_cases = 10 };
    a = run (test_count, &plain, use_fork);
    hegel_run_options off = { .test_cases = 10, .show_statistics = HEGEL_OFF };
    b = run (test_count, &off, use_fork);
    unsetenv ("HEGEL_STATISTICS");
    check (a.statistics == 1, m, "HEGEL_STATISTICS=1: the block is printed");
    check (b.statistics == 0, m, "show_statistics OFF wins over HEGEL_STATISTICS");
  }

  {
    hegel_run_options nob = { .has_seed = 1, .seed = 5, .print_blob = HEGEL_OFF };
    a = run (test_half, &nob, use_fork);
    check (a.rc == 1 && a.reproduce == 0, m, "print_blob OFF: no reproduce line");
    setenv ("HEGEL_PRINT_BLOB", "false", 1);
    hegel_run_options seeded = { .has_seed = 1, .seed = 5 };
    a = run (test_half, &seeded, use_fork);
    unsetenv ("HEGEL_PRINT_BLOB");
    check (a.rc == 1 && a.reproduce == 0, m, "HEGEL_PRINT_BLOB=false: no reproduce line");
  }

  {
    hegel_run_options s = { .settings = "test_cases=13; derandomize=true  phases=generate,shrink" };
    a = run (test_count, &s, use_fork);
    b = run (test_count, &s, use_fork);
    check (a.rc == 0 && a.n_calls == 13 && a.fingerprint == b.fingerprint, m,
           "settings string: test_cases=13, derandomize=true");
    setenv ("HEGEL_TEST_CASES", "21", 1);
    hegel_run_options s2 = { .settings = "test_cases=17" };
    a = run (test_count, &s2, use_fork);
    unsetenv ("HEGEL_TEST_CASES");
    check (a.n_calls == 17, m, "settings string wins over HEGEL_TEST_CASES");
    hegel_run_options s3 = { .settings = "test_cases=17", .test_cases = 9 };
    a = run (test_count, &s3, use_fork);
    check (a.n_calls == 9, m, "the typed field wins over the settings string");
    hegel_run_options bad = { .settings = "test_cases=5 bogus=1" };
    a = run (test_count, &bad, use_fork);
    check (a.rc == 1 && a.n_calls == 0 && a.settings_error == 1, m,
           "settings string: an unknown key fails the run");
    hegel_run_options bad2 = { .settings = "test_cases" };
    a = run (test_count, &bad2, use_fork);
    check (a.rc == 1 && a.settings_error == 1, m, "settings string: no '=' fails the run");
  }
}

/* The test location reaches the engine: under ANTITHESIS_OUTPUT_DIR it
** writes one SDK assertion per run, named and located by it. */
static
void
check_location (void)
{
  char                dir[256];
  char                path[300];
  char                want[400];
  char                line[4096];
  int                 named = 0, located = 0, numbered = 0;
  FILE *              f;
  int                 rc;

  snprintf (dir, sizeof (dir), "/tmp/hegel_settings_antithesis_%d", (int) getpid ());
  snprintf (path, sizeof (path), "%s/sdk.jsonl", dir);
  if (mkdir (dir, 0700) != 0) {
    check (0, "nofork", "test location: mkdir");
    return;
  }
  setenv ("ANTITHESIS_OUTPUT_DIR", dir, 1);
  {
    hegel_run_options o = { HEGEL_TEST_LOCATION (test_count), .test_cases = 5 };
    int line_no = __LINE__ - 1;
    run (test_count, &o, 0);
    hegel_run_options plain = { .test_cases = 5 };
    run (test_count, &plain, 0);
    snprintf (want, sizeof (want), "\"begin_line\":%d", line_no);
  }
  unsetenv ("ANTITHESIS_OUTPUT_DIR");
  f = fopen (path, "r");
  if (f != NULL) {
    while (fgets (line, sizeof (line), f) != NULL) {
      if (strstr (line, "\"test_settings::test_count passes properties\"") != NULL)
        named ++;
      if (strstr (line, "test_settings.c") != NULL && strstr (line, want) != NULL)
        located ++;
      if (strstr (line, "::run") != NULL)
        numbered ++;
    }
    fclose (f);
  }
  check (named > 0, "nofork", "test location: reported as <binary>::<function>");
  check (located > 0, "nofork", "test location: at HEGEL_TEST_LOCATION's file:line");
  check (numbered > 0, "nofork", "test location: unset, reported as <binary>::run<N>");
  snprintf (line, sizeof (line), "rm -rf %s", dir);
  rc = system (line);
  (void) rc;
}

int
main (void)
{
  obs = mmap (NULL, sizeof (*obs), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (obs == MAP_FAILED) {
    perror ("mmap");
    return (1);
  }
  /* The checks below assume the engine's own defaults. */
  unsetenv ("HEGEL_TEST_CASES");
  unsetenv ("HEGEL_STATISTICS");
  unsetenv ("HEGEL_PRINT_BLOB");
  unsetenv ("HEGEL_DERANDOMIZE");
  unsetenv ("HEGEL_STOP_ON_FIRST_FAILURE");
  unsetenv ("HEGEL_REPORT_MULTIPLE_FAILURES");   /* it also keeps a run going after a failure */
  unsetenv ("HEGEL_SEED");

  printf ("Engine settings...\n");
  check_mode (0);
  check_mode (1);
  check_location ();

  if (failures) {
    fprintf (stderr, "%d check(s) failed\n", failures);
    return (1);
  }
  return (0);
}
