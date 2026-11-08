/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a failing repeat loop shrinks to the fewest iterations, in both
**       loop forms and both runners.
**
** EXPECTED_SHRINK: 5 5 5 5 iterations, nested 1x5
**
** Layer 1: a tally — add() values one at a time, sum() reads the total
**          back.  It keeps the values in a 4-slot window and has a
**          deliberate bug: the 5th add overwrites the oldest slot instead
**          of growing, so from the 5th call on sum() forgets a value.
** Layer 2: one property per (form, runner): in a repeat loop, draw a value,
**          add it, and check sum() against a running total.  The bug needs
**          at least 5 iterations and a non-zero value among the first
**          ones, so the minimal counterexample is exactly 5 iterations.
**          The iteration count of the last failing execution (the final
**          replay of the shrunk case) is recorded in shared memory, so a
**          fork-mode child can report it to main.
**            - HEGEL_REPEAT,      nofork
**            - HEGEL_REPEAT,      fork
**            - hegel_repeat_run,  nofork
**            - hegel_repeat_run,  fork
**          and a nested one (nofork): each outer iteration runs an inner
**          HEGEL_REPEAT on a fresh tally; minimal: 1 outer iteration
**          whose inner loop runs 5 times.  A failure inside the inner
**          loop is reported to both open loops, innermost first.
** Layer 3: prints "shrunk: A B C D iterations, nested OxI" for the
**          Makefile's TESTS_SHRINK check and exits 1 when all five runs
**          failed.
**
** Expected: EXIT non-zero with "shrunk: 5 5 5 5 iterations, nested 1x5".
*/
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

#define TALLY_SLOTS 4

typedef struct {
  long                slot[TALLY_SLOTS + 1];
  int                 n;                      /* values added so far */
} Tally;

static
void
tally_add (
Tally *                     t,
long                        v)
{
  if (t->n < TALLY_SLOTS) {
    t->slot[t->n ++] = v;
    return;
  }
  /* BUG: should keep every value; drops the oldest one instead. */
  t->slot[t->n % TALLY_SLOTS] = v;
  t->n ++;
}

static
long
tally_sum (
const Tally *               t)
{
  long                s = 0;
  int                 i;

  for (i = 0; i < TALLY_SLOTS && i < t->n; i ++)
    s += t->slot[i];
  return (s);
}

/* ---- Layer 2: hegel tests ---- */

static int *        g_last_iters;             /* shared with fork children */

typedef struct {
  Tally               t;
  long                total;
  int                 iters;
} Run;

static
void
step (
hegel_testcase *            tc,
void *                      ctx)
{
  Run *               r = ctx;
  long                v;

  v = hegel_draw_int (tc, 0, 100);
  r->iters ++;
  tally_add (&r->t, v);
  r->total += v;
  if (tally_sum (&r->t) != r->total)
    *g_last_iters = r->iters;
  HEGEL_ASSERT (tally_sum (&r->t) == r->total, "after %d adds: sum %ld, expected %ld",
                r->iters, tally_sum (&r->t), r->total);
}

static
void
testMacro (
hegel_testcase *            tc)
{
  Run                 r;

  memset (&r, 0, sizeof r);
  HEGEL_REPEAT (tc) {
    step (tc, &r);
  }
}

static
void
testRun (
hegel_testcase *            tc)
{
  Run                 r;

  memset (&r, 0, sizeof r);
  hegel_repeat_run (tc, step, &r);
}

static int *        g_last_outer;

static
void
testNested (
hegel_testcase *            tc)
{
  Run                 r;
  int                 outer = 0;

  HEGEL_REPEAT (tc) {
    outer ++;
    memset (&r, 0, sizeof r);
    HEGEL_REPEAT (tc) {
      *g_last_outer = outer;
      step (tc, &r);
    }
  }
}

/* ---- Layer 3: runner (see Makefile TESTS_SHRINK) ---- */

static
int
run (
void                     (* fn) (hegel_testcase *),
int                         use_fork)
{
  hegel_run_options   o;
  int                 failed;

  memset (&o, 0, sizeof o);
  o.test_cases = 1000;
  *g_last_iters = -1;
  failed = use_fork ? hegel_run_test_with (fn, &o) : hegel_run_test_nofork_with (fn, &o);
  return (failed ? *g_last_iters : -1);
}

int
main (
int                 argc,
char *              argv[])
{
  int                 got[4];
  int                 inner;

  (void) argc;
  (void) argv;

  g_last_iters = mmap (NULL, 2 * sizeof *g_last_iters, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (g_last_iters == MAP_FAILED) {
    perror ("mmap");
    return (0);                               /* TESTS_SHRINK: exit 0 is the failure */
  }
  g_last_outer = g_last_iters + 1;

  printf ("Testing that a failing repeat loop shrinks to the fewest iterations...\n");
  got[0] = run (testMacro, 0);
  got[1] = run (testMacro, 1);
  got[2] = run (testRun, 0);
  got[3] = run (testRun, 1);
  inner = run (testNested, 0);
  printf ("shrunk: %d %d %d %d iterations, nested %dx%d\n", got[0], got[1], got[2], got[3],
          *g_last_outer, inner);
  return (got[0] > 0 && got[1] > 0 && got[2] > 0 && got[3] > 0 && inner > 0);
}
