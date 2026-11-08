/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a broken grammar is a health failure carrying the engine's
**       diagnostic, not a crash, a hang or a property failure.
**
** Layer 1: word_count() — counts the space-separated words of a string.
** Layer 2: tests that draw from a broken grammar, then check
**          word_count(); none of them gets that far:
**            - a grammar that does not parse ("start: (" ...), fork;
**            - a rule naming an undefined symbol, nofork;
**            - a start rule the grammar doesn't have, fork;
**            - a %declare'd terminal with no explicit value, nofork;
**            - explicit terminals with no fill callback, fork;
**            - a fill callback returning NULL, nofork.
**          Each must abort its run with "Health check failure" (the
**          runner prints the message; the Makefile checks it).
** Layer 3: main() runs all six (result runners, no exit) and exits 1
**          if every one was refused, 0 (the Makefile's failure) if any
**          run passed.
**
** Expected: EXIT non-zero, "Health check failure" on stderr.
*/
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
int
word_count (
const char *                s)
{
  int                 n = 0;
  int                 in = 0;

  for (; *s; s ++) {
    if (*s == ' ')
      in = 0;
    else if (!in) {
      in = 1;
      n ++;
    }
  }
  return (n);
}

/* ---- Layer 2: broken generators ---- */

static
void
check (
hegel_testcase *            tc,
const char *                s)
{
  HEGEL_ASSERT (word_count (s) >= 0, "unreachable");
  (void) tc;
}

static
void
testSyntax (
hegel_testcase *            tc)
{
  check (tc, hegel_draw_grammar (tc, "start: (\"a\" \n", NULL));
}

static
void
testUndefined (
hegel_testcase *            tc)
{
  check (tc, hegel_draw_grammar (tc, "start: word+\n", NULL));
}

static
void
testNoSuchStart (
hegel_testcase *            tc)
{
  check (tc, hegel_draw_grammar (tc, "start: \"a\"+\n", "sentence"));
}

static
void
testDeclared (
hegel_testcase *            tc)
{
  check (tc, hegel_draw_grammar (tc, "start: NAME+\n%declare NAME\n", NULL));
}

static const char * const NAMES[] = { "NAME" };

static
void
testNoFill (
hegel_testcase *            tc)
{
  hegel_grammar_options o;

  memset (&o, 0, sizeof o);
  o.explicit_names = NAMES;
  o.n_explicit = 1;
  check (tc, hegel_draw_grammar_with (tc, "start: NAME+\n%declare NAME\n", &o));
}

static
const char *
fill_null (
hegel_testcase *            tc,
size_t                      terminal,
void *                      ctx)
{
  (void) tc;
  (void) terminal;
  (void) ctx;
  return (NULL);
}

static
void
testFillNull (
hegel_testcase *            tc)
{
  hegel_grammar_options o;

  memset (&o, 0, sizeof o);
  o.explicit_names = NAMES;
  o.n_explicit = 1;
  o.fill = fill_null;
  check (tc, hegel_draw_grammar_with (tc, "start: NAME+\n%declare NAME\n", &o));
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  int                 refused = 0;

  refused += hegel_run_test_result (testSyntax);
  refused += hegel_run_test_nofork_with (testUndefined, NULL);
  refused += hegel_run_test_result (testNoSuchStart);
  refused += hegel_run_test_nofork_with (testDeclared, NULL);
  refused += hegel_run_test_result (testNoFill);
  refused += hegel_run_test_nofork_with (testFillNull, NULL);
  printf ("%d of 6 broken grammar runs refused\n", refused);
  return (refused == 6 ? 1 : 0);
}
