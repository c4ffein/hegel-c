/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: depth-exhaustion on HEGEL_SELF trips hegel_health_fail.
**
** Builds a simple recursive schema (Node → Node *) and draws it with
** max_depth=0 via hegel_schema_draw_at_n.  Any "present" draw on the
** HEGEL_SELF at depth 0 immediately calls hegel_health_fail, which
** emits "Health check failure: max recursion depth reached ..." to
** stderr and exits non-zero.
**
** This is the end-to-end verification that the new health-check
** plumbing works: the selftest Makefile's TESTS_HEALTH category
** greps stderr for "Health check failure" and only counts the test
** as passing if (a) exit code is non-zero and (b) that prefix is
** present in the output.
**
** Expected: EXIT non-zero, stderr contains "Health check failure:
**           max recursion depth reached".
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: the struct that carries the recursion ---- */

typedef struct Node {
  int                 val;
  struct Node *       next;
} Node;

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t  node_schema;

static
void
testDepthExhausted (
hegel_testcase *            tc)
{
  Node *               n = NULL;
  hegel_shape *        sh;

  /* max_depth=0 — any present=1 draw on HEGEL_SELF immediately
  ** triggers the depth health check.  Under 50/50 optional
  ** probabilities hegeltest hits present=1 within the first few
  ** cases, so the fail is reliable. */
  sh = hegel_schema_draw_at_n (tc, &n, node_schema, 0);

  /* Unreachable when the health check fires (panics / _exits).
  ** Kept so that if a random case happens to draw present=0 the
  ** case still completes cleanly. */
  hegel_shape_free (sh);
}

/* ---- Layer 3: runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  node_schema = HEGEL_STRUCT (Node,
      HEGEL_INT  (0, 100),
      HEGEL_SELF ());

  printf ("Testing that depth=0 + HEGEL_SELF trips health check...\n");
  hegel_run_test (testDepthExhausted);

  /* Only reached if NO case hit present=1 — astronomically unlikely,
  ** but surface it as a test failure so CI doesn't silently pass. */
  fprintf (stderr, "expected health check never fired — "
                   "re-run or bump n_cases\n");
  hegel_schema_free (node_schema);
  return (2);
}
