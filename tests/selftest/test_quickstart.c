/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: the example of docs/quickstart.md, kept here so the document's
** snippets are lines of a file that builds and runs.
**
** Layer 1: dedup() removes adjacent duplicates in place, but returns 1
**          for an empty array.
** Layer 2: a property over random arrays: the result is no longer than
**          the input and has no two adjacent equal values.
**
** Expected: EXIT NON-ZERO (shrunk to the empty array).
*/
#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

/* Remove adjacent duplicates in place; return the new length. */
static int dedup (int *a, int n)
{
  int w = 1;
  for (int r = 1; r < n; r++)
    if (a[r] != a[r - 1])
      a[w++] = a[r];
  return w;
}

/* ---- Layer 2: hegel test ---- */

static void test_dedup (hegel_testcase *tc)
{
  int  n = hegel_draw_int (tc, 0, 20);
  int *a = hegel_alloc (tc, (size_t) (n + 1) * sizeof *a);
  for (int i = 0; i < n; i++)
    a[i] = hegel_draw_int (tc, 0, 3);

  int m = dedup (a, n);

  HEGEL_ASSERT_LE (m, n);
  for (int i = 1; i < m; i++)
    HEGEL_ASSERT_NE (a[i], a[i - 1], "at index %d", i);
}

/* ---- Layer 3: runner (see Makefile TESTS_FAIL) ---- */

int main (void)
{
  hegel_run_test (test_dedup);
  return 0;
}
