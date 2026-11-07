/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Layer 1 of every pair: the functions under test, each with a known
** bug, plus printers for the inputs.  Shared verbatim by the raw_*.c and
** hc_*.c programs and not counted as test code.
*/
#ifndef LVH_SUT_H
#define LVH_SUT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wunused-function"   /* each program uses one pair */

/* ---- 1. minimal: a 5-slot buffer (fits n items only for n < 5) ---- */

#define BUF_SLOTS 5

static inline bool buffer_fits (int n) { return n < BUF_SLOTS; }

/* ---- 2. crash: name lookup returns NULL past the table, the caller
**      dereferences it (SIGSEGV for id >= 42) ---- */

#define N_NAMES 42

__attribute__((noinline)) static const char * name_of (int id)
{
  static const char * names[N_NAMES] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n",
    "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "aa", "ab",
    "ac", "ad", "ae", "af", "ag", "ah", "ai", "aj", "ak", "al", "am", "an",
    "ao", "ap",
  };
  return id < N_NAMES ? names[id] : NULL;
}

__attribute__((noinline)) static size_t name_len (int id)
{
  const char * volatile s = name_of (id);     /* bug: no NULL check */
  size_t n = 0;
  while (s[n]) n++;
  return n;
}

/* ---- 3. graph: adjacency builder with a fixed 3-slot row that
**      silently drops neighbours past the third ---- */

#define MAX_VERT 20
#define MAX_EDGE 40
#define ADJ_MAX   3

typedef struct { int u, v; } edge;

typedef struct {
  int    nvert;
  int    nedge;
  edge * edges;
} graph;

typedef struct {
  int deg[MAX_VERT];
  int adj[MAX_VERT][ADJ_MAX];
} adjacency;

/* Returns the number of adjacency entries stored: 2 per edge expected
** (a self-loop lists its vertex twice). */
static int adj_build (const graph * g, adjacency * a)
{
  int total = 0;
  memset (a, 0, sizeof *a);
  for (int i = 0; i < g->nedge; i++) {
    int ends[2][2] = { { g->edges[i].u, g->edges[i].v }, { g->edges[i].v, g->edges[i].u } };
    for (int k = 0; k < 2; k++) {
      int x = ends[k][0];
      if (a->deg[x] < ADJ_MAX) {               /* bug: drops the rest */
        a->adj[x][a->deg[x]++] = ends[k][1];
        total++;
      }
    }
  }
  return total;
}

static void graph_print (FILE * out, const graph * g)
{
  fprintf (out, "graph: nvert=%d nedge=%d edges=[", g->nvert, g->nedge);
  for (int i = 0; i < g->nedge; i++)
    fprintf (out, "%s(%d,%d)", i ? " " : "", g->edges[i].u, g->edges[i].v);
  fprintf (out, "]\n");
}

/* ---- 4. tree: expression simplifier; rewrites 0 - b to b (should be
**      -b) ---- */

typedef struct expr {
  char          op;      /* 0: leaf, '+', '-' */
  int           val;
  struct expr * a;
  struct expr * b;
} expr;

static long expr_eval (const expr * e)
{
  if (!e->op) return e->val;
  long x = expr_eval (e->a), y = expr_eval (e->b);
  return e->op == '+' ? x + y : x - y;
}

static expr * expr_simplify (expr * e)
{
  if (!e->op) return e;
  e->a = expr_simplify (e->a);
  e->b = expr_simplify (e->b);
  if (!e->b->op && e->b->val == 0) return e->a;                 /* x +- 0 -> x */
  if (!e->a->op && e->a->val == 0) return e->b;                 /* bug for '-' */
  return e;
}

static int expr_nodes (const expr * e) { return e->op ? 1 + expr_nodes (e->a) + expr_nodes (e->b) : 1; }

static void expr_print (FILE * out, const expr * e)
{
  if (!e->op) { fprintf (out, "%d", e->val); return; }
  fputc ('(', out);
  expr_print (out, e->a);
  fprintf (out, " %c ", e->op);
  expr_print (out, e->b);
  fputc (')', out);
}

/* ---- 5. two bugs: midpoint of [lo, hi] ----
** bug A: lo + hi wraps for large values (result outside [lo, hi]);
** bug B: C division rounds toward zero, so for a negative odd sum the
**        result is one above floor((lo + hi) / 2). */

static int midpoint (int lo, int hi)
{
  return (int) ((unsigned) lo + (unsigned) hi) / 2;
}

static int64_t floor_half (int64_t s) { return s >= 0 ? s / 2 : -((-s + 1) / 2); }

#endif
