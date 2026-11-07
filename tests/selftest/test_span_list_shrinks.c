/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: a list whose failure depends on ONE element shrinks to a
**       single-element list — with the HEGEL_SPAN_LIST /
**       HEGEL_SPAN_LIST_ELEMENT convention, and without it.
**
** EXPECTED_SHRINK: spans 1, no spans 1
**
** What this pins down, measured on libhegel 0.48.0 (2026-10-30, 20 to
** 50 seeds per variant, drawn length 0..30, failure on one element):
** both generators shrank to a one-element list on every seed, spans or
** not, fixed-width or variable-width elements, the bad element anywhere
** or last.  Spans changed only how much work the shrinker did, and not
** in one direction: with variable-width elements (this file's shape)
** about 40% fewer test executions (≈1500-1800 vs ≈2700-3000); with a
** nested list inside each element about 2.7x MORE (≈10200 vs ≈3800).
** So the convention is a hint, not what makes lists shrink; this test
** keeps both variants honest instead of claiming spans are needed.
**
** Layer 1: batch_cost() — the total cost of a batch of records.  Bug:
**          a kind-3 record with a value over 100 overflows its cost
**          field (cost goes negative).
** Layer 2: draw a list of records (drawn length; each record draws a
**          kind, a value and a variable-length payload) and check the
**          batch cost is non-negative — once wrapped in list/element
**          spans, once with bare draws.
** Layer 3: runs both nofork, prints the length of each run's last
**          failing list for the Makefile's TESTS_SHRINK check, exits 1.
**
** Expected: EXIT non-zero with "shrunk: spans 1, no spans 1".
*/
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"

#define MAX_RECORDS 30
#define MAX_PAYLOAD 6

typedef struct {
  int                 kind;
  int                 value;
  int                 payload[MAX_PAYLOAD];
  int                 n_payload;
} Record;

/* ---- Layer 1: function under test ---- */

static
int
record_cost (
const Record *              r)
{
  signed char         cost;   /* BUG: too narrow for kind 3's scaling */
  int                 i;
  int                 extra = 0;

  for (i = 0; i < r->n_payload; i ++)
    extra += r->payload[i] > 0;
  if (r->kind == 3) {
    cost = (signed char) (r->value + 27);   /* > 127 wraps negative */
    return (cost + extra);
  }
  return (r->value + extra);
}

static
long
batch_cost (
const Record *              rs,
int                         n)
{
  long                total = 0;
  int                 i;

  for (i = 0; i < n; i ++) {
    int c = record_cost (&rs[i]);
    if (c < 0)
      return (-1);
    total += c;
  }
  return (total);
}

/* ---- Layer 2: hegel tests ---- */

static
void
draw_record (
hegel_testcase *            tc,
Record *                    r)
{
  int                 i;

  r->kind  = hegel_draw_int (tc, 0, 5);
  r->value = hegel_draw_int (tc, 0, 1000);
  r->n_payload = hegel_draw_int (tc, 0, MAX_PAYLOAD);
  for (i = 0; i < r->n_payload; i ++)
    r->payload[i] = hegel_draw_int (tc, 0, 1000);
}

static int g_last_n;

static
void
check_batch (
hegel_testcase *            tc,
const Record *              rs,
int                         n)
{
  char                note[64];
  long                cost;

  cost = batch_cost (rs, n);
  if (cost < 0)
    g_last_n = n;
  snprintf (note, sizeof note, "%d record(s)", n);
  hegel_note (tc, note);
  HEGEL_ASSERT (cost >= 0, "batch of %d record(s) has negative cost", n);
}

static
void
testWithSpans (
hegel_testcase *            tc)
{
  Record              rs[MAX_RECORDS];
  int                 n;
  int                 i;

  hegel_start_span (tc, HEGEL_SPAN_LIST);
  n = hegel_draw_int (tc, 0, MAX_RECORDS);
  for (i = 0; i < n; i ++) {
    hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);
    draw_record (tc, &rs[i]);
    hegel_stop_span (tc, 0);
  }
  hegel_stop_span (tc, 0);
  check_batch (tc, rs, n);
}

static
void
testWithoutSpans (
hegel_testcase *            tc)
{
  Record              rs[MAX_RECORDS];
  int                 n;
  int                 i;

  n = hegel_draw_int (tc, 0, MAX_RECORDS);
  for (i = 0; i < n; i ++)
    draw_record (tc, &rs[i]);
  check_batch (tc, rs, n);
}

/* ---- Layer 3: runner (see Makefile TESTS_SHRINK) ---- */

int
main (
int                 argc,
char *              argv[])
{
  hegel_run_options   o;
  int                 with;
  int                 without;

  (void) argc;
  (void) argv;

  memset (&o, 0, sizeof o);
  o.test_cases = 1000;
  printf ("Testing that a list shrinks to the one element that fails...\n");

  g_last_n = -1;
  if (hegel_run_test_nofork_with (testWithSpans, &o) == 0) {
    printf ("  with spans: the bug was not found\n");
    return (0);                               /* TESTS_SHRINK: exit 0 is the failure */
  }
  with = g_last_n;

  g_last_n = -1;
  if (hegel_run_test_nofork_with (testWithoutSpans, &o) == 0) {
    printf ("  without spans: the bug was not found\n");
    return (0);
  }
  without = g_last_n;

  printf ("shrunk: spans %d, no spans %d\n", with, without);
  return (1);
}
