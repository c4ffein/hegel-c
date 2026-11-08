/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a grammar's explicit terminals are filled by the test's own
**       hegel draws, spliced in where the terminals stand.
**
** Layer 1: sum_vars() — reads "v<n>+v<n>+..." (the language of SUMS
**          below) into its numbers; -1 for anything else.
** Layer 2: SUMS %declares VAR; the fill callback draws n in [0, 99]
**          with hegel_draw_int and returns "v<n>" (a hegel_alloc
**          block).  The test checks the string parses, every number is
**          in range, and the numbers are exactly the last values the
**          fill drew, in order (an attempt the engine discards — values
**          over the size cap — re-asks from its first terminal, so
**          earlier fills may be dropped, never reordered).  A second
**          grammar overrides a DEFINED terminal (WORD) the same way.
**          Both run in fork mode (each fill draw crosses the pipe to the
**          parent, between the engine's NEXT and PROVIDE steps) and in
**          nofork mode.
** Layer 3: main() runs all four and exits 0 only if every one held and
**          some string had several terminals.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

#define MAX_VARS 64

/* ---- Layer 1: function under test ---- */

static
int
sum_vars (
const char *                s,
int *                       nums,
int                         cap)
{
  int                 n = 0;
  char *              end;
  long                v;

  for (;;) {
    if (*s != 'v' || s[1] < '0' || s[1] > '9' || n == cap)
      return (-1);
    v = strtol (s + 1, &end, 10);
    nums[n ++] = (int) v;
    s = end;
    if (*s == '\0')
      return (n);
    if (*s ++ != '+')
      return (-1);
  }
}

/* ---- Layer 2: hegel tests ---- */

static const char * SUMS =
  "start: VAR (\"+\" VAR)*\n"
  "%declare VAR\n";

/* WORD has a pattern of its own; the test overrides it. */
static const char * WORDS =
  "start: WORD (\"+\" WORD)*\n"
  "WORD: /[a-z]+/\n";

typedef struct {
  int *               fills;                 /* every value fill drew, in order */
  int                 nfills;
} Fills;

static const char * const SUMS_NAMES[] = { "VAR" };
static const char * const WORDS_NAMES[] = { "WORD" };

static
const char *
fill_var (
hegel_testcase *            tc,
size_t                      terminal,
void *                      ctx)
{
  Fills *             f = ctx;
  char *              v = hegel_alloc (tc, 8);
  int                 n;

  HEGEL_ASSERT (terminal == 0, "unknown terminal %zu", terminal);
  n = hegel_draw_int (tc, 0, 99);
  f->fills = hegel_realloc (tc, f->fills, (size_t) (f->nfills + 1) * sizeof *f->fills);
  f->fills[f->nfills ++] = n;
  snprintf (v, 8, "v%d", n);
  return (v);
}

static int g_max_vars;                        /* nofork: most terminals in one string */

static
void
check_sums (
hegel_testcase *            tc,
const char *                s,
const Fills *               f)
{
  int                 nums[MAX_VARS];
  int                 n;
  int                 i;

  hegel_note (tc, s);
  n = sum_vars (s, nums, MAX_VARS);
  HEGEL_ASSERT (n >= 1, "not a sum of vars: \"%s\"", s);
  HEGEL_ASSERT (n <= f->nfills, "%d vars but only %d fills: \"%s\"", n, f->nfills, s);
  for (i = 0; i < n; i ++) {
    HEGEL_ASSERT (nums[i] >= 0 && nums[i] <= 99, "var %d out of range in \"%s\"", i, s);
    HEGEL_ASSERT (nums[i] == f->fills[f->nfills - n + i],
                  "var %d is %d, but the fill drew %d: \"%s\"",
                  i, nums[i], f->fills[f->nfills - n + i], s);
  }
  if (n > g_max_vars)
    g_max_vars = n;
}

static
void
testSums (
hegel_testcase *            tc)
{
  hegel_grammar_options o;
  Fills *             f = hegel_alloc (tc, sizeof *f);
  char *              s;

  memset (&o, 0, sizeof o);
  o.explicit_names = SUMS_NAMES;
  o.n_explicit = 1;
  o.fill = fill_var;
  o.fill_ctx = f;
  o.max_size = 100;
  s = hegel_draw_grammar_with (tc, SUMS, &o);
  check_sums (tc, s, f);
}

static
void
testOverride (
hegel_testcase *            tc)
{
  hegel_grammar_options o;
  Fills *             f = hegel_alloc (tc, sizeof *f);
  char *              s;

  memset (&o, 0, sizeof o);
  o.explicit_names = WORDS_NAMES;
  o.n_explicit = 1;
  o.fill = fill_var;                          /* "v<n>": never what /[a-z]+/ draws */
  o.fill_ctx = f;
  o.max_size = 100;
  s = hegel_draw_grammar_with (tc, WORDS, &o);
  check_sums (tc, s, f);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  hegel_run_options   o;
  int                 bad = 0;

  memset (&o, 0, sizeof o);
  o.test_cases = 200;

  printf ("explicit, fork:     ");
  bad |= hegel_run_test_with (testSums, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");

  printf ("explicit, nofork:   ");
  bad |= hegel_run_test_nofork_with (testSums, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");

  printf ("override, fork:     ");
  bad |= hegel_run_test_with (testOverride, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");

  printf ("override, nofork:   ");
  bad |= hegel_run_test_nofork_with (testOverride, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");

  printf ("most terminals in one string: %d\n", g_max_vars);
  if (g_max_vars < 3) {
    printf ("  strings never had several terminals\n");
    bad = 1;
  }
  return (bad);
}
