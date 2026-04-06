/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Grammar-based strategy fuzzer: generate random Scotch-like strategy
** strings using hegel draws, validate them with a recursive descent
** parser.  Exercises composable generation on recursive structures.
**
** Simplified Scotch strategy grammar:
**   strat   := method | strat ' ' strat | '(' strat '|' strat ')'
**            | '/' cvar '>' num '?' strat ':' strat ';'
**   method  := [bfhmrsx]
**   cvar    := "vert" | "edge" | "levl"
**
** Three method tables (mapping: 7, separation: 8, ordering: 9) use
** letters from the same alphabet — we use the mapping set here.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "hegel_c.h"

#define BUF_SZ 256

/* ---- Layer 1: strategy string generator ---- */

static const char METHODS[] = "bfhmrsx";
static const int  N_METHODS = 7;
static const char * CVARS[] = { "vert", "edge", "levl" };

static int gen_strategy (hegel_testcase * tc, char * buf, int cap, int depth);

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

  /* At max depth, always produce a leaf method. */
  if (depth >= 4)
    kind = 0;
  else
    kind = hegel_draw_int (tc, 0, 3);
    /* 0=method, 1=sequence, 2=best-of, 3=conditional */

  if (kind == 0) {
    /* method */
    int idx = hegel_draw_int (tc, 0, N_METHODS - 1);
    char m[2] = { METHODS[idx], '\0' };
    pos = append (buf, pos, cap, m);
  } else if (kind == 1) {
    /* sequence: A B */
    pos = gen_strategy (tc, buf + pos, cap - pos, depth + 1);
    pos = append (buf, pos, cap, " ");
    pos += gen_strategy (tc, buf + pos, cap - pos, depth + 1);
  } else if (kind == 2) {
    /* best-of: (A|B) */
    pos = append (buf, pos, cap, "(");
    pos += gen_strategy (tc, buf + pos, cap - pos, depth + 1);
    pos = append (buf, pos, cap, "|");
    pos += gen_strategy (tc, buf + pos, cap - pos, depth + 1);
    pos = append (buf, pos, cap, ")");
  } else {
    /* conditional: /cvar>N?A:B; */
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

/* ---- Layer 1: recursive descent validator ---- */

static int parse_strat (const char * s, int pos, int len);

static int
parse_method (const char * s, int pos, int len)
{
  if (pos >= len) return -1;
  if (strchr (METHODS, s[pos]))
    return pos + 1;
  return -1;
}

static int
parse_number (const char * s, int pos, int len)
{
  int start = pos;
  while (pos < len && s[pos] >= '0' && s[pos] <= '9')
    pos++;
  return pos > start ? pos : -1;
}

static int
parse_cvar (const char * s, int pos, int len)
{
  if (pos + 4 <= len && strncmp (s + pos, "vert", 4) == 0) return pos + 4;
  if (pos + 4 <= len && strncmp (s + pos, "edge", 4) == 0) return pos + 4;
  if (pos + 4 <= len && strncmp (s + pos, "levl", 4) == 0) return pos + 4;
  return -1;
}

/* Parse a single atom: method, (A|B), or /cvar>N?A:B; */
static int
parse_atom (const char * s, int pos, int len)
{
  int p;

  if (pos >= len) return -1;

  /* conditional: /cvar>N?strat:strat; */
  if (s[pos] == '/') {
    p = parse_cvar (s, pos + 1, len);
    if (p < 0 || p >= len || s[p] != '>') return -1;
    p = parse_number (s, p + 1, len);
    if (p < 0 || p >= len || s[p] != '?') return -1;
    p = parse_strat (s, p + 1, len);
    if (p < 0 || p >= len || s[p] != ':') return -1;
    p = parse_strat (s, p + 1, len);
    if (p < 0 || p >= len || s[p] != ';') return -1;
    return p + 1;
  }

  /* best-of: (A|B) */
  if (s[pos] == '(') {
    p = parse_strat (s, pos + 1, len);
    if (p < 0 || p >= len || s[p] != '|') return -1;
    p = parse_strat (s, p + 1, len);
    if (p < 0 || p >= len || s[p] != ')') return -1;
    return p + 1;
  }

  /* method */
  return parse_method (s, pos, len);
}

/* Parse a strategy: atoms separated by spaces (sequence). */
static int
parse_strat (const char * s, int pos, int len)
{
  pos = parse_atom (s, pos, len);
  if (pos < 0) return -1;
  while (pos < len && s[pos] == ' ') {
    int next = parse_atom (s, pos + 1, len);
    if (next < 0) break;  /* trailing space not part of strategy */
    pos = next;
  }
  return pos;
}

static int
validate_strategy (const char * s)
{
  int len = (int) strlen (s);
  int end = parse_strat (s, 0, len);
  return (end == len) ? 1 : 0;
}

/* ---- Layer 2: hegel tests ---- */

/* PASS: all generated strategies are syntactically valid. */
static void
test_gen_valid (hegel_testcase * tc)
{
  char buf[BUF_SZ];
  gen_strategy (tc, buf, BUF_SZ, 0);
  HEGEL_ASSERT (validate_strategy (buf),
                "invalid strategy: \"%s\"", buf);
}

/* ---- Layer 3: runner ---- */

int
main (void)
{
  hegel_run_test (test_gen_valid);
  return (0);
}
