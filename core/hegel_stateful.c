/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Stateful PBT: command-sequence testing on the engine's state machine
** (hegel_new_state_machine).  The engine owns rule selection — swarm
** testing, the step budget, the stop decision — so sequences shrink
** like every other Hegel frontend's; this file is the C driver loop,
** shaped like hegel-rust's run_sequential.
**
** Preconditions map onto rule rejection: when the engine picks a rule
** whose precondition is false, the rule isn't run and the pick is
** reported as rejected (it doesn't count toward the step budget).  A
** rule body calling hegel_assume(false) is rejected the same way, via
** the rule-level escape.  Real failures (HEGEL_ASSERT / crashes)
** propagate to the case level.
**
** Every engine call is a wire request, so this works unchanged in fork
** mode (the default) and nofork mode.
*/

#include "hegel_internal.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SLOTS  64
#define STEP_COUNT 50

struct CStateMachine {
  void * (*setup)    (void);
  void   (*teardown) (void * state);
  const char * rule_names[MAX_SLOTS];
  void  (*rule_fns[MAX_SLOTS]) (void * state, hegel_testcase * tc);
  bool  (*rule_preconds[MAX_SLOTS]) (const void * state);
  int    n_rules;
  const char * inv_names[MAX_SLOTS];
  void  (*inv_fns[MAX_SLOTS]) (const void * state, hegel_testcase * tc);
  int    n_invs;
};

hegel_state_machine * hegel_state_machine_new (
    void * (*setup)    (void),
    void   (*teardown) (void * state))
{
  hegel_state_machine * sm = calloc (1, sizeof *sm);
  if (!sm) abort ();
  sm->setup = setup;
  sm->teardown = teardown;
  return sm;
}

void hegel_state_machine_free (hegel_state_machine * sm)
{
  free (sm);
}

static int add_rule_internal (hegel_state_machine * sm, const char * name,
                              bool (*precond)(const void *),
                              void (*fn)(void *, hegel_testcase *))
{
  if (sm->n_rules >= MAX_SLOTS) return -1;
  sm->rule_names[sm->n_rules] = name;
  sm->rule_fns[sm->n_rules] = fn;
  sm->rule_preconds[sm->n_rules] = precond;
  sm->n_rules++;
  return 0;
}

int hegel_state_machine_add_rule (
    hegel_state_machine * sm, const char * name,
    void (*rule_fn)(void * state, hegel_testcase * tc))
{
  return add_rule_internal (sm, name, NULL, rule_fn);
}

int hegel_state_machine_add_rule_with_precondition (
    hegel_state_machine * sm, const char * name,
    bool (*precondition_fn)(const void * state),
    void (*rule_fn)(void * state, hegel_testcase * tc))
{
  return add_rule_internal (sm, name, precondition_fn, rule_fn);
}

int hegel_state_machine_add_invariant (
    hegel_state_machine * sm, const char * name,
    void (*invariant_fn)(const void * state, hegel_testcase * tc))
{
  if (sm->n_invs >= MAX_SLOTS) return -1;
  sm->inv_names[sm->n_invs] = name;
  sm->inv_fns[sm->n_invs] = invariant_fn;
  sm->n_invs++;
  return 0;
}

/* ---- Engine requests ---- */

static int64_t sm_create (hegel_testcase * tc, hegel_state_machine * sm)
{
  hegel_buf p;
  hegel__buf_init (&p);
  uint32_t nr = (uint32_t) sm->n_rules, ni = (uint32_t) sm->n_invs;
  int64_t steps = STEP_COUNT;
  HEGEL_BUF_PUT (&p, nr);
  HEGEL_BUF_PUT (&p, ni);
  HEGEL_BUF_PUT (&p, steps);
  for (int i = 0; i < sm->n_rules + sm->n_invs; i++) {
    const char * name = i < sm->n_rules ? sm->rule_names[i] : sm->inv_names[i - sm->n_rules];
    uint32_t l = (uint32_t) strlen (name);
    HEGEL_BUF_PUT (&p, l);
    hegel__buf_put (&p, name, l);
  }
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_SM_NEW, p.data, (uint32_t) p.len, &r, 0);
  int64_t id = 0;
  if (r.len >= sizeof id) memcpy (&id, r.data, sizeof id);
  hegel__buf_free (&p);
  hegel__buf_free (&r);
  return id;
}

static int64_t sm_i64 (hegel_testcase * tc, uint8_t op, int64_t id)
{
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, op, &id, sizeof id, &r, 0);
  int64_t v = 0;
  if (r.len >= sizeof v) memcpy (&v, r.data, sizeof v);
  hegel__buf_free (&r);
  return v;
}

static int sm_check_inv (hegel_testcase * tc, int64_t id, int64_t inv)
{
  int64_t p[2] = { id, inv };
  hegel_buf r;
  hegel__buf_init (&r);
  hegel__call (tc, HEGEL_OP_SM_CHECK_INV, p, sizeof p, &r, 0);
  int v = r.len >= 1 && r.data[0];
  hegel__buf_free (&r);
  return v;
}

/* ---- The run loop ---- */

/* The state machine under test for the current run — the run driver's
** body callback has no closure context in C. */
static hegel_state_machine * g_active_sm = NULL;

static void check_invariants (hegel_state_machine * sm, void * state, hegel_testcase * tc,
                              int64_t machine)
{
  for (int i = 0; i < sm->n_invs; i++)
    if (machine < 0 || sm_check_inv (tc, machine, i))
      sm->inv_fns[i] (state, tc);
}

/* Apply one rule; returns 0 if it was rejected by hegel_assume.  The
** rule-level escape only catches ASSUME — FAIL / STOP / HEALTH are
** routed to the case level by hegel__escape. */
static int run_rule (hegel_state_machine * sm, int rule, void * state, hegel_testcase * tc)
{
  jmp_buf rule_env;
  jmp_buf * saved = tc->rule_escape;
  tc->rule_escape = &rule_env;
  if (setjmp (rule_env) != 0) {
    tc->rule_escape = saved;
    if (hegel__draws_abandoned) hegel__draws_abandoned ();
    return 0;
  }
  sm->rule_fns[rule] (state, tc);
  tc->rule_escape = saved;
  return 1;
}

static void run_one_case (hegel_testcase * tc)
{
  static uint64_t rule_label = 0;
  if (!rule_label) rule_label = hegel_label ("hegel_c.stateful.rule");

  hegel_state_machine * sm = g_active_sm;
  if (sm->n_rules == 0) {
    hegel_health_fail ("hegel-c stateful: cannot run a machine with no rules");
    return;
  }

  void * state = sm->setup ();
  if (sm->teardown) {
    /* Driver-run cleanup: fires on every exit path, including a FAIL
    ** escaping from a rule or invariant. */
    tc->cleanup = sm->teardown;
    tc->cleanup_arg = state;
  }

  int64_t machine = sm_create (tc, sm);

  if (sm->n_invs) hegel_note (tc, "Checking invariants on the initial state.");
  check_invariants (sm, state, tc, -1);

  long step = 0;
  for (;;) {
    hegel_start_span (tc, rule_label);
    if (sm_i64 (tc, HEGEL_OP_SM_GROUP, machine) == HG_SM_DONE) {
      hegel_stop_span (tc, 0);
      break;
    }
    int round_rejected = 0;
    for (;;) {
      int64_t rule = sm_i64 (tc, HEGEL_OP_SM_RULE, machine);
      if (rule == HG_SM_DONE) break;
      if (rule < 0 || rule >= sm->n_rules) {
        hegel_health_fail ("hegel-c stateful: engine returned an out-of-range rule index");
        return;
      }
      if (sm->rule_preconds[rule] && !sm->rule_preconds[rule] (state)) {
        sm_i64 (tc, HEGEL_OP_SM_REJECTED, machine);
        round_rejected = 1;
        continue;
      }

      char line[256];
      snprintf (line, sizeof line, "Step %ld: %s", ++step, sm->rule_names[rule]);
      hegel_note (tc, line);

      if (!run_rule (sm, (int) rule, state, tc)) {
        sm_i64 (tc, HEGEL_OP_SM_REJECTED, machine);
        round_rejected = 1;
        hegel_note (tc, "  (rule stopped early: violated assumption)");
      }
    }
    hegel_stop_span (tc, round_rejected);
    check_invariants (sm, state, tc, machine);
  }

  if (sm->n_invs) hegel_note (tc, "Checking invariants on the final state.");
  check_invariants (sm, state, tc, -1);
  /* Teardown is the driver-run tc->cleanup hook; the machine handle is
  ** released with the case's server state. */
}

static int run_machine (hegel_state_machine * sm, uint64_t n_cases, int use_fork)
{
  g_active_sm = sm;
  int rc = hegel__run_property (run_one_case, n_cases, use_fork);
  g_active_sm = NULL;
  return rc;
}

void hegel_state_machine_run_n (hegel_state_machine * sm, uint64_t n_cases)
{
  if (run_machine (sm, n_cases, 1)) exit (1);
}

void hegel_state_machine_run_nofork_n (hegel_state_machine * sm, uint64_t n_cases)
{
  if (run_machine (sm, n_cases, 0)) exit (1);
}
