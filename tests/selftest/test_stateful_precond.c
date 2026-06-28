/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful precondition test: an open/close bracket-nesting machine.
**
** The point is precondition FILTERING — `close` carries a precondition
** (depth > 0), so the runner EXCLUDES it from rule selection when no
** bracket is open, rather than picking it and having it no-op.  The
** close rule body therefore asserts depth > 0 on entry: if precondition
** filtering works, that assert can never fire and the run PASSES.
**
** Mirrors the asn1 SEQUENCE case that motivated preconditions: close_X
** rules that only apply when an X is open.  Without filtering, close
** would be drawn against an empty stack and pollute the byte stream /
** trace; with filtering it's invisible until applicable.
**
** Three layers:
**   1. Function under test  — brk_open / brk_close (a depth-bounded nest)
**   2. Hegel test           — close gated by a precondition
**   3. Makefile runner      — TESTS_PASS, must exit 0 */

#include "hegel_c.h"

#include <stdbool.h>
#include <stdlib.h>

#define BRK_MAX 16

typedef struct {
  int depth;
} brk_t;

/* ---- Layer 1: the bracket nest under test ---- */

static void brk_open (brk_t * b)
{
  if (b->depth < BRK_MAX) b->depth++;
}

static void brk_close (brk_t * b)
{
  b->depth--;   /* caller guarantees depth > 0 (precondition) */
}

/* ---- Layer 2: the state machine ---- */

static void * sm_setup (void)
{
  return calloc (1, sizeof (brk_t));
}

static void sm_teardown (void * state)
{
  free (state);
}

static void rule_open (void * state, hegel_testcase * tc)
{
  (void) tc;
  brk_open ((brk_t *) state);
}

/* close is only applicable when something is open. */
static bool pre_close (const void * state)
{
  return ((const brk_t *) state)->depth > 0;
}

static void rule_close (void * state, hegel_testcase * tc)
{
  (void) tc;
  brk_t * b = state;
  /* If precondition filtering is honored, depth is always > 0 here. */
  HEGEL_ASSERT (b->depth > 0,
                "close selected with no open bracket (precondition ignored)");
  brk_close (b);
}

static void inv_depth_in_range (const void * state, hegel_testcase * tc)
{
  (void) tc;
  const brk_t * b = state;
  HEGEL_ASSERT (b->depth >= 0 && b->depth <= BRK_MAX,
                "bracket depth out of range: %d", b->depth);
}

int main (void)
{
  hegel_state_machine * sm = hegel_state_machine_new (sm_setup, sm_teardown);
  hegel_state_machine_add_rule (sm, "open", rule_open);
  hegel_state_machine_add_rule_with_precondition (sm, "close", pre_close, rule_close);
  hegel_state_machine_add_invariant (sm, "depth_in_range", inv_depth_in_range);
  hegel_state_machine_run_n (sm, 200);
  hegel_state_machine_free (sm);
  return 0;
}
