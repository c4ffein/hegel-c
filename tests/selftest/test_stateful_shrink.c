/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful shrink test: a BUGGY stack whose push ignores capacity.
**
** The runner must (a) discover a command sequence that drives len past
** the declared capacity and (b) shrink that SEQUENCE to its minimal
** form — STK_CAP+1 pushes with nothing else.  The invariant fires, so
** the run FAILS (exit non-zero); the final replay prints the minimal
** step list.
**
** This is the stateful analogue of the scalar shrink tests: it exercises
** sequence shrinking (dropping pops, simplifying operands) rather than
** value shrinking.
**
** Three layers:
**   1. Function under test  — buggy_push (no capacity guard) + pop
**   2. Hegel test           — invariant detects len > cap
**   3. Makefile runner      — TESTS_FAIL, must exit non-zero */

#include "hegel_c.h"

#include <stdlib.h>

#define STK_CAP 4
/* Backing store is generously oversized so the bug is a *logical*
** capacity-overflow (caught by the invariant), not a memory OOB —
** keeps this a clean TESTS_FAIL rather than a TESTS_CRASH. */
#define STK_BACKING 256

typedef struct {
  int data[STK_BACKING];
  int len;
} stk_t;

/* ---- Layer 1: the buggy stack under test ---- */

/* BUG: pushes without enforcing STK_CAP — len can exceed capacity. */
static void buggy_push (stk_t * s, int v)
{
  if (s->len < STK_BACKING) s->data[s->len++] = v;
}

static void stk_pop (stk_t * s)
{
  if (s->len > 0) s->len--;
}

/* ---- Layer 2: the state machine ---- */

static void * sm_setup (void)
{
  return calloc (1, sizeof (stk_t));
}

static void sm_teardown (void * state)
{
  free (state);
}

static void rule_push (void * state, hegel_testcase * tc)
{
  buggy_push ((stk_t *) state, hegel_draw_int (tc, 0, 9));
}

static void rule_pop (void * state, hegel_testcase * tc)
{
  (void) tc;
  stk_pop ((stk_t *) state);
}

static void inv_within_capacity (const void * state, hegel_testcase * tc)
{
  (void) tc;
  const stk_t * s = state;
  HEGEL_ASSERT (s->len <= STK_CAP,
                "stack overflowed capacity: len=%d > cap=%d", s->len, STK_CAP);
}

int main (void)
{
  hegel_state_machine * sm = hegel_state_machine_new (sm_setup, sm_teardown);
  hegel_state_machine_add_rule      (sm, "push", rule_push);
  hegel_state_machine_add_rule      (sm, "pop",  rule_pop);
  hegel_state_machine_add_invariant (sm, "within_capacity", inv_within_capacity);
  /* exits non-zero when the bug is found and shrunk. */
  hegel_state_machine_run_n         (sm, 500);
  hegel_state_machine_free          (sm);
  return 0;
}
