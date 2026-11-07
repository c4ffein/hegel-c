/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Pair 4, libhegel only: an expression tree ('+' / '-' branches, leaves
** 0..9) drawn with the raw recursion protocol (max depth 50, max leaves
** 100, as hegel-c's defaults), a span per node; expr_simplify must keep
** the value.  An attempt that outgrows the budget (HEGEL_E_RETRY from
** recursion_leaf) unwinds up the C stack and is retried from the root;
** one the engine rejects as mispriced (HEGEL_E_RETRY from
** recursion_finish) restarts without the retry call.  Nodes come from a
** per-case arena freed once at the end: abandoned attempts, and the
** nodes expr_simplify unlinks, would otherwise leak.
** Lines between B{ and }B markers are boilerplate.
*/
#include "raw_check.h"
#include "common.h"
#include "sut.h"

static uint64_t L_EXPR;

/* B{ */
typedef struct { expr ** p; size_t n; } arena;

static expr * arena_node (arena * ar)
{
  expr ** grown = realloc (ar->p, (ar->n + 1) * sizeof *grown);
  if (!grown) abort ();
  ar->p = grown;
  if (!(ar->p[ar->n] = calloc (1, sizeof (expr)))) abort ();
  return ar->p[ar->n++];
}

static void arena_free (arena * ar)
{
  for (size_t i = 0; i < ar->n; i++) free (ar->p[i]);
  free (ar->p);
}
/* }B */

/* One sub-value at `depth`.  Any non-OK code is passed up unchanged. */
static hegel_result_t gen_node (hegel_context_t * ctx, hegel_test_case_t * tc,
                                hegel_recursion_t * rec, arena * ar, uint64_t depth,
                                expr ** out)
{
  hegel_result_t rc;
  bool branch;
  int64_t x;

  if ((rc = hegel_start_span (ctx, tc, L_EXPR)) != HEGEL_OK) return rc;
  if ((rc = hegel_recursion_branch (ctx, tc, rec, depth, &branch)) != HEGEL_OK) return rc;
  expr * e = *out = arena_node (ar);
  if (!branch) {
    if ((rc = hegel_recursion_leaf (ctx, tc, rec)) != HEGEL_OK) return rc;  /* RETRY too */
    if ((rc = hegel_generate_integer (ctx, tc, 0, 9, &x)) != HEGEL_OK) return rc;
    e->val = (int) x;
  } else {
    if ((rc = hegel_generate_integer (ctx, tc, 0, 1, &x)) != HEGEL_OK) return rc;
    e->op = x ? '-' : '+';
    if ((rc = gen_node (ctx, tc, rec, ar, depth + 1, &e->a)) != HEGEL_OK) return rc;
    if ((rc = gen_node (ctx, tc, rec, ar, depth + 1, &e->b)) != HEGEL_OK) return rc;
  }
  return hegel_stop_span (ctx, tc, false);
}

static hegel_result_t gen_expr (hegel_context_t * ctx, hegel_test_case_t * tc, arena * ar,
                                expr ** out)
{
  hegel_recursion_t * rec;
  hegel_result_t rc = hegel_new_recursion (ctx, tc, 50, 100, &rec);
  if (rc != HEGEL_OK) return rc;
  for (;;) {
    rc = gen_node (ctx, tc, rec, ar, 0, out);
    if (rc == HEGEL_OK) {
      rc = hegel_recursion_finish (ctx, tc, rec);
      if (rc == HEGEL_E_RETRY) continue;          /* mispriced: again, no retry call */
      break;                                      /* OK (keep it) or STOP_TEST */
    }
    if (rc != HEGEL_E_RETRY) break;               /* STOP_TEST */
    if ((rc = hegel_recursion_retry (ctx, tc, rec)) != HEGEL_OK) break;   /* ASSUME / STOP */
  }
  hegel_recursion_free (ctx, rec);
  return rc;
}

static uint32_t prop (hegel_context_t * ctx, hegel_test_case_t * tc, bool show,
                      const char ** origin)
{
  count_exec ();
  arena ar = { 0 };
  expr * e = NULL;
  hegel_result_t rc = gen_expr (ctx, tc, &ar, &e);
  /* B{ */
  if (rc != HEGEL_OK) {
    arena_free (&ar);
    return rc == HEGEL_E_ASSUME ? HEGEL_STATUS_INVALID : HEGEL_STATUS_OVERRUN;
  }
  /* }B */
  if (show) { printf ("expr = "); expr_print (stdout, e); printf ("\n"); }
  long before = expr_eval (e);
  long after = expr_eval (expr_simplify (e));
  /* B{ */
  arena_free (&ar);
  /* }B */
  if (before != after) {
    if (show) printf ("value %ld, simplified %ld\n", before, after);
    *origin = "expr_simplify keeps the value";
    return HEGEL_STATUS_INTERESTING;
  }
  return HEGEL_STATUS_VALID;
}

/* B{ */
int main (void)
{
  hegel_context_t * ctx = hegel_context_new ();
  CHECK (hegel_label_from_name, ctx, STR ("lvh.expr"), &L_EXPR);
  hegel_settings_t * s;
  CHECK (hegel_settings_new, ctx, &s);
  CHECK (hegel_settings_set_test_cases, ctx, s, 100);
  CHECK (hegel_settings_set_database, ctx, s, &STR (""));

  hegel_run_t * run;
  CHECK (hegel_run_start, ctx, s, NULL, NULL, &run);
  for (;;) {
    hegel_test_case_t * tc;
    CHECK (hegel_next_test_case, ctx, run, &tc);
    if (!tc) break;
    const char * origin = NULL;
    uint32_t st = prop (ctx, tc, false, &origin);
    CHECK (hegel_mark_complete, ctx, tc, st, origin ? &STR (origin) : NULL);
    CHECK (hegel_test_case_free, ctx, tc);
  }

  hegel_run_result_t * r;
  hegel_run_status_t status;
  size_t nf;
  CHECK (hegel_run_result, ctx, run, &r);
  CHECK (hegel_run_result_status, ctx, r, &status);
  if (status == HEGEL_RUN_STATUS_ERROR) {
    const hegel_string_t * err;
    CHECK (hegel_run_result_error, ctx, r, &err);
    fprintf (stderr, "run error: %.*s\n", (int) err->len, err->data);
  }
  CHECK (hegel_run_result_failure_count, ctx, r, &nf);
  for (size_t i = 0; i < nf; i++) {
    hegel_failure_t * f;
    hegel_string_t origin;
    const hegel_string_t * blob;
    CHECK (hegel_run_result_failure, ctx, r, i, &f);
    CHECK (hegel_failure_origin, ctx, f, &origin);
    CHECK (hegel_failure_reproduction_blob, ctx, f, &blob);
    printf ("FAILED: %.*s\n", (int) origin.len, origin.data);
    if (blob) {
      hegel_test_case_t * tc;
      const char * o = NULL;
      CHECK (hegel_test_case_from_blob, ctx, s, *blob, NULL, NULL, &tc);
      uint32_t st = prop (ctx, tc, true, &o);
      CHECK (hegel_mark_complete, ctx, tc, st, o ? &STR (o) : NULL);
      CHECK (hegel_test_case_free, ctx, tc);
      printf ("blob: %.*s\n", (int) blob->len, blob->data);
    }
    CHECK (hegel_failure_free, ctx, f);
  }
  CHECK (hegel_run_result_free, ctx, r);
  CHECK (hegel_run_free, ctx, run);
  CHECK (hegel_settings_free, ctx, s);
  CHECK (hegel_context_free, ctx);
  return status == HEGEL_RUN_STATUS_PASSED ? 0 : 1;
}
/* }B */
