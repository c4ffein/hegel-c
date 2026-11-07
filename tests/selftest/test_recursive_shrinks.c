/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a tree drawn with hegel_draw_recursive shrinks to a minimal
**       counterexample.
**
** EXPECTED_SHRINK: 5 nodes
**
** Layer 1: expr_print() writes an expression tree (single digits, + and
**          -) with as few parentheses as it thinks it needs, and
**          expr_parse() reads it back.  The printer has a deliberate
**          bug: it never parenthesizes the RIGHT operand of '-', so
**          a - (b + c) prints as "a-b+c".
** Layer 2: draw an expression (leaf = digit, branch = an operator over
**          two children, nodes from hegel_alloc), and check that
**          eval (parse (print (e))) == eval (e).  The smallest tree that
**          breaks it is a '-' whose right child is an operator with a
**          non-zero right operand: 5 nodes, e.g. 0-(0+1).
** Layer 3: runs nofork (so the last failing tree is visible to main),
**          prints "shrunk: N nodes" for the Makefile's TESTS_SHRINK
**          check, and exits 1.
**
** Expected: EXIT non-zero with "shrunk: 5 nodes".
*/
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"

typedef struct Expr {
  char                op;      /* '+' or '-', 0 for a digit leaf */
  int                 digit;
  struct Expr *       a;
  struct Expr *       b;
} Expr;

/* ---- Layer 1: functions under test ---- */

static
int
expr_eval (
const Expr *                e)
{
  if (!e->op)
    return (e->digit);
  return (e->op == '+' ? expr_eval (e->a) + expr_eval (e->b)
                       : expr_eval (e->a) - expr_eval (e->b));
}

/* Appends to buf[*len]; buf is big enough for the budgets used. */
static
void
expr_print (
const Expr *                e,
char *                      buf,
int *                       len)
{
  if (!e->op) {
    buf[(*len) ++] = (char) ('0' + e->digit);
    return;
  }
  expr_print (e->a, buf, len);                /* left-assoc: never needs parens */
  buf[(*len) ++] = e->op;
  /* BUG: '-' over an operator needs parentheses around its right side. */
  expr_print (e->b, buf, len);
}

/* expr := term (('+'|'-') term)* ; term := digit | '(' expr ')' */
static int parse_expr (const char ** s);

static
int
parse_term (
const char **               s)
{
  int                 v;

  if (**s == '(') {
    (*s) ++;
    v = parse_expr (s);
    (*s) ++;                                  /* ')' */
    return (v);
  }
  return (*(*s) ++ - '0');
}

static
int
parse_expr (
const char **               s)
{
  int                 v;
  char                op;

  v = parse_term (s);
  while (**s == '+' || **s == '-') {
    op = *(*s) ++;
    v = op == '+' ? v + parse_term (s) : v - parse_term (s);
  }
  return (v);
}

static
int
expr_parse (
const char *                s)
{
  return (parse_expr (&s));
}

/* ---- Layer 2: hegel test ---- */

static
void *
gen_expr (
hegel_testcase *            tc,
bool                        leaf,
void *                      ctx)
{
  Expr *              e;

  (void) ctx;
  e = hegel_alloc (tc, sizeof *e);
  if (leaf) {
    e->digit = hegel_draw_int (tc, 0, 9);
    return (e);
  }
  e->op = hegel_draw_int (tc, 0, 1) ? '-' : '+';
  e->a = hegel_recursive_child (tc);
  e->b = hegel_recursive_child (tc);
  return (e);
}

static
int
expr_nodes (
const Expr *                e)
{
  return (e->op ? 1 + expr_nodes (e->a) + expr_nodes (e->b) : 1);
}

/* Fully parenthesized, for the report. */
static
void
expr_show (
const Expr *                e,
char *                      buf,
int *                       len)
{
  if (!e->op) {
    buf[(*len) ++] = (char) ('0' + e->digit);
    return;
  }
  buf[(*len) ++] = '(';
  expr_show (e->a, buf, len);
  buf[(*len) ++] = e->op;
  expr_show (e->b, buf, len);
  buf[(*len) ++] = ')';
}

#define MAX_DEPTH  8
#define MAX_LEAVES 30

static int g_last_failing_nodes;

static
void
testRoundTrip (
hegel_testcase *            tc)
{
  Expr *              e;
  char *              printed;
  char *              shown;
  int                 len = 0;
  int                 slen = 0;
  int                 want;
  int                 got;

  e = hegel_draw_recursive (tc, MAX_DEPTH, MAX_LEAVES, gen_expr, NULL);
  printed = hegel_alloc (tc, 4 * MAX_LEAVES + 1);   /* zeroed: terminated */
  shown = hegel_alloc (tc, 4 * MAX_LEAVES + 1);
  expr_print (e, printed, &len);
  expr_show (e, shown, &slen);
  hegel_note (tc, shown);

  want = expr_eval (e);
  got = expr_parse (printed);
  if (got != want)
    g_last_failing_nodes = expr_nodes (e);
  HEGEL_ASSERT (got == want, "%s printed as \"%s\": %d, reads back as %d",
                shown, printed, want, got);
}

/* ---- Layer 3: runner (see Makefile TESTS_SHRINK) ---- */

int
main (
int                 argc,
char *              argv[])
{
  hegel_run_options   o;

  (void) argc;
  (void) argv;

  memset (&o, 0, sizeof o);
  o.test_cases = 1000;
  printf ("Testing that a hegel_draw_recursive tree shrinks...\n");
  if (hegel_run_test_nofork_with (testRoundTrip, &o) == 0) {
    printf ("  the printer bug was not found\n");
    return (0);                               /* TESTS_SHRINK: exit 0 is the failure */
  }
  printf ("shrunk: %d nodes\n", g_last_failing_nodes);
  return (1);
}
