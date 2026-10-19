/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: engine-owned collections — size bounds hold and
**       hegel_collection_reject keeps duplicates out.
**
** Layer 1: is_strictly_sorted_after_sort() — inserts into a sorted set
**          (correct implementation) and checks strict ordering.
** Layer 2: draw a collection of distinct ints (duplicates rejected),
**          feed them to the set, check size bounds and strictness.
**          Runs under both runners.
**
** Expected: EXIT 0.
*/
#include <stdio.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Insert x into sorted array s[0..*n) keeping it sorted; returns 0 if
** x was already present. */

static
int
sorted_insert (
int *                       s,
int *                       n,
int                         x)
{
  int                 i;
  int                 j;

  for (i = 0; i < *n && s[i] < x; i ++) ;
  if (i < *n && s[i] == x)
    return (0);
  for (j = *n; j > i; j --)
    s[j] = s[j - 1];
  s[i] = x;
  (*n) ++;
  return (1);
}

/* ---- Layer 2: hegel test ---- */

static
void
testCollection (
hegel_testcase *            tc)
{
  int                 set[12];
  int                 n;
  int                 i;
  hegel_collection    c;

  n = 0;
  c = hegel_collection_new (tc, 2, 12);
  while (hegel_collection_more (tc, c)) {
    int               x;

    x = hegel_draw_int (tc, 0, 40);
    if (!sorted_insert (set, &n, x))
      hegel_collection_reject (tc, c);
  }
  hegel_collection_free (tc, c);

  HEGEL_ASSERT (n >= 2 && n <= 12, "collection size %d outside [2, 12]", n);
  for (i = 1; i < n; i ++)
    HEGEL_ASSERT (set[i - 1] < set[i], "not strictly sorted at %d", i);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing engine-owned collections...\n");
  if (hegel_run_test_result_n (testCollection, 100) != 0)
    return (1);
  hegel_run_test_nofork_n (testCollection, 100);
  printf ("OK\n");

  return (0);
}
