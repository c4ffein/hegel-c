/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a failing property over grammar strings shrinks to the shortest
**       string of the grammar that fails.
**
** EXPECTED_SHRINK: ((()))
**
** Layer 1: paren_depth() — the nesting depth of a balanced parentheses
**          string, with a fixed stack of open positions.  Deliberate
**          bug: the stack holds 2 entries and the function returns -1
**          when a third '(' is open at once, so any string nesting 3
**          deep is rejected.
** Layer 2: draw balanced strings from a Lark grammar with
**          hegel_draw_grammar and assert paren_depth(s) >= 0.  The
**          shortest failing string is "((()))": the shrinker must cut
**          the derivation down to it (siblings dropped, nesting exactly
**          3).
** Layer 3: runs it once in fork mode (must fail), then nofork (so the
**          last failing string is visible to main), prints
**          "shrunk: <string>" for the Makefile's TESTS_SHRINK check,
**          and exits 1.
**
** Expected: EXIT non-zero with "shrunk: ((()))".
*/
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
int
paren_depth (
const char *                s)
{
  int                 stack[2];               /* BUG: room for 2 open parens */
  int                 top = 0;
  int                 depth = 0;
  int                 i;

  for (i = 0; s[i]; i ++) {
    if (s[i] == '(') {
      if (top == 2)
        return (-1);                          /* "overflow" */
      stack[top ++] = i;
      if (top > depth)
        depth = top;
    }
    else if (top > 0)
      top --;
  }
  (void) stack;
  return (depth);
}

/* ---- Layer 2: hegel test ---- */

static const char * PARENS = "start: (\"(\" start \")\")*\n";

static char g_last_failing[256];

static
void
testDepth (
hegel_testcase *            tc)
{
  char *              s = hegel_draw_grammar (tc, PARENS, NULL);
  int                 d = paren_depth (s);

  if (d < 0)
    snprintf (g_last_failing, sizeof g_last_failing, "%s", s);
  HEGEL_ASSERT (d >= 0, "paren_depth(\"%s\") = %d", s, d);
}

/* ---- Layer 3: runner (see Makefile TESTS_SHRINK) ---- */

int
main (void)
{
  hegel_run_options   o;

  memset (&o, 0, sizeof o);
  o.test_cases = 500;
  printf ("Testing that a grammar string shrinks (fork mode)...\n");
  if (hegel_run_test_with (testDepth, &o) == 0) {
    printf ("  fork mode: the depth bug was not found\n");
    return (0);                               /* TESTS_SHRINK: exit 0 is the failure */
  }
  printf ("Testing that a grammar string shrinks (nofork)...\n");
  if (hegel_run_test_nofork_with (testDepth, &o) == 0) {
    printf ("  nofork: the depth bug was not found\n");
    return (0);
  }
  printf ("shrunk: %s\n", g_last_failing);
  return (1);
}
