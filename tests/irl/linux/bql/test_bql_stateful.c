/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Stateful PBT for Linux kernel lib/dynamic_queue_limits.c (BQL).
**
** BQL is a numerical accounting state machine: dql_queued / dql_completed
** track byte counts on a queue and BQL adjusts an internal `limit` so
** the queue stays just-full-enough to avoid starvation.  Bug-class is
** ordering-sensitive arithmetic: weird (queue, complete, queue, ...)
** sequences can violate documented invariants if the math is wrong.
**
** Rules:
**   - rule_queue(N)     : call dql_queued(dql, N), shadow.outstanding += N
**   - rule_complete(N)  : precondition outstanding > 0; dql_completed(dql, N)
**   - rule_advance_time : tick g_fake_jiffies (exercises stall path)
**
** Invariants (after every rule):
**   - num_queued >= num_completed (no over-completion)
**   - outstanding shadow == num_queued - num_completed
**   - dql_avail(dql) == (int)(adj_limit - num_queued)
**   - limit ∈ [min_limit, max_limit]
**   - limit <= DQL_MAX_LIMIT
*/

#include <linux/dynamic_queue_limits.h>
#include "hegel_c.h"

/* The fake jiffies counter — referenced by bql_compat.h's #define jiffies. */
volatile unsigned long g_fake_jiffies = 0;

/* Shadow model maintained alongside the dql struct. */
typedef struct {
  struct dql        dql;
  unsigned long long outstanding;       /* expected num_queued - num_completed */
  unsigned long long total_queued;      /* shadow of num_queued */
  unsigned long long total_completed;   /* shadow of num_completed */
} bql_state_t;

static void *
setup (void)
{
  bql_state_t * s = calloc (1, sizeof *s);
  /* hold_time = 1 jiffy → exercises the slack path quickly. */
  dql_init (&s->dql, 1);
  /* Pin a max_limit large enough that dql_completed can grow limit. */
  s->dql.max_limit = DQL_MAX_LIMIT;
  s->dql.min_limit = 0;
  /* Need adj_limit > 0 so the first dql_queued can be observed
  ** as "available" if test wants — leave at default (0). */
  return s;
}

static void
teardown (void * st)
{
  free (st);
}

/* -------- rules -------- */

/* Bound N to DQL_MAX_OBJECT - 1 so dql_queued doesn't WARN_ON_ONCE-and-skip.
** That ceiling is ~268M — plenty of headroom for the property to be
** meaningful, while keeping the input space numerically tractable for
** shrinking. */
#define MAX_DRAW (DQL_MAX_OBJECT - 1)

static void
rule_queue (void * state, hegel_testcase * tc)
{
  bql_state_t * s = state;
  /* FRICTION: I want hegel to draw from [1, min(MAX_DRAW, headroom_until_overflow)]
  ** but the byte stream needs to be shrink-friendly.  Today: draw uniformly,
  ** then clamp.  Wasted bits when N is much larger than realistic. */
  unsigned int n = (unsigned int) hegel_draw_int (tc, 1, 1024);
  dql_queued (&s->dql, n);
  s->outstanding += n;
  s->total_queued += n;
}

static bool
can_complete (const void * state)
{
  const bql_state_t * s = state;
  return s->outstanding > 0;
}

static void
rule_complete (void * state, hegel_testcase * tc)
{
  bql_state_t * s = state;
  /* outstanding > 0 by precondition.  Clamp draw to outstanding. */
  unsigned int hi = s->outstanding > 1024 ? 1024 : (unsigned int) s->outstanding;
  unsigned int n = (unsigned int) hegel_draw_int (tc, 1, (int) hi);
  dql_completed (&s->dql, n);
  s->outstanding -= n;
  s->total_completed += n;
}

static void
rule_advance_time (void * state, hegel_testcase * tc)
{
  (void) state;
  /* Tick by a draw — exercises the slack-hold-time and stall paths. */
  unsigned long delta = (unsigned long) hegel_draw_int (tc, 1, 100);
  g_fake_jiffies += delta;
}

/* -------- invariants -------- */

static void
inv_completed_le_queued (const void * st, hegel_testcase * tc)
{
  (void) tc;
  const bql_state_t * s = st;
  HEGEL_ASSERT (s->dql.num_completed <= s->dql.num_queued,
                "num_completed=%u > num_queued=%u",
                s->dql.num_completed, s->dql.num_queued);
}

static void
inv_outstanding_matches_shadow (const void * st, hegel_testcase * tc)
{
  (void) tc;
  const bql_state_t * s = st;
  unsigned int diff = s->dql.num_queued - s->dql.num_completed;
  HEGEL_ASSERT ((unsigned long long) diff == s->outstanding,
                "outstanding mismatch: dql=%u shadow=%llu (Q=%u C=%u)",
                diff, s->outstanding,
                s->dql.num_queued, s->dql.num_completed);
}

static void
inv_avail_formula (const void * st, hegel_testcase * tc)
{
  (void) tc;
  const bql_state_t * s = st;
  int avail = dql_avail (&s->dql);
  int expected = (int) (s->dql.adj_limit - s->dql.num_queued);
  HEGEL_ASSERT (avail == expected,
                "dql_avail=%d != adj_limit(%u)-num_queued(%u)=%d",
                avail, s->dql.adj_limit, s->dql.num_queued, expected);
}

static void
inv_limit_in_bounds (const void * st, hegel_testcase * tc)
{
  (void) tc;
  const bql_state_t * s = st;
  HEGEL_ASSERT (s->dql.limit >= s->dql.min_limit,
                "limit=%u < min_limit=%u",
                s->dql.limit, s->dql.min_limit);
  HEGEL_ASSERT (s->dql.limit <= s->dql.max_limit,
                "limit=%u > max_limit=%u",
                s->dql.limit, s->dql.max_limit);
  HEGEL_ASSERT (s->dql.limit <= DQL_MAX_LIMIT,
                "limit=%u > DQL_MAX_LIMIT=%u",
                s->dql.limit, (unsigned) DQL_MAX_LIMIT);
}

int
main (void)
{
  fprintf (stderr, "BQL stateful PBT — 200 cases, nofork mode\n");
  hegel_state_machine * sm = hegel_state_machine_new (setup, teardown);
  hegel_state_machine_add_rule                   (sm, "queue",        rule_queue);
  hegel_state_machine_add_rule_with_precondition (sm, "complete",
                                                  can_complete, rule_complete);
  hegel_state_machine_add_rule                   (sm, "advance_time", rule_advance_time);
  hegel_state_machine_add_invariant              (sm, "completed_le_queued",
                                                  inv_completed_le_queued);
  hegel_state_machine_add_invariant              (sm, "outstanding_matches",
                                                  inv_outstanding_matches_shadow);
  hegel_state_machine_add_invariant              (sm, "avail_formula",
                                                  inv_avail_formula);
  hegel_state_machine_add_invariant              (sm, "limit_in_bounds",
                                                  inv_limit_in_bounds);
  hegel_state_machine_run_n                      (sm, 200);
  hegel_state_machine_free                       (sm);
  fprintf (stderr, "BQL stateful PBT: PASSED\n");
  return 0;
}
