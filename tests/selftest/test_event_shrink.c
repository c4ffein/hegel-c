/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_event / hegel_event_value don't disturb generation or
**       shrinking, in fork and nofork bodies.
**
** EXPECTED_SHRINK: [100]
**
** Layer 1: count_above() counts the elements of a list strictly above a
**          threshold.  Deliberate bug: it tests >= instead of >, so an
**          element equal to the threshold is counted.
** Layer 2: draw a list (length 0..8, elements 90..110) and check
**          count_above (xs, n, 100) against a reference count.  With
**          `events` on, the same body also records events and several
**          values per case (an event per boundary hit, the length, every
**          element, a repeated label).  The smallest failing list is
**          [100].
** Layer 3: four runs with the same seed — fork and nofork, each without
**          and with events.  Every body records a hash of everything it
**          drew, and its last failing list, in memory shared with the
**          fork children (MAP_SHARED, inherited across fork).  Events on
**          and off must give the same hash, the same case count and the
**          same shrunk list, in each mode.  Then prints
**          "shrunk: <list>" for the Makefile's TESTS_SHRINK check (or
**          "shrunk: MISMATCH" if anything differs) and exits 1.
**
** Expected: EXIT non-zero with "shrunk: [100]".
*/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "hegel_c.h"

#define MAX_N     8
#define THRESHOLD 100

/* ---- Layer 1: function under test ---- */

static
int
count_above (
const int *                 xs,
int                         n,
int                         t)
{
  int                 c = 0;

  for (int i = 0; i < n; i ++)
    if (xs[i] >= t)                           /* BUG: should be > */
      c ++;
  return (c);
}

/* ---- Layer 2: the property ---- */

typedef struct {
  uint64_t            hash;                   /* FNV-1a over every drawn value */
  uint64_t            cases;
  int                 last_n;                 /* last failing list */
  int                 last_xs[MAX_N];
} Trace;

static Trace *              g_trace;          /* MAP_SHARED: fork children write it */
static int                  g_events;

static
void
mix (
uint64_t                    v)
{
  for (int i = 0; i < 8; i ++) {
    g_trace->hash ^= (v >> (8 * i)) & 0xff;
    g_trace->hash *= 1099511628211ull;
  }
}

static
void
testCountAbove (
hegel_testcase *            tc)
{
  int                 xs[MAX_N];
  int                 n;
  int                 expected = 0;

  n = hegel_draw_int (tc, 0, MAX_N);
  mix ((uint64_t) n);
  for (int i = 0; i < n; i ++) {
    xs[i] = hegel_draw_int (tc, 90, 110);
    mix ((uint64_t) xs[i]);
  }
  g_trace->cases ++;

  if (g_events) {
    if (n == 0)
      hegel_event (tc, "empty list");
    hegel_event_value (tc, "length", (double) n);
    for (int i = 0; i < n; i ++) {
      hegel_event_value (tc, "element", (double) xs[i]);
      if (xs[i] == THRESHOLD)
        hegel_event (tc, "element at the threshold");   /* repeats count once */
    }
    hegel_event (tc, "case");
  }

  for (int i = 0; i < n; i ++)
    if (xs[i] > THRESHOLD)
      expected ++;
  if (count_above (xs, n, THRESHOLD) != expected) {
    g_trace->last_n = n;
    memcpy (g_trace->last_xs, xs, (size_t) n * sizeof xs[0]);
  }
  HEGEL_ASSERT (count_above (xs, n, THRESHOLD) == expected,
                "count_above: got %d, expected %d", count_above (xs, n, THRESHOLD), expected);
}

/* ---- Layer 3: runner ---- */

static
int
run (
int                         nofork,
int                         events,
Trace *                     out)
{
  hegel_run_options   o = { .test_cases = 300, .has_seed = 1, .seed = 20261108 };
  int                 rc;

  memset (g_trace, 0, sizeof *g_trace);
  g_trace->hash = 14695981039346656037ull;
  g_events = events;
  rc = nofork ? hegel_run_test_nofork_with (testCountAbove, &o)
              : hegel_run_test_with (testCountAbove, &o);
  *out = *g_trace;
  return (rc);
}

static
void
format_list (
const Trace *               t,
char *                      buf,
size_t                      cap)
{
  size_t              k = 0;

  k += (size_t) snprintf (buf + k, cap - k, "[");
  for (int i = 0; i < t->last_n && k < cap; i ++)
    k += (size_t) snprintf (buf + k, cap - k, "%s%d", i ? ", " : "", t->last_xs[i]);
  if (k < cap)
    snprintf (buf + k, cap - k, "]");
}

int
main (void)
{
  Trace               t[4];                   /* fork off, fork on, nofork off, nofork on */
  int                 rc[4];
  char                lists[4][128];
  int                 ok = 1;

  g_trace = mmap (NULL, sizeof *g_trace, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (g_trace == MAP_FAILED) {
    perror ("mmap");
    return (2);
  }

  for (int i = 0; i < 4; i ++) {
    rc[i] = run (i >= 2, i & 1, &t[i]);
    format_list (&t[i], lists[i], sizeof lists[i]);
    printf ("%-6s events %-3s: rc %d, %llu cases, hash %016llx, shrunk %s\n",
            i >= 2 ? "nofork" : "fork", (i & 1) ? "on" : "off", rc[i],
            (unsigned long long) t[i].cases, (unsigned long long) t[i].hash, lists[i]);
    if (rc[i] != 1)
      ok = 0;
  }
  for (int m = 0; m < 4; m += 2)
    if (t[m].hash != t[m + 1].hash || t[m].cases != t[m + 1].cases ||
        strcmp (lists[m], lists[m + 1]) != 0)
      ok = 0;
  if (strcmp (lists[1], lists[3]) != 0)
    ok = 0;

  printf ("shrunk: %s\n", ok ? lists[1] : "MISMATCH");
  return (1);
}
