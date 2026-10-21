/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_run_options and the engine settings around them.
**
** Layer 1: two small functions under test — is_multiple_of_97() (a
**          predicate the test filters on) and below_limit() (the
**          property, false from 1000 up).
** Layer 2: nofork runs with options, each observed from the body:
**   - test_cases:   exactly N cases run;
**   - HEGEL_TEST_CASES: the plain runner honours it (the runner no
**                   longer overrides the engine's env settings);
**   - seed:         same seed, same run; another seed, another run;
**   - suppress:     a filter rejecting ~96/97 of draws fails the health
**                   check, and passes with HEGEL_SUPPRESS_FILTER_TOO_MUCH;
**   - skip shrink:  with HEGEL_SKIP_SHRINK the reported failure is the
**                   first one found, without it the minimal one (1000);
**   - database:     with HEGEL_DATABASE set, a failure found once is
**                   replayed first by the next invocation of the binary
**                   (the test re-executes itself for this one).
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "hegel_c.h"

/* ---- Layer 1: functions under test ---- */

static
int
is_multiple_of_97 (
int                 x)
{
  return (x % 97 == 0);
}

static
int
below_limit (
int                 x)
{
  return (x < 1000);
}

/* ---- Layer 2: observed bodies ---- */

static int            n_calls;
static uint64_t       fingerprint;
static int            last_failing;
static int            first_fail_call;    /* call number of the first failure */

static
void
test_count (
hegel_testcase *    tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 1000000);
  n_calls ++;
  fingerprint = fingerprint * 1000003u + (uint64_t) x;
}

static
void
test_filtered (
hegel_testcase *    tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 1000000);
  hegel_assume (tc, is_multiple_of_97 (x));
}

static
void
test_limit (
hegel_testcase *    tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 1000000);
  n_calls ++;
  if (!below_limit (x)) {
    last_failing = x;
    if (first_fail_call == 0)
      first_fail_call = n_calls;
  }
  HEGEL_ASSERT (below_limit (x), "x=%d", x);
}

/* ---- Layer 3: runner ---- */

static int            failures;

static
void
check (
int                 ok,
const char *        what)
{
  printf ("  %-58s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok)
    failures ++;
}

static
uint64_t
run_fingerprint (
const hegel_run_options *   o)
{
  fingerprint = 0;
  n_calls = 0;
  hegel_run_test_nofork_with (test_count, o);
  return (fingerprint);
}

static
int
dir_has_entries (
const char *        path)
{
  DIR *               d;
  struct dirent *     ent;
  int                 n = 0;

  d = opendir (path);
  if (d == NULL)
    return (0);
  while ((ent = readdir (d)) != NULL)
    if (strcmp (ent->d_name, ".") != 0 && strcmp (ent->d_name, "..") != 0)
      n ++;
  closedir (d);
  return (n > 0);
}

/* db mode: one seeded run of test_limit; exit status = the call number
** of its first failure (0 if none). */
static
int
db_mode (
const char *        seed)
{
  hegel_run_options o = { .has_seed = 1, .seed = strtoull (seed, NULL, 10) };

  n_calls = 0;
  first_fail_call = 0;
  hegel_run_test_nofork_with (test_limit, &o);
  return (first_fail_call);
}

static
int
db_child (
int                 seed)
{
  char                arg[16];
  pid_t               pid;
  int                 status;

  snprintf (arg, sizeof (arg), "%d", seed);
  fflush (stdout);
  pid = fork ();
  if (pid == 0) {
    int devnull = open ("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2 (devnull, 1);
      dup2 (devnull, 2);
    }
    execl ("/proc/self/exe", "test_run_options", "db", arg, (char *) NULL);
    _exit (127);
  }
  if (pid < 0 || waitpid (pid, &status, 0) < 0 || !WIFEXITED (status))
    return (-1);
  return (WEXITSTATUS (status));
}

int
main (
int                 argc,
char *              argv[])
{
  char                db[256];
  char                cmd[300];
  int                 rc;

  if (argc == 3 && strcmp (argv[1], "db") == 0)
    return (db_mode (argv[2]));

  printf ("Run options...\n");

  {
    hegel_run_options o = { .test_cases = 37 };
    n_calls = 0;
    hegel_run_test_nofork_with (test_count, &o);
    check (n_calls == 37, "test_cases = 37 runs 37 cases");
  }

  {
    setenv ("HEGEL_TEST_CASES", "23", 1);
    n_calls = 0;
    hegel_run_test_nofork (test_count);
    unsetenv ("HEGEL_TEST_CASES");
    check (n_calls == 23, "HEGEL_TEST_CASES=23 honoured by hegel_run_test_nofork");
  }

  {
    hegel_run_options a = { .test_cases = 20, .has_seed = 1, .seed = 7 };
    hegel_run_options b = { .test_cases = 20, .has_seed = 1, .seed = 99 };
    uint64_t f1 = run_fingerprint (&a);
    uint64_t f2 = run_fingerprint (&a);
    uint64_t f3 = run_fingerprint (&b);
    check (f1 == f2, "same seed: same run");
    check (f1 != f3, "another seed: another run");
  }

  {
    hegel_run_options plain = { .has_seed = 1, .seed = 1 };
    hegel_run_options quiet = { .has_seed = 1, .seed = 1,
                                .suppress_health = HEGEL_SUPPRESS_FILTER_TOO_MUCH };
    check (hegel_run_test_nofork_with (test_filtered, &plain) != 0,
           "heavy filter fails the health check");
    check (hegel_run_test_nofork_with (test_filtered, &quiet) == 0,
           "... and passes with HEGEL_SUPPRESS_FILTER_TOO_MUCH");
  }

  {
    hegel_run_options shrink = { .has_seed = 1, .seed = 1 };
    hegel_run_options raw = { .has_seed = 1, .seed = 1, .skip_phases = HEGEL_SKIP_SHRINK };
    hegel_run_test_nofork_with (test_limit, &shrink);
    check (last_failing == 1000, "default: failure shrunk to 1000");
    hegel_run_test_nofork_with (test_limit, &raw);
    check (last_failing > 1000, "HEGEL_SKIP_SHRINK: failure reported unshrunk");
  }

  {
    /* The database key is "<binary>#<run number>", so replay happens
    ** across invocations of the binary: run it twice (db mode below). */
    snprintf (db, sizeof (db), "/tmp/hegel_run_options_db_%d", (int) getpid ());
    setenv ("HEGEL_DATABASE", db, 1);
    rc = db_child (3);
    check (rc > 1 && dir_has_entries (db), "HEGEL_DATABASE: a failure is found and stored");
    /* Another seed.  Without the database the first case is the
    ** simplest one (0, passing); with it the stored failure is replayed
    ** first, so the very first call fails. */
    rc = db_child (4);
    check (rc == 1, "HEGEL_DATABASE: the next invocation replays it first");
    unsetenv ("HEGEL_DATABASE");
    snprintf (cmd, sizeof (cmd), "rm -rf %s", db);
    rc = system (cmd);
    (void) rc;
  }

  if (failures) {
    fprintf (stderr, "%d check(s) failed\n", failures);
    return (1);
  }
  return (0);
}
