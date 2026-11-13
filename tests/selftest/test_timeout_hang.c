/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: code under test that never returns is a failure that shrinks,
**       not a hung run — the forked body is killed at the run's timeout.
**
** EXPECTED_SHRINK: 8 8
**
** Layer 1: ring_walk() hops around a ring of 8 slots, `step` slots at a
**          time, until it lands on a slot other than 0.  With a step
**          that is a multiple of 8 it lands on slot 0 for ever (the
**          deliberate bug: no bound on the number of hops).
** Layer 2: draw a step in 1..100 and walk.  Run
**            - fork mode: the case's child hangs; the runner kills it at
**              the deadline and the case fails with origin "timeout";
**            - nofork, the walk inside HEGEL_ISOLATED: the block's child
**              hangs and is killed; the failure's origin is the block.
**          Both with a 300 ms timeout (the default, 10 s, would spend
**          minutes shrinking: every hanging execution costs the whole
**          timeout).  The step of the last execution (the final replay
**          of the shrunk case) is kept in shared memory.
** Layer 3: prints "shrunk: A B" for the Makefile's TESTS_SHRINK check
**          and exits 1 when both runs failed.  The smallest step that
**          hangs is 8.
**
** Expected: EXIT non-zero with "shrunk: 8 8".
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

#define RING 8

static
int
ring_walk (
int                         step)
{
  volatile int        pos = 0;                /* volatile: the loop is really run */
  int                 hops = 0;

  do {
    pos = (pos + step) % RING;
    hops ++;
  } while (pos == 0);                         /* BUG: no bound on hops */
  return (hops);
}

/* ---- Layer 2: hegel tests ---- */

static int *        g_last_step;              /* shared with fork children */

static
void
testFork (
hegel_testcase *            tc)
{
  int                 step = hegel_draw_int (tc, 1, 100);
  int                 hops;

  *g_last_step = step;
  hops = ring_walk (step);
  HEGEL_ASSERT (hops >= 1, "ring_walk(%d) = %d", step, hops);
}

static
void
testIsolated (
hegel_testcase *            tc)
{
  int                 step = hegel_draw_int (tc, 1, 100);

  *g_last_step = step;
  HEGEL_ISOLATED (tc) {
    int               hops = ring_walk (step);

    HEGEL_ASSERT (hops >= 1, "ring_walk(%d) = %d", step, hops);
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
  o.test_cases = 200;
  o.timeout_ms = 300;
  *g_last_step = -1;
  failed = use_fork ? hegel_run_test_with (fn, &o) : hegel_run_test_nofork_with (fn, &o);
  return (failed ? *g_last_step : -1);
}

int
main (
int                 argc,
char *              argv[])
{
  int                 a;
  int                 b;

  (void) argc;
  (void) argv;

  g_last_step = mmap (NULL, sizeof *g_last_step, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (g_last_step == MAP_FAILED) {
    perror ("mmap");
    return (2);
  }

  a = run (testFork, 1);
  b = run (testIsolated, 0);
  printf ("shrunk: %d %d\n", a, b);
  return ((a > 0 && b > 0) ? 1 : 0);
}
