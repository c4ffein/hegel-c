/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_alloc blocks are freed at the end of the test case, on
** every way out of it, and are usable until then.
**
** Layer 1: fill_pattern() / check_pattern() — write and verify a
**          seed-derived byte pattern.
** Layer 2: bodies that allocate case memory (1 MiB per case, so a leak
**          shows), fill it, then leave the case a different way each:
**            - return (nofork and fork);
**            - hegel_assume (half the cases);
**            - HEGEL_ASSERT failure (the run fails and shrinks, as
**              expected — its report is printed);
**            - hegel_health_fail (the run aborts, as expected);
**            - a stateful rule that assumes after hanging a block on
**              the state (teardown still reads it);
**            - a HEGEL_ISOLATED block that reads earlier blocks and
**              allocates its own (nofork: in the block's child).
**          Plus the API surface: zeroed memory, hegel_realloc keeping
**          contents and zeroing growth, hegel_free.
** Layer 3: main() runs each and, outside sanitizers, checks with
**          mallinfo2 that the heap did not grow by the cases' blocks
**          (100 cases x 1 MiB would be 100 MiB; a runtime that skips
**          the release fails all six nofork checks).  Under
**          ASan the check is LeakSanitizer's, at exit: build the core
**          and this file in one gcc command with
**          -fsanitize=address,undefined.
**
** Expected: EXIT 0 (the failing and health runs are expected; main
** checks that they did fail).
*/
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

#define BIG        (1u << 20)   /* bytes per case: a leak must be visible */
#define N_CASES    100
#define SM_BLOCK   4096         /* bytes per stateful rule block */
#define LEAK_SLACK (4u << 20)   /* heap growth tolerated per run (engine state) */

#if defined(__SANITIZE_ADDRESS__)
#define UNDER_ASAN 1
#else
#define UNDER_ASAN 0
#endif

/* ---- Layer 1: functions under test ---- */

static
void
fill_pattern (
unsigned char *             buf,
size_t                      n,
unsigned                    seed)
{
  size_t              i;

  for (i = 0; i < n; i ++)
    buf[i] = (unsigned char) (seed * 31u + i * 7u);
}

static
int
check_pattern (
const unsigned char *       buf,
size_t                      n,
unsigned                    seed)
{
  size_t              i;

  for (i = 0; i < n; i ++)
    if (buf[i] != (unsigned char) (seed * 31u + i * 7u))
      return (0);
  return (1);
}

/* ---- Layer 2: hegel tests ---- */

/* Common prologue: a big zeroed block filled with a pattern. */
static
unsigned char *
big_block (
hegel_testcase *            tc,
unsigned *                  seed)
{
  unsigned char *     b;
  size_t              i;

  *seed = (unsigned) hegel_draw_int (tc, 0, 1000);
  b = hegel_alloc (tc, BIG);
  for (i = 0; i < BIG; i += 4096)
    HEGEL_ASSERT (b[i] == 0, "hegel_alloc memory not zeroed at %zu", i);
  fill_pattern (b, BIG, *seed);
  return (b);
}

static
void
test_return (
hegel_testcase *            tc)
{
  unsigned            seed;
  unsigned char *     b;
  unsigned char *     s;
  int *               v;
  int                 i;

  b = big_block (tc, &seed);

  /* realloc: contents kept, growth zeroed, NULL acts as alloc. */
  v = hegel_realloc (tc, NULL, 4 * sizeof (int));
  for (i = 0; i < 4; i ++)
    v[i] = i + 1;
  v = hegel_realloc (tc, v, 64 * sizeof (int));
  for (i = 0; i < 4; i ++)
    HEGEL_ASSERT (v[i] == i + 1, "hegel_realloc lost v[%d]", i);
  for (i = 4; i < 64; i ++)
    HEGEL_ASSERT (v[i] == 0, "hegel_realloc growth not zeroed at %d", i);

  /* free one early; the rest go at the end of the case. */
  s = hegel_alloc (tc, 100);
  hegel_free (tc, s);
  hegel_free (tc, NULL);

  HEGEL_ASSERT (check_pattern (b, BIG, seed), "pattern damaged");
}

static
void
test_assume (
hegel_testcase *            tc)
{
  unsigned            seed;

  (void) big_block (tc, &seed);
  hegel_assume (tc, seed % 2 == 0);
}

static
void
test_fail (
hegel_testcase *            tc)
{
  unsigned            seed;

  (void) big_block (tc, &seed);
  HEGEL_ASSERT (seed < 500, "seed %u >= 500 (expected failure)", seed);
}

static
void
test_health (
hegel_testcase *            tc)
{
  unsigned            seed;
  int                 i;

  (void) big_block (tc, &seed);
  /* A health failure ends the run within a case or two: allocate
  ** enough that a leak still shows. */
  for (i = 0; i < 8; i ++)
    (void) hegel_alloc (tc, BIG);
  if (seed > 10)
    hegel_health_fail ("expected health failure from test_alloc_lifetime");
}

/* HEGEL_ISOLATED: the block sees the blocks made before it, and may
** allocate (and free) its own.  Under nofork the block is a forked
** child, whose blocks vanish with it; under fork it runs inline and its
** blocks are the case's.  Freeing an EARLIER block inside the block
** would mean different things in the two modes, so it isn't done. */
static
void
test_isolated (
hegel_testcase *            tc)
{
  unsigned            seed;
  unsigned char *     b;
  unsigned char *     early;

  b = big_block (tc, &seed);
  early = hegel_alloc (tc, 64);
  fill_pattern (early, 64, seed + 1);

  HEGEL_ISOLATED (tc) {
    unsigned char *   mine;
    unsigned char *   kept;

    HEGEL_ASSERT (check_pattern (b, BIG, seed), "block not visible in HEGEL_ISOLATED");
    mine = hegel_alloc (tc, BIG);
    fill_pattern (mine, BIG, seed + 2);
    HEGEL_ASSERT (check_pattern (mine, BIG, seed + 2), "block made inside damaged");
    hegel_free (tc, mine);
    kept = hegel_alloc (tc, BIG);           /* left to the case end (fork) / exit (nofork) */
    kept[0] = 1;
  }

  HEGEL_ASSERT (check_pattern (early, 64, seed + 1), "earlier block damaged by the block");
}

/* ---- Stateful: a rule that assumes after hanging a block on the state ---- */

typedef struct {
  unsigned char *     blocks[64];
  unsigned            seeds[64];
  int                 n;
} sm_state;

static int g_teardown_checked;

static
void *
sm_setup (void)
{
  return (calloc (1, sizeof (sm_state)));
}

static
void
sm_teardown (
void *                      p)
{
  sm_state *          st = p;
  int                 i;

  /* Blocks are still alive here: they are freed after teardown. */
  for (i = 0; i < st->n; i ++)
    if (!check_pattern (st->blocks[i], SM_BLOCK, st->seeds[i])) {
      fprintf (stderr, "teardown: block %d damaged\n", i);
      abort ();
    }
  g_teardown_checked += st->n;
  free (st);
}

static
void
rule_add (
void *                      p,
hegel_testcase *            tc)
{
  sm_state *          st = p;
  unsigned            seed;
  unsigned char *     b;

  if (st->n == 64)
    return;
  seed = (unsigned) hegel_draw_int (tc, 0, 1000);
  b = hegel_alloc (tc, SM_BLOCK);
  fill_pattern (b, SM_BLOCK, seed);
  st->blocks[st->n] = b;
  st->seeds[st->n ++] = seed;
  hegel_assume (tc, seed % 3 != 0);         /* rejected rule keeps its block */
}

static
void
inv_blocks (
const void *                p,
hegel_testcase *            tc)
{
  const sm_state *    st = p;
  int                 i;

  (void) tc;
  for (i = 0; i < st->n; i ++)
    HEGEL_ASSERT (check_pattern (st->blocks[i], SM_BLOCK, st->seeds[i]),
                  "block %d damaged", i);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

static
size_t
heap_in_use (void)
{
  struct mallinfo2    mi;

  mi = mallinfo2 ();
  return (mi.uordblks + mi.hblkhd);
}

static int g_errors;

static
void
check_growth (
const char *                what,
size_t                      before)
{
  size_t              after;

  if (UNDER_ASAN) {
    printf ("  %-28s ran (leaks checked by LeakSanitizer at exit)\n", what);
    return;
  }
  after = heap_in_use ();
  if (after > before + LEAK_SLACK) {
    printf ("  %-28s LEAK: heap grew by %zu KiB\n", what, (after - before) >> 10);
    g_errors ++;
  }
  else
    printf ("  %-28s ok (heap %+ld KiB)\n", what,
            (long) ((long long) after - (long long) before) / 1024);
}

int
main (
int                 argc,
char *              argv[])
{
  hegel_run_options   o;
  hegel_state_machine * sm;
  size_t              h;
  int                 rc;

  (void) argc;
  (void) argv;

  memset (&o, 0, sizeof o);
  o.test_cases = N_CASES;

  printf ("Testing hegel_alloc lifetimes...\n");

  h = heap_in_use ();
  rc = hegel_run_test_nofork_with (test_return, &o);
  if (rc != 0) g_errors ++;
  check_growth ("nofork, return", h);

  h = heap_in_use ();
  rc = hegel_run_test_nofork_with (test_assume, &o);
  if (rc != 0) g_errors ++;
  check_growth ("nofork, assume", h);

  printf ("  (the next run fails on purpose)\n");
  h = heap_in_use ();
  rc = hegel_run_test_nofork_with (test_fail, &o);
  if (rc != 1) { printf ("  expected test_fail to fail\n"); g_errors ++; }
  check_growth ("nofork, HEGEL_ASSERT failure", h);

  printf ("  (the next run is a health failure on purpose)\n");
  h = heap_in_use ();
  rc = hegel_run_test_nofork_with (test_health, &o);
  if (rc != 1) { printf ("  expected test_health to fail\n"); g_errors ++; }
  check_growth ("nofork, health failure", h);

  h = heap_in_use ();
  rc = hegel_run_test_nofork_with (test_isolated, &o);
  if (rc != 0) g_errors ++;
  check_growth ("nofork, HEGEL_ISOLATED", h);

  h = heap_in_use ();
  sm = hegel_state_machine_new (sm_setup, sm_teardown);
  hegel_state_machine_add_rule (sm, "add", rule_add);
  hegel_state_machine_add_invariant (sm, "blocks", inv_blocks);
  hegel_state_machine_run_nofork_n (sm, N_CASES);   /* exit(1) on failure */
  check_growth ("nofork, stateful assume", h);
  if (g_teardown_checked == 0) {
    printf ("  stateful: teardown never saw a block\n");
    g_errors ++;
  }

  /* Fork mode: the blocks belong to the child; same API checks. */
  if (hegel_run_test_with (test_return, &o) != 0) g_errors ++;
  if (hegel_run_test_with (test_isolated, &o) != 0) g_errors ++;
  hegel_state_machine_run_n (sm, N_CASES);
  printf ("  %-28s ok\n", "fork mode (return, isolated, stateful)");
  hegel_state_machine_free (sm);

  if (g_errors) {
    printf ("FAILED: %d error(s)\n", g_errors);
    return (1);
  }
  printf ("OK\n");
  return (0);
}
