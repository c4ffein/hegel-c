/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Strategy shrink test: generate a strategy string using hegel draws,
** fail if it contains a conditional (/cvar>N?A:B;).  Hegel should
** find and shrink to the simplest conditional strategy.
**
** This proves shrinking works on complex recursive structured data —
** hegel doesn't just shrink integers, it shrinks the entire decision
** tree (which kind of node, which method letter, which parameter)
** down to the minimal failing example.
*/

#include <stdio.h>
#include <string.h>

#include "hegel_c.h"

#define BUF_SZ 256

/* ---- Strategy generator (same as test_strategy_gen.c) ---- */

static const char METHODS[] = "bfhmrsx";
static const int  N_METHODS = 7;
static const char * CVARS[] = { "vert", "edge", "levl" };

static int
append (char * buf, int pos, int cap, const char * s)
{
  int len = (int) strlen (s);
  int i;
  for (i = 0; i < len && pos + i < cap - 1; i++)
    buf[pos + i] = s[i];
  return pos + i;
}

static int
gen_strategy (
hegel_testcase *            tc,
char *                      buf,
int                         cap,
int                         depth)
{
  int                 kind;
  int                 pos = 0;

  if (depth >= 4)
    kind = 0;
  else
    kind = hegel_draw_int (tc, 0, 3);

  if (kind == 0) {
    int idx = hegel_draw_int (tc, 0, N_METHODS - 1);
    char m[2] = { METHODS[idx], '\0' };
    pos = append (buf, pos, cap, m);
  } else if (kind == 1) {
    pos = gen_strategy (tc, buf + pos, cap - pos, depth + 1);
    pos = append (buf, pos, cap, " ");
    pos += gen_strategy (tc, buf + pos, cap - pos, depth + 1);
  } else if (kind == 2) {
    pos = append (buf, pos, cap, "(");
    pos += gen_strategy (tc, buf + pos, cap - pos, depth + 1);
    pos = append (buf, pos, cap, "|");
    pos += gen_strategy (tc, buf + pos, cap - pos, depth + 1);
    pos = append (buf, pos, cap, ")");
  } else {
    int ci = hegel_draw_int (tc, 0, 2);
    int n  = hegel_draw_int (tc, 0, 999);
    char hdr[32];
    snprintf (hdr, sizeof (hdr), "/%s>%d?", CVARS[ci], n);
    pos = append (buf, pos, cap, hdr);
    pos += gen_strategy (tc, buf + pos, cap - pos, depth + 1);
    pos = append (buf, pos, cap, ":");
    pos += gen_strategy (tc, buf + pos, cap - pos, depth + 1);
    pos = append (buf, pos, cap, ";");
  }

  buf[pos] = '\0';
  return pos;
}

/* ---- Layer 1: function under test ---- */

static int
has_conditional (const char * s)
{
  return strchr (s, '/') != NULL;
}

/* ---- Layer 2: hegel test ---- */

/* FAIL: reject any strategy that contains a conditional.
** Hegel finds a minimal conditional and shrinks to it. */
static void
test_no_conditional (hegel_testcase * tc)
{
  char buf[BUF_SZ];
  gen_strategy (tc, buf, BUF_SZ, 0);

  hegel_note (tc, buf);

  HEGEL_ASSERT (!has_conditional (buf),
                "strategy has conditional: \"%s\"", buf);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  hegel_run_test (test_no_conditional);
  return (0);
}
