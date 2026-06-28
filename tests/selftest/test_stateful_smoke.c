/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful smoke test: a correct bounded integer stack driven by the
** hegel_state_machine_* API.  No bug — every command sequence keeps
** the invariant, so the run PASSES (exit 0).  Proves the stateful
** loop wires up setup/teardown, rules, and invariants.  Models the
** canonical stack from CLAUDE.md.
**
** Three layers:
**   1. Function under test  — stk_push / stk_pop (a real bounded stack)
**   2. Hegel test           — rules drive it, invariant checks bounds
**   3. Makefile runner      — TESTS_PASS, must exit 0 */

#include "hegel_c.h"

#include <stdlib.h>

#define STK_CAP 64

typedef struct {
  int data[STK_CAP];
  int len;
} stk_t;

/* ---- Layer 1: the bounded stack under test (correct) ---- */

static void stk_push (stk_t * s, int v)
{
  if (s->len < STK_CAP) s->data[s->len++] = v;
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
  stk_push ((stk_t *) state, hegel_draw_int (tc, 0, 999));
}

static void rule_pop (void * state, hegel_testcase * tc)
{
  (void) tc;
  stk_pop ((stk_t *) state);
}

static void inv_len_in_range (const void * state, hegel_testcase * tc)
{
  (void) tc;
  const stk_t * s = state;
  HEGEL_ASSERT (s->len >= 0 && s->len <= STK_CAP,
                "stack len out of range: %d (cap %d)", s->len, STK_CAP);
}

int main (void)
{
  hegel_state_machine * sm = hegel_state_machine_new (sm_setup, sm_teardown);
  hegel_state_machine_add_rule      (sm, "push", rule_push);
  hegel_state_machine_add_rule      (sm, "pop",  rule_pop);
  hegel_state_machine_add_invariant (sm, "len_in_range", inv_len_in_range);
  hegel_state_machine_run_n         (sm, 200);
  hegel_state_machine_free          (sm);
  return 0;
}
