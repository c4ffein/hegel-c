/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_draw_recursive builds bounded trees, and unwinds the
** attempts that outgrow the leaf budget without leaking.
**
** Two shapes, both with hegel_alloc'd nodes (no free anywhere):
**   - binary: a branch has exactly two children, a leaf holds a value
**     (the twin of test_recursion_tree.c, which runs the raw protocol);
**   - n-ary: a node draws 0..3 kid slots, each slot a sub-value whose
**     leaf is NULL — "optional child" (the twin of
**     test_recursion_nary_tree.c); the root may be NULL too.
**
** Layer 1: tree_depth() / tree_leaves() / tree_nodes() and the n-ary
**          counterparts — measure a tree.
** Layer 2: draw a tree with tight budgets and check the engine's
**          bounds (depth <= max_depth, leaves <= max_leaves); draw with
**          the default budgets and record sizes.  Node functions count
**          the nodes they build: built - kept = nodes of abandoned
**          attempts (unwound over budget, or dropped as mispriced).
** Layer 3: runs under nofork and fork; nofork additionally checks the
**          heap did not grow by the abandoned nodes (mallinfo2; under
**          ASan, LeakSanitizer does that at exit).  Prints the size
**          distribution over 1000 cases with the default budgets.
**
** Expected: EXIT 0.
*/
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

#if defined(__SANITIZE_ADDRESS__)
#define UNDER_ASAN 1
#else
#define UNDER_ASAN 0
#endif

#define TIGHT_DEPTH  6
#define TIGHT_LEAVES 20
#define MAX_KIDS     3

typedef struct Bin {
  struct Bin *        left;
  struct Bin *        right;   /* both NULL for a leaf */
  int                 value;
} Bin;

typedef struct Nary {
  int                 val;
  struct Nary **      kids;    /* each slot may be NULL */
  int                 n_kids;
} Nary;

/* ---- Layer 1: functions under test ---- */

static
int
bin_depth (
const Bin *                 n)
{
  int                 l;
  int                 r;

  if (!n->left)
    return (0);
  l = bin_depth (n->left);
  r = bin_depth (n->right);
  return (1 + (l > r ? l : r));
}

static
int
bin_leaves (
const Bin *                 n)
{
  if (!n->left)
    return (1);
  return (bin_leaves (n->left) + bin_leaves (n->right));
}

static
int
bin_nodes (
const Bin *                 n)
{
  if (!n->left)
    return (1);
  return (1 + bin_nodes (n->left) + bin_nodes (n->right));
}

/* Depth counts sub-value levels: a node at depth d has kids at d + 1;
** a NULL slot is a leaf at its own depth. */
static
int
nary_depth (
const Nary *                t)
{
  int                 best = 0;
  int                 d;
  int                 i;

  if (!t)
    return (0);
  for (i = 0; i < t->n_kids; i ++) {
    d = 1 + nary_depth (t->kids[i]);
    if (d > best)
      best = d;
  }
  return (best);
}

/* Leaves in the engine's sense: NULL slots, plus a NULL root. */
static
int
nary_leaves (
const Nary *                t)
{
  int                 n = 0;
  int                 i;

  if (!t)
    return (1);
  for (i = 0; i < t->n_kids; i ++)
    n += nary_leaves (t->kids[i]);
  return (n);
}

static
int
nary_nodes (
const Nary *                t)
{
  int                 n;
  int                 i;

  if (!t)
    return (0);
  n = 1;
  for (i = 0; i < t->n_kids; i ++)
    n += nary_nodes (t->kids[i]);
  return (n);
}

/* ---- Layer 2: generators and hegel tests ---- */

static long g_built;     /* nodes the node functions built, all attempts */
static long g_kept;      /* nodes in the trees the tests received */

static
void *
gen_bin (
hegel_testcase *            tc,
bool                        leaf,
void *                      ctx)
{
  Bin *               n;

  (void) ctx;
  n = hegel_alloc (tc, sizeof *n);
  g_built ++;
  if (leaf) {
    n->value = hegel_draw_int (tc, 0, 9);
    return (n);
  }
  n->left  = hegel_recursive_child (tc);
  n->right = hegel_recursive_child (tc);
  return (n);
}

static
void *
gen_nary (
hegel_testcase *            tc,
bool                        leaf,
void *                      ctx)
{
  Nary *              t;
  int                 i;

  (void) ctx;
  if (leaf)
    return (NULL);                         /* an absent child */
  t = hegel_alloc (tc, sizeof *t);
  g_built ++;
  t->n_kids = hegel_draw_int (tc, 0, MAX_KIDS);
  t->val    = hegel_draw_int (tc, -100, 100);
  t->kids   = hegel_alloc (tc, (size_t) MAX_KIDS * sizeof *t->kids);
  for (i = 0; i < t->n_kids; i ++)
    t->kids[i] = hegel_recursive_child (tc);
  return (t);
}

static
void
test_bin_tight (
hegel_testcase *            tc)
{
  Bin *               t;
  int                 d;
  int                 l;

  t = hegel_draw_recursive (tc, TIGHT_DEPTH, TIGHT_LEAVES, gen_bin, NULL);
  g_kept += bin_nodes (t);
  d = bin_depth (t);
  l = bin_leaves (t);
  HEGEL_ASSERT (d <= TIGHT_DEPTH, "tree depth %d > %d", d, TIGHT_DEPTH);
  HEGEL_ASSERT (l <= TIGHT_LEAVES, "tree has %d leaves > %d", l, TIGHT_LEAVES);
}

static
void
test_nary_tight (
hegel_testcase *            tc)
{
  Nary *              t;
  int                 d;
  int                 l;

  t = hegel_draw_recursive (tc, TIGHT_DEPTH, TIGHT_LEAVES, gen_nary, NULL);
  g_kept += nary_nodes (t);
  d = nary_depth (t);
  l = nary_leaves (t);
  HEGEL_ASSERT (d <= TIGHT_DEPTH, "tree depth %d > %d", d, TIGHT_DEPTH);
  HEGEL_ASSERT (l <= TIGHT_LEAVES, "tree has %d leaves > %d", l, TIGHT_LEAVES);
}

/* Default budgets: record sizes. */
#define HIST_MAX 400
static int g_cases;
static int g_size_hist[HIST_MAX + 1];

static
void
record_size (
int                         n)
{
  g_cases ++;
  g_size_hist[n > HIST_MAX ? HIST_MAX : n] ++;
}

static
void
test_bin_default (
hegel_testcase *            tc)
{
  Bin *               t;
  int                 l;

  t = hegel_draw_recursive (tc, HEGEL_RECURSIVE_MAX_DEPTH, HEGEL_RECURSIVE_MAX_LEAVES,
                            gen_bin, NULL);
  l = bin_leaves (t);
  HEGEL_ASSERT (l <= HEGEL_RECURSIVE_MAX_LEAVES, "%d leaves", l);
  HEGEL_ASSERT (bin_depth (t) <= HEGEL_RECURSIVE_MAX_DEPTH, "too deep");
  g_kept += bin_nodes (t);
  record_size (bin_nodes (t));
}

static
void
test_nary_default (
hegel_testcase *            tc)
{
  Nary *              t;

  t = hegel_draw_recursive (tc, HEGEL_RECURSIVE_MAX_DEPTH, HEGEL_RECURSIVE_MAX_LEAVES,
                            gen_nary, NULL);
  HEGEL_ASSERT (nary_leaves (t) <= HEGEL_RECURSIVE_MAX_LEAVES, "%d leaves", nary_leaves (t));
  g_kept += nary_nodes (t);
  record_size (nary_nodes (t));
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
run (
const char *                what,
void                     (* fn) (hegel_testcase *),
uint64_t                    n,
int                         fork_mode)
{
  hegel_run_options   o;
  size_t              before;
  size_t              after;
  int                 rc;

  memset (&o, 0, sizeof o);
  o.test_cases = n;
  g_built = g_kept = 0;
  before = heap_in_use ();
  rc = fork_mode ? hegel_run_test_with (fn, &o) : hegel_run_test_nofork_with (fn, &o);
  after = heap_in_use ();
  if (rc != 0) {
    printf ("  %-26s FAILED\n", what);
    g_errors ++;
    return;
  }
  if (fork_mode) {
    printf ("  %-26s ok\n", what);
    return;
  }
  printf ("  %-26s ok (%ld nodes built, %ld kept, %ld abandoned)\n",
          what, g_built, g_kept, g_built - g_kept);
  /* The abandoned nodes alone would be ~32 bytes each, plus kept ones. */
  if (!UNDER_ASAN && after > before + (1u << 20)) {
    printf ("  %-26s LEAK: heap grew by %zu KiB\n", what, (after - before) >> 10);
    g_errors ++;
  }
}

static
void
print_sizes (
const char *                what)
{
  long                sum = 0;
  int                 cum = 0;
  int                 median = 0;
  int                 p90 = 0;
  int                 max = 0;
  int                 small = 0;
  int                 i;

  for (i = 0; i <= HIST_MAX; i ++) {
    sum += (long) i * g_size_hist[i];
    if (g_size_hist[i])
      max = i;
    if (i <= 1)
      small += g_size_hist[i];
    cum += g_size_hist[i];
    if (!median && cum * 2 >= g_cases)
      median = i;
    if (!p90 && cum * 10 >= g_cases * 9)
      p90 = i;
  }
  printf ("  %-26s %d cases: mean %.1f nodes, median %d, p90 %d, max %d, <=1 node %d%%\n",
          what, g_cases, g_cases ? (double) sum / g_cases : 0.0, median, p90, max,
          g_cases ? 100 * small / g_cases : 0);
  if (g_cases && (double) sum / g_cases < 5.0) {
    printf ("  %-26s trees collapsed to a few nodes on average\n", what);
    g_errors ++;
  }
  g_cases = 0;
  memset (g_size_hist, 0, sizeof g_size_hist);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing hegel_draw_recursive...\n");
  run ("binary, tight, nofork", test_bin_tight, 300, 0);
  run ("n-ary, tight, nofork", test_nary_tight, 300, 0);
  run ("binary, tight, fork", test_bin_tight, 100, 1);
  run ("n-ary, tight, fork", test_nary_tight, 100, 1);

  run ("binary, default, nofork", test_bin_default, 1000, 0);
  print_sizes ("binary, default budgets");
  run ("n-ary, default, nofork", test_nary_default, 1000, 0);
  print_sizes ("n-ary, default budgets");

  if (g_errors) {
    printf ("FAILED: %d error(s)\n", g_errors);
    return (1);
  }
  printf ("OK\n");
  return (0);
}
