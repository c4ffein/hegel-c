/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a repeat handle belongs to its test case — using it after
**       hegel_repeat_free, or in a later case (the end of its case freed
**       it), is a health failure, not a silent misuse of the engine.
**
** Layer 1: count_until_told() — a counter driven by a repeat loop; it
**          returns how many iterations the engine asked for (correct).
** Layer 2: two misuses, each its own run:
**            - fork mode: free the handle, then ask it for more;
**            - nofork: keep the first case's handle in a static and drive
**              it from the second case, after the first case ended.
** Layer 3: exits 1 iff both runs failed (each prints "Health check
**          failure: hegel-c: stale or invalid repeat handle").
**
** Expected: EXIT non-zero with "Health check failure" in the output.
*/
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

static
int
count_until_told (
hegel_testcase *            tc,
hegel_repeat                r,
int                         cap)
{
  int                 n = 0;

  while (n < cap && hegel_repeat_more (tc, r))
    n ++;
  return (n);
}

/* ---- Layer 2: hegel tests ---- */

static
void
testUseAfterFree (
hegel_testcase *            tc)
{
  hegel_repeat        r;

  r = hegel_repeat_new (tc);
  count_until_told (tc, r, 3);
  hegel_repeat_free (tc, r);
  count_until_told (tc, r, 3);                /* health failure */
}

static
void
testLaterCase (
hegel_testcase *            tc)
{
  static int          have;
  static hegel_repeat kept;

  if (!have) {
    kept = hegel_repeat_new (tc);
    count_until_told (tc, kept, 3);
    have = 1;                                 /* not freed: the case end frees it */
    return;
  }
  count_until_told (tc, kept, 3);             /* health failure */
}

/* ---- Layer 3: runner (see Makefile TESTS_HEALTH) ---- */

int
main (
int                 argc,
char *              argv[])
{
  int                 a;
  int                 b;

  (void) argc;
  (void) argv;

  printf ("Testing that stale repeat handles are rejected...\n");
  a = hegel_run_test_result_n (testUseAfterFree, 20);
  b = hegel_run_test_nofork_with (testLaterCase, &(hegel_run_options) { .test_cases = 20 });
  printf ("  use after free: %s, from a later case: %s\n",
          a ? "rejected" : "NOT rejected", b ? "rejected" : "NOT rejected");
  return (a && b);
}
