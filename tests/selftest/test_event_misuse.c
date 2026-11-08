/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: misusing hegel_event / hegel_event_value is a health failure
**       carrying the reason, in fork and nofork bodies.
**
** Layer 1: mean() of an int array (correct).
** Layer 2: six broken properties around mean(): a NaN value (fork), an
**          infinite value (nofork), a label that isn't UTF-8 (fork), a
**          NULL label for each function (nofork, fork), and an event
**          inside a HEGEL_ISOLATED block (nofork).  The engine's
**          diagnostics for NaN / infinity / bad UTF-8 prove the value and
**          the label bytes crossed the wire intact.
** Layer 3: main() runs each with stderr captured (result runners, no
**          exit) and checks it failed with "Health check failure" and
**          the expected reason, then replays the captured text on stderr
**          and exits 1.  A run that didn't fail as expected prints
**          "MISMATCH" and exits 0, which the Makefile's HEALTH category
**          rejects.
**
** Expected: EXIT non-zero, "Health check failure" on stderr.
*/
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
double
mean (
const int *                 xs,
int                         n)
{
  double              s = 0;

  for (int i = 0; i < n; i ++)
    s += xs[i];
  return (n ? s / n : 0.0);
}

/* ---- Layer 2: broken properties ---- */

static
void
draw_pair (
hegel_testcase *            tc,
int *                       xs)
{
  xs[0] = hegel_draw_int (tc, 0, 100);
  xs[1] = hegel_draw_int (tc, 0, 100);
}

static
void
check_mean (
const int *                 xs)
{
  double              m = mean (xs, 2);

  HEGEL_ASSERT (m >= 0 && m <= 100, "mean out of range: %g", m);
}

static void testNan (hegel_testcase * tc)
{
  int xs[2];
  draw_pair (tc, xs);
  hegel_event_value (tc, "mean", NAN);        /* wrong: must be finite */
  check_mean (xs);
}

static void testInf (hegel_testcase * tc)
{
  int xs[2];
  draw_pair (tc, xs);
  hegel_event_value (tc, "mean", INFINITY);   /* wrong: must be finite */
  check_mean (xs);
}

static void testBadUtf8 (hegel_testcase * tc)
{
  int xs[2];
  draw_pair (tc, xs);
  hegel_event (tc, "mean \xff\xfe");          /* wrong: not UTF-8 */
  check_mean (xs);
}

static void testNullEvent (hegel_testcase * tc)
{
  int xs[2];
  draw_pair (tc, xs);
  hegel_event (tc, NULL);                     /* wrong: NULL label */
  check_mean (xs);
}

static void testNullValue (hegel_testcase * tc)
{
  int xs[2];
  draw_pair (tc, xs);
  hegel_event_value (tc, NULL, mean (xs, 2)); /* wrong: NULL label */
  check_mean (xs);
}

static void testIsolated (hegel_testcase * tc)
{
  int xs[2];
  draw_pair (tc, xs);
  HEGEL_ISOLATED (tc) {
    hegel_event (tc, "inside");               /* wrong: engine call in the block */
    check_mean (xs);
  }
}

/* ---- Layer 3: runner ---- */

typedef struct {
  const char *        name;
  void                (*fn) (hegel_testcase *);
  int                 nofork;
  const char *        reason;                 /* expected in the diagnostic */
} Case;

static char                 g_out[1 << 15];

static
int
run_captured (
const Case *                c)
{
  hegel_run_options   o = { .test_cases = 20 };
  FILE *              f = tmpfile ();
  int                 saved;
  int                 rc;
  size_t              n;

  fflush (stderr);
  saved = dup (2);
  dup2 (fileno (f), 2);
  rc = c->nofork ? hegel_run_test_nofork_with (c->fn, &o) : hegel_run_test_with (c->fn, &o);
  fflush (stderr);
  dup2 (saved, 2);
  close (saved);
  rewind (f);
  n = fread (g_out, 1, sizeof g_out - 1, f);
  g_out[n] = '\0';
  fclose (f);
  return (rc);
}

int
main (void)
{
  static const Case   cases[] = {
    { "NaN value (fork)",          testNan,       0, "event_value rejected by the engine" },
    { "infinite value (nofork)",   testInf,       1, "event_value rejected by the engine" },
    { "non-UTF-8 label (fork)",    testBadUtf8,   0, "event rejected by the engine" },
    { "NULL event label (nofork)", testNullEvent, 1, "hegel_event with a NULL label" },
    { "NULL value label (fork)",   testNullValue, 0, "hegel_event_value with a NULL label" },
    { "inside HEGEL_ISOLATED",     testIsolated,  1, "inside HEGEL_ISOLATED" },
  };
  int                 bad = 0;

  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i ++) {
    int rc = run_captured (&cases[i]);
    int ok = rc == 1 && strstr (g_out, "Health check failure") && strstr (g_out, cases[i].reason);

    fprintf (stderr, "---- %s ----\n%s", cases[i].name, g_out);
    printf ("%-28s %s\n", cases[i].name, ok ? "refused" : "MISMATCH");
    if (!ok)
      bad = 1;
  }
  return (bad ? 0 : 1);
}
