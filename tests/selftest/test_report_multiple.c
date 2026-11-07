/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_run_options.report_multiple and HEGEL_REPORT_MULTIPLE_FAILURES
** report every distinct failure; by default a run reports one.
**
** Layer 1: clamp_percent() has two independent bugs — values above 500
**          come back unclamped, values below -500 come back negative.
** Layer 2: one property with two HEGEL_ASSERTs (two origins), run three
**          times with stderr captured: default (expect 1 failure report),
**          .report_multiple = 1 (expect 2), and the environment variable
**          (expect 2).  Each report must carry its own reproduce line.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

/* Clamp to [0, 100].  Two bugs, on purpose. */
static int clamp_percent (int x)
{
  if (x > 500) return x;              /* bug A: large values pass through */
  if (x < -500) return x;             /* bug B: very negative values too */
  if (x < 0) return 0;
  if (x > 100) return 100;
  return x;
}

/* ---- Layer 2: the property and the checks ---- */

static void test_clamp (hegel_testcase *tc)
{
  int x = HEGEL_DRAW_INT (-1000, 1000);
  int y = clamp_percent (x);
  HEGEL_ASSERT (y <= 100, "clamp_percent(%d) = %d, above 100", x, y);   /* origin A */
  HEGEL_ASSERT (y >= 0, "clamp_percent(%d) = %d, below 0", x, y);       /* origin B */
}

/* Run the property with stderr captured; return how many failure
** reports and reproduce lines it printed. */
static void run_captured (const hegel_run_options *o, int *reports, int *reproduce,
                          int *numbered)
{
  FILE * tmp = tmpfile ();
  int    saved = dup (2);
  char   line[1024];

  fflush (stderr);
  dup2 (fileno (tmp), 2);
  int rc = hegel_run_test_nofork_with (test_clamp, o);
  fflush (stderr);
  dup2 (saved, 2);
  close (saved);

  *reports = *reproduce = *numbered = 0;
  rewind (tmp);
  while (fgets (line, sizeof line, tmp)) {
    if (strstr (line, "=== hegel-c: property test FAILED")) (*reports)++;
    if (strstr (line, "failure 1 of 2") || strstr (line, "failure 2 of 2")) (*numbered)++;
    if (strncmp (line, "reproduce: HEGEL_C_REPRODUCE=", 29) == 0) (*reproduce)++;
  }
  fclose (tmp);
  if (rc != 1) {
    fprintf (stderr, "  FAIL: the run should report a failure (rc=%d)\n", rc);
    exit (1);
  }
}

static int check (const char *what, const hegel_run_options *o, int want)
{
  int reports, reproduce, numbered;

  run_captured (o, &reports, &reproduce, &numbered);
  printf ("  %-34s %d failure report(s), %d reproduce line(s)\n", what, reports, reproduce);
  if (reports != want || reproduce != want || (want > 1 && numbered != want)) {
    fprintf (stderr, "  FAIL: %s: expected %d report(s)%s\n", what, want,
             want > 1 ? ", numbered" : "");
    return 1;
  }
  return 0;
}

/* ---- Layer 3: runner ---- */

int main (void)
{
  hegel_run_options one   = { .test_cases = 1000, .has_seed = 1, .seed = 7 };
  hegel_run_options multi = { .test_cases = 1000, .has_seed = 1, .seed = 7,
                              .report_multiple = 1 };
  int errors = 0;

  printf ("Testing multiple-failure reporting...\n");
  unsetenv ("HEGEL_REPORT_MULTIPLE_FAILURES");
  errors += check ("default", &one, 1);
  errors += check (".report_multiple = 1", &multi, 2);
  setenv ("HEGEL_REPORT_MULTIPLE_FAILURES", "1", 1);
  errors += check ("HEGEL_REPORT_MULTIPLE_FAILURES=1", &one, 2);
  unsetenv ("HEGEL_REPORT_MULTIPLE_FAILURES");

  if (errors) return 1;
  printf ("OK\n");
  return 0;
}
