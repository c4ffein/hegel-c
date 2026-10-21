/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a failure first met while shrinking another one is shrunk too.
**
** hegel-c runs report one failure (report_multiple_failures off): the
** engine shrinks only the failure with the smallest example.  Shrinking
** it can run into a *different* failure with a smaller example than any
** seen so far — that one is then the failure reported, so it must be
** shrunk as well, not reported as stumbled upon.
**
** Layer 1: classify(x) on x in [0, 10000] — "large" for x >= 1000,
**          "small" for 100 < x < 1000, fine otherwise.  The two are two
**          distinct HEGEL_ASSERTs, so two origins (two bugs) to the
**          engine.  Most of the range is "large", so the first failure
**          found is (almost always) large, and "small" is first reached
**          while shrinking it.
** Layer 2: each failing execution records {which, x} in a probe file
**          (last write wins across forks: the last failing execution is
**          the final replay of the reported failure); the first failing
**          execution is recorded separately.
**
** Expected: the reported failure is "small" at x = 101.
** Port of libhegel's
**   one_reported_failure_shrinks_a_smaller_failure_found_while_shrinking
** (libhegel/tests/embedded/native/test_runner_tests.rs, added in 0.48.0;
** on 0.47.x the small failure is reported unshrunk).
**
** The engine favours small values, so a third to a half of seeds find
** the small failure first and never exercise the path; main then tries
** the next seed, up to MAX_ATTEMPTS.  Each attempt fixes its seed
** (HEGEL_SEED=k, HEGEL_DERANDOMIZE=false — the env wins over the
** engine's `ci` profile, which derandomizes and would make every
** attempt the same run), so the test is deterministic everywhere.
**
** Test status: TESTS_PASS — main exits 0 iff, in the first run whose
** first failure was large, the reported failure is small at x = 101.
*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "hegel_c.h"

/* ---- Layer 1: the function under test ---- */

enum { FINE = 0, SMALL = 1, LARGE = 2 };

#define MAX_ATTEMPTS   30

static
int
classify (
int                 x)
{
  if (x >= 1000)
    return (LARGE);
  if (x > 100)
    return (SMALL);
  return (FINE);
}

/* ---- Probe files: what the forked children saw ---- */

typedef struct {
  int                 which;
  int                 x;
} Probe;

static char           last_path[256];
static char           first_path[256];

static
void
probe_write (
const char *        path,
int                 flags,
const Probe *       p)
{
  int                 fd;
  ssize_t             w;

  fd = open (path, O_WRONLY | O_CREAT | flags, 0600);
  if (fd < 0) return;               /* first_path: O_EXCL, already written */
  w = write (fd, p, sizeof (*p));
  (void) w;
  close (fd);
}

static
int
probe_read (
const char *        path,
Probe *             p)
{
  int                 fd;
  ssize_t             r;

  fd = open (path, O_RDONLY);
  if (fd < 0) return (0);
  r = read (fd, p, sizeof (*p));
  close (fd);
  unlink (path);
  return (r == (ssize_t) sizeof (*p));
}

/* ---- Layer 2: hegel test ---- */

static
void
test_two_failures (
hegel_testcase *    tc)
{
  int                 x;
  Probe               p;

  x = hegel_draw_int (tc, 0, 10000);
  p.which = classify (x);
  p.x = x;
  if (p.which != FINE) {
    probe_write (first_path, O_EXCL, &p);
    probe_write (last_path, O_TRUNC, &p);
  }
  HEGEL_ASSERT (p.which != LARGE, "large failure: x=%d", x);
  HEGEL_ASSERT (p.which != SMALL, "small failure: x=%d", x);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  Probe               first;
  Probe               last;
  int                 attempt;

  snprintf (last_path, sizeof (last_path),
            "/tmp/hegel_shrink_probe_twofail_last_%d", (int) getpid ());
  snprintf (first_path, sizeof (first_path),
            "/tmp/hegel_shrink_probe_twofail_first_%d", (int) getpid ());

  printf ("Shrinking a failure found while shrinking another...\n");
  for (attempt = 1; attempt <= MAX_ATTEMPTS; attempt ++) {
    char              seed[32];

    snprintf (seed, sizeof (seed), "%d", attempt);
    setenv ("HEGEL_SEED", seed, 1);
    setenv ("HEGEL_DERANDOMIZE", "false", 1);
    unlink (last_path);
    unlink (first_path);
    if (hegel_run_test_result (test_two_failures) == 0) {
      fprintf (stderr, "FAIL: the run found no failure\n");
      return (1);
    }
    if (!probe_read (first_path, &first) || !probe_read (last_path, &last)) {
      fprintf (stderr, "FAIL: probe files missing\n");
      return (1);
    }
    if (first.which == LARGE)
      break;
    printf ("  seed %d: first failure already small (x=%d), path not exercised — "
            "next seed\n", attempt, first.x);
  }
  if (attempt > MAX_ATTEMPTS) {
    fprintf (stderr, "INCONCLUSIVE: no seed of 1..%d found the large failure first\n",
             MAX_ATTEMPTS);
    return (1);
  }
  printf ("  first failure: large at x=%d; reported: %s at x=%d\n", first.x,
          last.which == SMALL ? "small" : "large", last.x);
  if (last.which != SMALL || last.x != 101) {
    fprintf (stderr, "FAIL: expected the reported failure to be small at x=101\n");
    return (1);
  }
  return (0);
}
