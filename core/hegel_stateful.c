/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Stateful PBT: command-sequence testing.  Pure-C port of the run
** loop previously in rust-version/src/stateful.rs.
**
** Why we own the loop instead of using an upstream stateful runner:
** preconditions.  Rules whose precondition returns false are excluded
** BEFORE the rule-index draw, so non-applicable rules are invisible
** to the byte stream and don't pollute counterexample traces.  (The
** first asn1 SEQUENCE subagent, 2026-04-30, showed why: close_X rules
** that only fire when the top frame matches produced 0 completed
** trees in 20 cases under equiprobable selection.)
**
** Rule-level escapes: a rule body calling hegel_assume(false) is
** retried with another rule; a draw hitting the engine's choice
** budget ends the case.  Both are caught here via tc->rule_escape —
** the C analogue of stateful.rs's catch_unwind + panic-message match.
** Real failures (HEGEL_ASSERT / crashes) propagate to the case level.
**
** V0 limits (unchanged): 32 rules, 32 invariants, nofork only.
*/

#include "hegel_internal.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SLOTS 32
#define MAX_STEPS 50

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

/* ---- The run loop ---- */

/* The state machine under test for the current run — the run driver's
** body callback has no closure context in C. */
static hegel_state_machine * g_active_sm = NULL;

static void check_all_invariants (hegel_state_machine * sm, void * state,
                                  hegel_testcase * tc)
{
  for (int i = 0; i < sm->n_invs; i++)
    sm->inv_fns[i] (state, tc);
}

static void run_one_case (hegel_testcase * tc)
{
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

  hegel_note (tc, "Initial invariant check.");
  check_all_invariants (sm, state, tc);

  /* Step cap: an unbounded draw capped at MAX_STEPS, mirroring the
  ** upstream loop (lets the shrinker pull the sequence length down). */
  tc->silent = 1;
  int64_t unbounded_cap = hegel_draw_i64 (tc, 1, INT64_MAX);
  tc->silent = 0;
  int64_t step_cap = unbounded_cap < MAX_STEPS ? unbounded_cap : MAX_STEPS;

  int64_t steps_ok = 0;
  int64_t steps_attempted = 0;
  int64_t step = 0;
  int stopped = 0;

  while (!stopped
         && steps_ok < step_cap
         && (steps_attempted < 10 * step_cap
             || (steps_ok == 0 && steps_attempted < 1000))) {
    step++;

    int applicable[MAX_SLOTS];
    int n_app = 0;
    for (int i = 0; i < sm->n_rules; i++) {
      bool ok = sm->rule_preconds[i] == NULL
              || sm->rule_preconds[i] (state);
      if (ok) applicable[n_app++] = i;
    }

    if (n_app == 0) {
      hegel_note (tc, "No applicable rule from current state — ending case.");
      break;
    }

    int chosen_in_app = 0;
    if (n_app > 1) {
      tc->silent = 1;
      chosen_in_app = hegel_draw_int (tc, 0, n_app - 1);
      tc->silent = 0;
    }
    int rule_idx = applicable[chosen_in_app];

    if (tc->final_replay)
      printf ("Step %lld: %s\n", (long long)step, sm->rule_names[rule_idx]);

    /* Rule-level catch: ASSUME retries with another rule, STOP ends
    ** the case.  FAIL never lands here — hegel__escape routes it to
    ** the case level. */
    jmp_buf rule_env;
    jmp_buf * saved = tc->rule_escape;
    tc->rule_escape = &rule_env;

    int esc = setjmp (rule_env);
    if (esc == 0) {
      sm->rule_fns[rule_idx] (state, tc);
      tc->rule_escape = saved;
      steps_attempted++;
      steps_ok++;
      check_all_invariants (sm, state, tc);
    } else {
      tc->rule_escape = saved;
      steps_attempted++;
      if (esc == HEGEL_ESC_STOP) stopped = 1;
      /* HEGEL_ESC_ASSUME: silently retry with another rule. */
    }
  }

  /* Teardown is the driver-run tc->cleanup hook — not called here, so
  ** it also covers FAIL escapes that never return to this frame.
  **
  ** A STOP mid-rule means the engine's budget is gone; end the case
  ** through the normal overrun path (cleanup still runs). */
  if (stopped) hegel__escape (tc, HEGEL_ESC_STOP);
}

void hegel_state_machine_run_n (hegel_state_machine * sm, uint64_t n_cases)
{
  g_active_sm = sm;
  int rc = hegel__run_property_nofork (run_one_case, n_cases);
  g_active_sm = NULL;
  if (rc) exit (1);
}
