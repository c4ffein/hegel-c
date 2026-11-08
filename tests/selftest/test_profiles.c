/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: engine settings profiles from C — hegel_register_profile,
** hegel_run_options.profile, hegel_set_default_profile — and where they
** sit in the precedence (profile < HEGEL_* env < settings string < typed
** field).
**
** Layer 1: mix() folds a drawn value into a run fingerprint.
** Layer 2: a body counting its cases and fingerprinting its draws in
**          memory shared with the fork children; each check is one run,
**          in nofork and fork mode:
**   - a registered profile (test_cases = 7) applies when named;
**   - a profile registered over another (`ci`: derandomized) keeps it;
**   - HEGEL_TEST_CASES wins over the profile, the typed field over both;
**   - hegel_set_default_profile makes the plain runners use it, and
**     clearing it brings the engine default (100) back;
**   - an unknown profile fails the run; a reserved or malformed name is
**     refused by hegel_register_profile; a bad settings string too.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
uint64_t
mix (
uint64_t            acc,
int                 x)
{
  return (acc * 1000003u + (uint64_t) x);
}

/* ---- Layer 2: observed body ---- */

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

/* One run (NULL options: the plain runner); returns its exit code and
** leaves the counts in *obs.  stderr is silenced. */
static
int
run (
const hegel_run_options *   o,
int                 use_fork)
{
  int                 saved = dup (2);
  FILE *              null = fopen ("/dev/null", "w");
  int                 rc;

  obs->n_calls = 0;
  obs->fingerprint = 0;
  fflush (stderr);
  if (null != NULL)
    dup2 (fileno (null), 2);
  if (o == NULL && use_fork)
    rc = hegel_run_test_result (test_count);
  else                                  /* NULL options: the defaults */
    rc = use_fork ? hegel_run_test_with (test_count, o) : hegel_run_test_nofork_with (test_count, o);
  fflush (stderr);
  dup2 (saved, 2);
  close (saved);
  if (null != NULL)
    fclose (null);
  return (rc);
}

static
void
check_mode (
int                 use_fork)
{
  const char *        m = use_fork ? "fork" : "nofork";
  int                 rc;
  uint64_t            f1;

  {
    hegel_run_options o = { .profile = "seven" };
    rc = run (&o, use_fork);
    check (rc == 0 && obs->n_calls == 7, m, "profile \"seven\": 7 cases");
  }

  {
    hegel_run_options o = { .profile = "ci_eleven" };
    run (&o, use_fork);
    f1 = obs->fingerprint;
    rc = run (&o, use_fork);
    check (rc == 0 && obs->n_calls == 11 && obs->fingerprint == f1, m,
           "profile over `ci`: 11 cases, derandomized");
  }

  {
    hegel_run_options o = { .profile = "seven" };
    setenv ("HEGEL_TEST_CASES", "15", 1);
    run (&o, use_fork);
    check (obs->n_calls == 15, m, "HEGEL_TEST_CASES wins over the profile");
    hegel_run_options t = { .profile = "seven", .test_cases = 4 };
    run (&t, use_fork);
    unsetenv ("HEGEL_TEST_CASES");
    check (obs->n_calls == 4, m, "the typed field wins over env and profile");
  }

  {
    check (hegel_set_default_profile ("seven") == 0, m, "hegel_set_default_profile (\"seven\")");
    rc = run (NULL, use_fork);
    check (rc == 0 && obs->n_calls == 7, m, "default profile: the plain runner runs 7");
    hegel_run_options o = { .profile = "base" };
    run (&o, use_fork);
    check (obs->n_calls == 100, m, "profile \"base\" bypasses the default profile");
    check (hegel_set_default_profile (NULL) == 0, m, "hegel_set_default_profile (NULL)");
    rc = run (NULL, use_fork);
    check (rc == 0 && obs->n_calls == 100, m, "cleared: the engine default, 100");
  }

  {
    hegel_set_default_profile ("no_such_profile");
    rc = run (NULL, use_fork);
    hegel_set_default_profile ("");
    check (rc == 1 && obs->n_calls == 0, m, "an unknown default profile fails the run");
    hegel_run_options o = { .profile = "no_such_profile" };
    rc = run (&o, use_fork);
    check (rc == 1 && obs->n_calls == 0, m, "an unknown named profile fails the run");
  }
}

int
main (void)
{
  int                 saved, rc_base, rc_bad, rc_str, rc_dflt;
  FILE *              null;

  obs = mmap (NULL, sizeof (*obs), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (obs == MAP_FAILED) {
    perror ("mmap");
    return (1);
  }
  unsetenv ("HEGEL_TEST_CASES");
  unsetenv ("HEGEL_DEFAULT_PROFILE");

  printf ("Settings profiles...\n");
  {
    hegel_run_options seven = { .test_cases = 7 };
    hegel_run_options ci_eleven = { .profile = "ci", .test_cases = 11 };
    check (hegel_register_profile ("seven", &seven) == 0, "-", "register \"seven\"");
    check (hegel_register_profile ("ci_eleven", &ci_eleven) == 0, "-", "register \"ci_eleven\"");
  }
  {
    hegel_run_options bad = { .settings = "nope=1" };
    saved = dup (2);
    null = fopen ("/dev/null", "w");
    fflush (stderr);
    dup2 (fileno (null), 2);
    rc_base = hegel_register_profile ("base", NULL);
    rc_bad = hegel_register_profile ("has space", NULL);
    rc_str = hegel_register_profile ("bad_settings", &bad);
    rc_dflt = hegel_set_default_profile ("default");
    fflush (stderr);
    dup2 (saved, 2);
    close (saved);
    fclose (null);
    check (rc_base == 1, "-", "register: reserved name \"base\" refused");
    check (rc_bad == 1, "-", "register: malformed name refused");
    check (rc_str == 1, "-", "register: bad settings string refused");
    check (rc_dflt == 1, "-", "set_default_profile (\"default\") refused");
  }

  check_mode (0);
  check_mode (1);

  if (failures) {
    fprintf (stderr, "%d check(s) failed\n", failures);
    return (1);
  }
  return (0);
}
