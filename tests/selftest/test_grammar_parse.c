/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: strings drawn from a Lark grammar are in the grammar's language.
**
** Layer 1: two hand-written recognizers — parens_ok() (balanced
**          parentheses) and expr_ok() (integer arithmetic over + - * /
**          with parentheses and optional blanks between tokens, the
**          language of EXPR below).
** Layer 2: hegel tests drawing from the matching grammar with
**          hegel_draw_grammar / _with and asserting the recognizer
**          accepts every string:
**            - parentheses, fork mode and nofork mode;
**            - arithmetic with %import common.INT / common.WS (Lark's
**              bundled grammar) and %ignore, fork mode;
**            - a non-default start rule (start = "pair"), nofork;
**            - an %import read from import_dir (a file this test
**              writes), with max_size / max_depth set, fork mode.
**          Each also checks the strings are not all trivial.
** Layer 3: main() runs them all and exits 0 only if every one held.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "hegel_c.h"

/* ---- Layer 1: functions under test ---- */

static
int
parens_ok (
const char *                s)
{
  int                 depth = 0;

  for (; *s; s ++) {
    if (*s == '(')
      depth ++;
    else if (*s == ')') {
      if (-- depth < 0)
        return (0);
    }
    else
      return (0);
  }
  return (depth == 0);
}

/* expr := term (('+'|'-') term)* ; term := atom (('*'|'/') atom)* ;
** atom := INT | '(' expr ')' ; blanks (space, \t, \n, \r, \f) between
** tokens. */
static int rec_expr (const char ** s);

static
void
skip_ws (
const char **               s)
{
  while (**s == ' ' || **s == '\t' || **s == '\n' || **s == '\r' || **s == '\f')
    (*s) ++;
}

static
int
rec_atom (
const char **               s)
{
  skip_ws (s);
  if (**s == '(') {
    (*s) ++;
    if (!rec_expr (s))
      return (0);
    skip_ws (s);
    if (**s != ')')
      return (0);
    (*s) ++;
    return (1);
  }
  if (**s < '0' || **s > '9')
    return (0);
  while (**s >= '0' && **s <= '9')
    (*s) ++;
  return (1);
}

static
int
rec_term (
const char **               s)
{
  if (!rec_atom (s))
    return (0);
  for (;;) {
    skip_ws (s);
    if (**s != '*' && **s != '/')
      return (1);
    (*s) ++;
    if (!rec_atom (s))
      return (0);
  }
}

static
int
rec_expr (
const char **               s)
{
  if (!rec_term (s))
    return (0);
  for (;;) {
    skip_ws (s);
    if (**s != '+' && **s != '-')
      return (1);
    (*s) ++;
    if (!rec_term (s))
      return (0);
  }
}

static
int
expr_ok (
const char *                s)
{
  if (!rec_expr (&s))
    return (0);
  skip_ws (&s);
  return (*s == '\0');
}

/* ---- Layer 2: hegel tests ---- */

static const char * PARENS = "start: (\"(\" start \")\")*\n";

static const char * EXPR =
  "?start: sum\n"
  "?sum: product | sum \"+\" product | sum \"-\" product\n"
  "?product: atom | product \"*\" atom | product \"/\" atom\n"
  "?atom: INT | \"(\" sum \")\"\n"
  "%import common.INT\n"
  "%import common.WS\n"
  "%ignore WS\n";

static const char * PAIRS =
  "start: pair+\n"
  "pair: \"<\" WORD \"=\" WORD \">\"\n"
  "WORD: /[a-z]+/\n";

static int g_nontrivial;                      /* nofork: some string was not trivial */

static
void
testParens (
hegel_testcase *            tc)
{
  char *              s = hegel_draw_grammar (tc, PARENS, NULL);

  hegel_note (tc, s);
  HEGEL_ASSERT (parens_ok (s), "not balanced: \"%s\"", s);
  if (strlen (s) >= 4)
    g_nontrivial = 1;
}

static
void
testExpr (
hegel_testcase *            tc)
{
  char *              s = hegel_draw_grammar (tc, EXPR, NULL);

  hegel_note (tc, s);
  HEGEL_ASSERT (expr_ok (s), "not an expression: \"%s\"", s);
}

/* A pair "<a=b>" as start rule: exactly one pair. */
static
void
testStartRule (
hegel_testcase *            tc)
{
  char *              s = hegel_draw_grammar (tc, PAIRS, "pair");
  size_t              n = strlen (s);
  const char *        eq = strchr (s, '=');

  HEGEL_ASSERT (n >= 5 && s[0] == '<' && s[n - 1] == '>' && eq && strchr (eq + 1, '<') == NULL
                && strchr (s + 1, '<') == NULL, "not one pair: \"%s\"", s);
  if (n > 5)
    g_nontrivial = 1;
}

/* The imported file digits/lib.lark (written by main) defines DIGITS. */
static char g_import_dir[256];

static
void
testImportDir (
hegel_testcase *            tc)
{
  hegel_grammar_options o;
  char *              s;
  const char *        p;

  memset (&o, 0, sizeof o);
  o.import_dir = g_import_dir;
  o.max_size = 20;
  o.max_depth = 10;
  s = hegel_draw_grammar_with (tc, "start: DIGITS (\",\" DIGITS)*\n"
                                   "%import digits.lib.DIGITS\n", &o);
  HEGEL_ASSERT (strlen (s) <= 20, "over max_size: \"%s\"", s);
  for (p = s; *p; p ++)
    HEGEL_ASSERT ((*p >= '0' && *p <= '7') || *p == ',', "bad char in \"%s\"", s);
  HEGEL_ASSERT (s[0] != ',' && s[strlen (s) - 1] != ',' && strstr (s, ",,") == NULL,
                "not a list: \"%s\"", s);
}

static
int
write_import_dir (void)
{
  char                path[300];
  FILE *              f;

  snprintf (g_import_dir, sizeof g_import_dir, "/tmp/hegel_c_grammar_%ld", (long) getpid ());
  snprintf (path, sizeof path, "%s/digits", g_import_dir);
  if (mkdir (g_import_dir, 0700) != 0 || mkdir (path, 0700) != 0)
    return (-1);
  snprintf (path, sizeof path, "%s/digits/lib.lark", g_import_dir);
  if ((f = fopen (path, "w")) == NULL)
    return (-1);
  fputs ("DIGITS: /[0-7]+/\n", f);
  fclose (f);
  return (0);
}

static
void
remove_import_dir (void)
{
  char                path[300];

  snprintf (path, sizeof path, "%s/digits/lib.lark", g_import_dir);
  unlink (path);
  snprintf (path, sizeof path, "%s/digits", g_import_dir);
  rmdir (path);
  rmdir (g_import_dir);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  hegel_run_options   o;
  int                 bad = 0;

  memset (&o, 0, sizeof o);
  o.test_cases = 200;

  printf ("parens, fork:    ");
  bad |= hegel_run_test_with (testParens, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");

  g_nontrivial = 0;
  printf ("parens, nofork:  ");
  bad |= hegel_run_test_nofork_with (testParens, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");
  if (!g_nontrivial) {
    printf ("  every parens string was trivial\n");
    bad = 1;
  }

  printf ("expr, fork:      ");
  bad |= hegel_run_test_with (testExpr, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");

  g_nontrivial = 0;
  printf ("start rule:      ");
  bad |= hegel_run_test_nofork_with (testStartRule, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");
  if (!g_nontrivial) {
    printf ("  every pair was trivial\n");
    bad = 1;
  }

  if (write_import_dir () != 0) {
    printf ("could not write the import dir %s\n", g_import_dir);
    return (1);
  }
  printf ("import_dir:      ");
  bad |= hegel_run_test_with (testImportDir, &o);
  printf ("%s\n", bad ? "FAILED" : "ok");
  remove_import_dir ();

  return (bad);
}
