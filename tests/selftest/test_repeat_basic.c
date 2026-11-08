/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: repeat loops — the engine runs the body a varying number of
**       times, in every form, under both runners.
**
** Layer 1: a growable int stack (correct implementation): push, pop,
**          size.
** Layer 2: each iteration draws an operation (push a value / pop) and
**          checks the stack against a model array.  Properties:
**            - HEGEL_REPEAT (nofork);
**            - hegel_repeat_run (nofork), where an iteration rejects some
**              pushes with hegel_assume: only that iteration ends, the
**              loop goes on (the case is not discarded);
**            - the raw protocol (hegel_repeat_new / _more, fork and
**              nofork), stopping itself after 64 iterations, whose handle
**              is freed only every other case: the rest are left for the
**              end of the case to free.
**          A passing loop often runs until the case's choice budget runs
**          out (2^20 choices: hundreds of thousands of iterations), which
**          is why the uncapped forms run nofork, on few cases; their fork
**          paths are covered by test_repeat_shrinks.
**          Statistics go to shared memory (fork children write them
**          too): how many distinct iteration counts were seen, the
**          largest, how many iterations ran after a rejected one.
** Layer 3: exits 0 iff every run passed and the statistics show the
**          engine varied the iteration count (several distinct counts,
**          some loop of 5+ iterations) and rejected iterations did not
**          end their loop.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

typedef struct {
  int *               v;
  int                 n;
  int                 cap;
} Stack;

static
void
stack_push (
Stack *                     s,
int                         x)
{
  if (s->n == s->cap) {
    s->cap = s->cap ? 2 * s->cap : 4;
    s->v = realloc (s->v, (size_t) s->cap * sizeof *s->v);
    if (!s->v)
      abort ();
  }
  s->v[s->n ++] = x;
}

static
int
stack_pop (
Stack *                     s,
int *                       out)
{
  if (s->n == 0)
    return (0);
  *out = s->v[-- s->n];
  return (1);
}

static
int
stack_size (
const Stack *               s)
{
  return (s->n);
}

/* ---- Layer 2: hegel tests ---- */

#define MAX_TRACKED 64

typedef struct {
  int                 seen[MAX_TRACKED + 1];  /* seen[k]: some loop ran k times */
  int                 max_iters;
  int                 after_reject;           /* iterations run after a rejected one */
  int                 rejected;
  int                 cases;
} Stats;

static Stats *      g_stats;                  /* shared with fork children */

typedef struct {
  hegel_testcase *    tc;
  Stack               s;
  int *               model;
  int                 n;
  int                 model_cap;
  int                 iters;
  int                 rejected;               /* this loop rejected an iteration */
  int                 reject_some;            /* hegel_assume away pushes of multiples of 7 */
} Run;

static
void
one_op (
hegel_testcase *            tc,
void *                      ctx)
{
  Run *               r = ctx;
  int                 x;
  int                 got;

  r->iters ++;
  if (r->rejected)
    g_stats->after_reject ++;
  if (hegel_draw_bool (tc)) {
    x = hegel_draw_int (tc, 0, 1000);
    if (r->reject_some && x % 7 == 0) {
      r->rejected = 1;
      g_stats->rejected ++;
    }
    if (r->reject_some)
      hegel_assume (tc, x % 7 != 0);
    stack_push (&r->s, x);
    if (r->n == r->model_cap) {
      r->model_cap = r->model_cap ? 2 * r->model_cap : 16;
      r->model = hegel_realloc (tc, r->model, (size_t) r->model_cap * sizeof *r->model);
    }
    r->model[r->n ++] = x;
  } else if (stack_pop (&r->s, &got)) {
    HEGEL_ASSERT (r->n > 0, "popped %d from a stack the model says is empty", got);
    r->n --;
    HEGEL_ASSERT (got == r->model[r->n], "popped %d, model says %d", got, r->model[r->n]);
  } else {
    HEGEL_ASSERT (r->n == 0, "pop failed with %d values in the model", r->n);
  }
  HEGEL_ASSERT (stack_size (&r->s) == r->n, "size %d, model %d", stack_size (&r->s), r->n);
}

static
void
record (
Run *                       r)
{
  g_stats->cases ++;
  g_stats->seen[r->iters < MAX_TRACKED ? r->iters : MAX_TRACKED] = 1;
  if (r->iters > g_stats->max_iters)
    g_stats->max_iters = r->iters;
  free (r->s.v);
}

static
void
testMacro (
hegel_testcase *            tc)
{
  Run                 r;

  memset (&r, 0, sizeof r);
  HEGEL_REPEAT (tc) {
    one_op (tc, &r);
  }
  record (&r);
}

static
void
testRunAssume (
hegel_testcase *            tc)
{
  Run                 r;

  memset (&r, 0, sizeof r);
  r.reject_some = 1;
  hegel_repeat_run (tc, one_op, &r);
  record (&r);
}

/* The raw protocol, stopping on its own after RAW_CAP iterations (the
** engine would often go on until the case's choice budget runs out —
** a million pipe round trips in fork mode).  Leaving a loop early is
** fine; the handle is freed every other case, the rest at case end. */
#define RAW_CAP 64

static
void
testRaw (
hegel_testcase *            tc)
{
  static int          parity;
  Run                 r;
  hegel_repeat        rp;

  memset (&r, 0, sizeof r);
  rp = hegel_repeat_new (tc);
  while (r.iters < RAW_CAP && hegel_repeat_more (tc, rp))
    one_op (tc, &r);
  if (parity ++ % 2)
    hegel_repeat_free (tc, rp);
  record (&r);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

static
int
check (
const char *                name,
void                     (* fn) (hegel_testcase *),
int                         use_fork,
int                         want_rejects)
{
  hegel_run_options   o;
  int                 distinct = 0;
  int                 k;
  int                 ok;

  memset (g_stats, 0, sizeof *g_stats);
  memset (&o, 0, sizeof o);
  o.test_cases = fn == testRaw ? 200 : 40;
  /* Seeded: the uncapped forms spend most cases near the choice budget, so
  ** with a random seed 40 cases sometimes land in only 2 count buckets
  ** (2026-11-08, once in a suite run).  The check is that the engine varies
  ** the count at all, which one fixed seed shows as well as any. */
  o.has_seed = 1;
  o.seed = 1;
  if ((use_fork ? hegel_run_test_with (fn, &o) : hegel_run_test_nofork_with (fn, &o)) != 0) {
    printf ("  %s (%s): run failed\n", name, use_fork ? "fork" : "nofork");
    return (0);
  }
  for (k = 0; k <= MAX_TRACKED; k ++)
    distinct += g_stats->seen[k];
  ok = distinct >= 3 && g_stats->max_iters >= 5
       && (!want_rejects || (g_stats->rejected > 0 && g_stats->after_reject > 0));
  printf ("  %s (%s): %d cases, %d distinct iteration counts, max %d, "
          "%d rejected iterations, %d iterations after one: %s\n",
          name, use_fork ? "fork" : "nofork", g_stats->cases, distinct, g_stats->max_iters,
          g_stats->rejected, g_stats->after_reject, ok ? "ok" : "BAD");
  return (ok);
}

int
main (
int                 argc,
char *              argv[])
{
  int                 ok = 1;

  (void) argc;
  (void) argv;

  g_stats = mmap (NULL, sizeof *g_stats, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (g_stats == MAP_FAILED) {
    perror ("mmap");
    return (1);
  }

  printf ("Testing repeat loops...\n");
  ok &= check ("HEGEL_REPEAT", testMacro, 0, 0);
  ok &= check ("hegel_repeat_run + assume", testRunAssume, 0, 1);
  ok &= check ("raw protocol, half unfreed", testRaw, 0, 0);
  ok &= check ("raw protocol, half unfreed", testRaw, 1, 0);
  return (ok ? 0 : 1);
}
