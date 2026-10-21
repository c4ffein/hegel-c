/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_C_REPRODUCE replays the failure a report names.
**
** Layer 1: parse_port() — should accept 1..65535, but checks against a
**          wrong limit (99999), so 65536..99999 slip through.
** Layer 2: the property "parse_port accepts only valid ports" is the
**          second of two runs, so run numbering matters.  Each
**          "invocation of the binary" is a fresh child process (numbering
**          starts at 1, as in a real rerun):
**   1. a plain invocation fails; its report ends with
**      `reproduce: HEGEL_C_REPRODUCE='2:<blob>'`, which main captures;
**   2. with that variable set, run 2 replays exactly the shrunk failure
**      (65536, in a few cases) and run 1 is skipped;
**   3. with parse_port fixed, the same blob "did not reproduce" and the
**      invocation passes — how a fix is checked.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static int            fixed;

static
int
parse_port (
long                v)
{
  long                max = fixed ? 65535 : 99999;   /* the bug */

  return (v >= 1 && v <= max);
}

/* ---- Layer 2: properties ---- */

static int            n_cases;
static long           last_value;

static
void
test_unrelated (
hegel_testcase *    tc)
{
  int                 x;

  x = hegel_draw_int (tc, 0, 10);
  HEGEL_ASSERT (x >= 0, "x=%d", x);
}

static
void
test_port_range (
hegel_testcase *    tc)
{
  long                v;

  v = (long) hegel_draw_i64 (tc, -20000, 200000);
  n_cases ++;
  last_value = v;
  if (parse_port (v))
    HEGEL_ASSERT (v <= 65535, "accepted port %ld", v);
}

/* The two runs of the "binary".  In-process runners, so the counters
** above are visible afterwards. */
static
int
run_both (void)
{
  /* The engine favours boundaries and small values: a middle window
  ** gets ~3 hits per 100 cases, so 100 cases miss it ~5% of the time. */
  hegel_run_options   many = { .test_cases = 1000 };
  int                 rc;

  rc = hegel_run_test_nofork_with (test_unrelated, NULL);
  rc |= hegel_run_test_nofork_with (test_port_range, &many);
  return (rc);
}

/* ---- Layer 3: runner ---- */

/* One invocation of the "binary" as a fresh child.  repro: the value of
** HEGEL_C_REPRODUCE, or NULL.  capture: if non-NULL, the child's stderr
** is read into it.  Exit status bits: 1 = the runs failed, 2 = the last
** value drawn was 65536, 4 = at most 4 cases ran (replayed, not
** explored). */
static
int
invocation (
const char *        repro,
int                 with_fix,
char *              capture,
size_t              cap_size)
{
  int                 fds[2];
  pid_t               pid;
  int                 status;
  size_t              used = 0;
  ssize_t             r;

  if (capture != NULL && pipe (fds) != 0)
    return (-1);
  fflush (stdout);
  fflush (stderr);
  pid = fork ();
  if (pid == 0) {
    int               rc;

    if (capture != NULL) {
      close (fds[0]);
      dup2 (fds[1], 2);
      close (fds[1]);
    }
    if (repro != NULL)
      setenv ("HEGEL_C_REPRODUCE", repro, 1);
    fixed = with_fix;
    rc = run_both ();
    fflush (stdout);
    fflush (stderr);
    _exit ((rc ? 1 : 0) | (last_value == 65536 ? 2 : 0) | (n_cases <= 4 ? 4 : 0));
  }
  if (capture != NULL) {
    close (fds[1]);
    while (used + 1 < cap_size &&
           (r = read (fds[0], capture + used, cap_size - 1 - used)) > 0)
      used += (size_t) r;
    capture[used] = 0;
    close (fds[0]);
  }
  if (pid < 0 || waitpid (pid, &status, 0) < 0 || !WIFEXITED (status))
    return (-1);
  return (WEXITSTATUS (status));
}

/* Copy the quoted value after HEGEL_C_REPRODUCE=' in text into out. */
static
int
find_reproduce (
const char *        text,
char *              out,
size_t              n)
{
  const char *        key = "HEGEL_C_REPRODUCE='";
  const char *        p;
  const char *        end;

  p = strstr (text, key);
  if (p == NULL)
    return (-1);
  p += strlen (key);
  end = strchr (p, '\'');
  if (end == NULL || (size_t) (end - p) >= n)
    return (-1);
  memcpy (out, p, (size_t) (end - p));
  out[end - p] = 0;
  return (0);
}

int
main (void)
{
  static char         report[1 << 16];
  char                repro[4096];
  int                 rc;
  int                 failures = 0;

  printf ("Replaying a failure with HEGEL_C_REPRODUCE...\n");
  rc = invocation (NULL, 0, report, sizeof (report));
  if (rc < 0 || !(rc & 1) || find_reproduce (report, repro, sizeof (repro)) != 0) {
    fprintf (stderr, "FAIL: the plain invocation should fail with a reproduce line\n");
    return (1);
  }
  printf ("  captured: HEGEL_C_REPRODUCE='%s'\n", repro);
  if (strncmp (repro, "2:", 2) != 0) {
    fprintf (stderr, "FAIL: expected the failure in run 2\n");
    failures ++;
  }

  rc = invocation (repro, 0, NULL, 0);
  printf ("  replay: %s, last value %s65536, %s\n", (rc & 1) ? "fails" : "passes",
          (rc & 2) ? "" : "not ", (rc & 4) ? "replayed" : "explored");
  if (rc != 7) {
    fprintf (stderr, "FAIL: the replay should fail at 65536 within a few cases\n");
    failures ++;
  }

  rc = invocation (repro, 1, NULL, 0);
  printf ("  replay against the fix: %s\n", (rc & 1) ? "fails" : "passes");
  if (rc < 0 || (rc & 1)) {
    fprintf (stderr, "FAIL: with the fix the blob should not reproduce (pass)\n");
    failures ++;
  }

  return (failures ? 1 : 0);
}
