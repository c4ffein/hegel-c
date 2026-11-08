/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 4, lite libhegel: a +/- expression tree from the recursion protocol
** (leaves 0..9, depth 50, 100 leaves, a span per node, as in raw_tree.c);
** expr_simplify must keep the value.  Lite: nodes come from a fixed pool
** reset at each attempt, so an abandoned attempt needs no unwinding and
** nothing is freed.  The retry loop can't be shortened: a leaf over the
** budget returns RETRY (call recursion_retry, start again), a tree the
** engine prices out at finish returns RETRY too (start again, no retry
** call), and the two must not be mixed up.  See tree.md.
*/
#include <stdio.h>
#include "hegel.h"
#include "common.h"
#include "sut.h"

static expr pool[512];          /* 100 leaves: at most 199 nodes an attempt */
static int  used;

static hegel_result_t node (hegel_context_t * ctx, hegel_test_case_t * tc, hegel_recursion_t * rec,
                            uint64_t depth, expr ** out)
{
  hegel_result_t rc;
  bool branch;
  int64_t x;
  expr * e = *out = &pool[used++];
  if ((rc = hegel_start_span (ctx, tc, 1))
      || (rc = hegel_recursion_branch (ctx, tc, rec, depth, &branch))) return rc;
  if (!branch) {
    if ((rc = hegel_recursion_leaf (ctx, tc, rec))
        || (rc = hegel_generate_integer (ctx, tc, 0, 9, &x))) return rc;
    *e = (expr) { 0, (int) x, NULL, NULL };
  } else {
    if ((rc = hegel_generate_integer (ctx, tc, 0, 1, &x))) return rc;
    *e = (expr) { x ? '-' : '+', 0, NULL, NULL };
    if ((rc = node (ctx, tc, rec, depth + 1, &e->a))
        || (rc = node (ctx, tc, rec, depth + 1, &e->b))) return rc;
  }
  return hegel_stop_span (ctx, tc, false);
}

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, char * shown)
{
  count_exec ();
  hegel_recursion_t * rec;
  hegel_result_t rc;
  expr * e;
  if (hegel_new_recursion (ctx, tc, 50, 100, &rec)) return HEGEL_STATUS_OVERRUN;
  do {
    used = 0;
    rc = node (ctx, tc, rec, 0, &e);
    if (rc == HEGEL_OK) rc = hegel_recursion_finish (ctx, tc, rec);
    else if (rc == HEGEL_E_RETRY && (rc = hegel_recursion_retry (ctx, tc, rec)) == HEGEL_OK)
      rc = HEGEL_E_RETRY;
  } while (rc == HEGEL_E_RETRY);
  hegel_recursion_free (ctx, rec);
  if (rc) return rc == HEGEL_E_ASSUME ? HEGEL_STATUS_INVALID : HEGEL_STATUS_OVERRUN;
  FILE * m = fmemopen (shown, 8192, "w");
  fprintf (m, "expr = ");
  expr_print (m, e);
  fclose (m);
  long before = expr_eval (e);
  return expr_eval (expr_simplify (e)) == before ? HEGEL_STATUS_VALID : HEGEL_STATUS_INTERESTING;
}

/* B{ */
int main (void)
{
  hegel_context_t * ctx = hegel_context_new ();
  hegel_settings_t * s;
  hegel_run_t * run;
  hegel_test_case_t * tc;
  static char last[8192], shown[8192];
  hegel_settings_new (ctx, &s);
  hegel_settings_set_database (ctx, s, &(hegel_string_t) { "", 0 });
  hegel_run_start (ctx, s, NULL, NULL, &run);
  while (hegel_next_test_case (ctx, run, &tc) == HEGEL_OK && tc) {
    bool capture = false;
    hegel_test_case_should_capture (ctx, tc, &capture);
    uint32_t st = prop (ctx, tc, shown);
    if (capture && st == HEGEL_STATUS_INTERESTING) snprintf (last, sizeof last, "%s", shown);
    hegel_mark_complete (ctx, tc, st, NULL);
    hegel_test_case_free (ctx, tc);
  }
  hegel_run_result_t * r;
  hegel_run_status_t status;
  hegel_run_result (ctx, run, &r);
  hegel_run_result_status (ctx, r, &status);
  if (status == HEGEL_RUN_STATUS_FAILED) printf ("FAILED: %s\n", last);
  if (status == HEGEL_RUN_STATUS_ERROR) {
    const hegel_string_t * err;
    hegel_run_result_error (ctx, r, &err);
    printf ("run error: %.*s\n", (int) err->len, err->data);
  }
  return status == HEGEL_RUN_STATUS_PASSED ? 0 : 1;
}
/* }B */
