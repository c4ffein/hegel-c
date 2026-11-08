/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_event / hegel_event_value reach the engine intact, from
**       fork and nofork bodies and from stateful rules, and show up in
**       its statistics report.
**
** Layer 1: clamp_to() clamps an int into [lo, hi] (correct).
** Layer 2: a property over clamp_to() that records known events: a label
**          in every case, a constant value twice per case, a UTF-8 label,
**          an empty label, a 300-byte label (several wire chunks), a
**          label recorded only in cases that are then discarded with
**          hegel_assume, and a label in about half of the cases.  A
**          stateful machine whose rules record their names.
** Layer 3: with HEGEL_STATISTICS=1 (read by the engine's settings) the
**          engine prints its end-of-run report on stderr; main captures
**          stderr around each run (fork, nofork, stateful fork, stateful
**          nofork) and checks the report: the case count N, every label
**          at the expected percentage, the constant value's summary
**          (count 2N), no line for the discarded-only label.  Exits 0
**          only if every check holds.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
int
clamp_to (
int                         x,
int                         lo,
int                         hi)
{
  return (x < lo ? lo : (x > hi ? hi : x));
}

/* ---- Layer 2: properties that record events ---- */

static char                 g_long_label[320];

static
void
testClamp (
hegel_testcase *            tc)
{
  int                 x = hegel_draw_int (tc, -1000, 1000);
  int                 r;

  if (x % 7 == 0) {
    hegel_event (tc, "discarded only");      /* the case is dropped: never counted */
    hegel_assume (tc, 0);
  }
  hegel_event (tc, "always");
  hegel_event (tc, "always");                /* repeats in one case count once */
  hegel_event_value (tc, "const", 2.5);
  hegel_event_value (tc, "const", 2.5);
  hegel_event_value (tc, "unicode λ ü", -3.0);
  hegel_event (tc, "");
  hegel_event (tc, g_long_label);
  if (x > 0)
    hegel_event (tc, "positive");

  r = clamp_to (x, -10, 10);
  HEGEL_ASSERT (r >= -10 && r <= 10, "clamp_to (%d) = %d", x, r);
}

typedef struct { int n; } Counter;

static void * sm_setup (void) { return calloc (1, sizeof (Counter)); }
static void   sm_teardown (void * s) { free (s); }

static
void
rule_inc (
void *                      s,
hegel_testcase *            tc)
{
  ((Counter *) s)->n ++;
  hegel_event (tc, "rule inc");
  hegel_event_value (tc, "counter", 1.0);
}

static
void
rule_reset (
void *                      s,
hegel_testcase *            tc)
{
  ((Counter *) s)->n = 0;
  hegel_event (tc, "rule reset");
}

static
void
inv_nonneg (
const void *                s,
hegel_testcase *            tc)
{
  (void) tc;
  HEGEL_ASSERT (((const Counter *) s)->n >= 0, "negative counter");
}

/* ---- Layer 3: run with stderr captured, check the report ---- */

static char                 g_out[1 << 16];

/* Run fn with fd 2 redirected to a temporary file; g_out gets what was
** written.  Returns fn's result. */
static
int
captured (
int                         (*fn) (int),
int                         arg)
{
  FILE *              f = tmpfile ();
  int                 saved;
  int                 rc;
  size_t              n;

  fflush (stderr);
  saved = dup (2);
  dup2 (fileno (f), 2);
  rc = fn (arg);
  fflush (stderr);
  dup2 (saved, 2);
  close (saved);
  rewind (f);
  n = fread (g_out, 1, sizeof g_out - 1, f);
  g_out[n] = '\0';
  fclose (f);
  return (rc);
}

static
int
run_prop (
int                         nofork)
{
  hegel_run_options   o = { .test_cases = 100 };

  return (nofork ? hegel_run_test_nofork_with (testClamp, &o)
                 : hegel_run_test_with (testClamp, &o));
}

static
int
run_sm (
int                         nofork)
{
  hegel_state_machine * sm = hegel_state_machine_new (sm_setup, sm_teardown);

  hegel_state_machine_add_rule      (sm, "inc",   rule_inc);
  hegel_state_machine_add_rule      (sm, "reset", rule_reset);
  hegel_state_machine_add_invariant (sm, "nonneg", inv_nonneg);
  if (nofork)
    hegel_state_machine_run_nofork_n (sm, 50);
  else
    hegel_state_machine_run_n (sm, 50);
  hegel_state_machine_free (sm);
  return (0);
}

static int                  g_bad;

static
void
expect (
const char *                what,
const char *                needle,
int                         present)
{
  if ((strstr (g_out, needle) != NULL) == present)
    return;
  printf ("  %s: %s \"%s\"\n", what, present ? "missing" : "unexpected", needle);
  g_bad = 1;
}

static
void
check_prop (
const char *                what)
{
  unsigned long       n = 0;
  const char *        p = strstr (g_out, "Statistics (over ");
  char                line[512];

  if (!p || sscanf (p, "Statistics (over %lu test cases)", &n) != 1 || n == 0) {
    printf ("  %s: no statistics report in:\n%s\n", what, g_out);
    g_bad = 1;
    return;
  }
  expect (what, "  * always: 100.0% of test cases\n", 1);
  expect (what, "  * : 100.0% of test cases\n", 1);
  snprintf (line, sizeof line, "  * %s: 100.0%% of test cases\n", g_long_label);
  expect (what, line, 1);
  expect (what, "  * positive: ", 1);
  expect (what, "  * positive: 100.0%", 0);
  expect (what, "discarded only", 0);
  snprintf (line, sizeof line,
            "  * const: count %lu, min 2.5, median 2.5, mean 2.50, p90 2.5, max 2.5\n", 2 * n);
  expect (what, line, 1);
  snprintf (line, sizeof line,
            "  * unicode λ ü: count %lu, min -3, median -3, mean -3.00, p90 -3, max -3\n", n);
  expect (what, line, 1);
  printf ("%s: report over %lu test cases checked\n", what, n);
}

static
void
check_sm (
const char *                what)
{
  expect (what, "Statistics (over ", 1);
  expect (what, "  * rule inc: ", 1);
  expect (what, "  * rule reset: ", 1);
  expect (what, "  * counter: count ", 1);
  printf ("%s: report checked\n", what);
}

int
main (void)
{
  memset (g_long_label, 'x', 300);
  memcpy (g_long_label + 300, " long label end", 16);
  setenv ("HEGEL_STATISTICS", "1", 1);

  if (captured (run_prop, 0) != 0) { printf ("fork run failed:\n%s\n", g_out); return (1); }
  check_prop ("fork");
  if (captured (run_prop, 1) != 0) { printf ("nofork run failed:\n%s\n", g_out); return (1); }
  check_prop ("nofork");
  captured (run_sm, 0);
  check_sm ("stateful fork");
  captured (run_sm, 1);
  check_sm ("stateful nofork");
  return (g_bad);
}
