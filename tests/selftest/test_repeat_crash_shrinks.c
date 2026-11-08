/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a repeat loop whose body CRASHES shrinks to the fewest
**       iterations — the iteration is reported failed by the runtime even
**       though the body never got to say so.
**
** EXPECTED_SHRINK: 4 4 iterations
**
** Layer 1: an event log with room for 3 entries; log_append() aborts when
**          it is full instead of growing (the deliberate bug), so the 4th
**          call crashes.
** Layer 2: in a HEGEL_REPEAT loop, draw an event and append it.  Run
**            - fork mode: the child dies with SIGABRT; the parent marks
**              the open iteration failed (hegel_repeat_failed) before
**              completing the case;
**            - nofork, the append inside HEGEL_ISOLATED: the block's
**              child dies, the body fails at the block's origin.
**          The iteration count reached by the last execution (the final
**          replay of the shrunk case) is kept in shared memory.
** Layer 3: prints "shrunk: A B iterations" for the Makefile's
**          TESTS_SHRINK check and exits 1 when both runs failed.
**
** Expected: EXIT non-zero with "shrunk: 4 4 iterations".
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

#define LOG_CAP 3

typedef struct {
  int                 entry[LOG_CAP];
  int                 n;
} Log;

static
void
log_append (
Log *                       l,
int                         v)
{
  if (l->n >= LOG_CAP)
    abort ();                                 /* BUG: should grow */
  l->entry[l->n ++] = v;
}

/* ---- Layer 2: hegel tests ---- */

static int *        g_last_iters;             /* shared with fork children */

static
void
testFork (
hegel_testcase *            tc)
{
  Log                 l;
  int                 iters = 0;

  memset (&l, 0, sizeof l);
  HEGEL_REPEAT (tc) {
    int               v = hegel_draw_int (tc, 0, 9);

    *g_last_iters = ++ iters;
    log_append (&l, v);
  }
}

static
void
testIsolated (
hegel_testcase *            tc)
{
  Log                 l;
  int                 iters = 0;

  memset (&l, 0, sizeof l);
  HEGEL_REPEAT (tc) {
    int               v = hegel_draw_int (tc, 0, 9);

    *g_last_iters = ++ iters;
    HEGEL_ISOLATED (tc) {
      log_append (&l, v);
    }
    if (l.n < LOG_CAP)                        /* the block's writes stayed in its child */
      l.entry[l.n ++] = v;
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
  int                 a;
  int                 b;

  (void) argc;
  (void) argv;

  g_last_iters = mmap (NULL, sizeof *g_last_iters, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (g_last_iters == MAP_FAILED) {
    perror ("mmap");
    return (0);                               /* TESTS_SHRINK: exit 0 is the failure */
  }

  printf ("Testing that a crashing repeat loop shrinks to the fewest iterations...\n");
  a = run (testFork, 1);
  b = run (testIsolated, 0);
  printf ("shrunk: %d %d iterations\n", a, b);
  return (a > 0 && b > 0);
}
