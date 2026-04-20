/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_ARRAY(int, ...) — length variation, element plumbing,
** empty-list support.
**
** Three sub-tests, each running N cases in nofork mode so a file-scope
** counter can aggregate across cases:
**
**   1. Length variation — ARRAY(int(0, 9), 2, 10).  Asserts we see at
**      least two distinct lengths across N cases.  Catches a generator
**      stuck at a single length (min or max).
**
**   2. Pinned element — ARRAY(int(7, 7), 1, 5).  Every element must
**      equal 7 exactly.  Sharp plumbing check: a bug writing to the
**      wrong offset, truncating, or reusing a slot fails here even
**      though a range-based test might pass.
**
**   3. Empty-allowed — ARRAY(int(0, 0), 0, 5).  Asserts both empty
**      (len=0) and non-empty (len>0) lists are seen.  Empty arrays
**      are a distinct code path (null items pointer, count==0).
**
** Expected: EXIT 0.
*/
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

#define N_CASES   4

typedef struct {
  int *               items;
  int                 n;
} Bag;

/* ---- Test 1: length variation ---- */

static hegel_schema_t  range_schema;
static int             range_total;
static int             range_min_len = INT_MAX;
static int             range_max_len = INT_MIN;

static
void
testListRange (
hegel_testcase *            tc)
{
  Bag *                b;
  hegel_shape *        sh;
  int                  i;

  sh = hegel_schema_draw (tc, range_schema, (void **) &b);

  range_total++;
  HEGEL_ASSERT (b->n >= 2 && b->n <= 10,
                "range list length %d not in [2,10]", b->n);
  for (i = 0; i < b->n; i++) {
    HEGEL_ASSERT (b->items[i] >= 0 && b->items[i] <= 9,
                  "range elem[%d]=%d not in [0,9]", i, b->items[i]);
  }
  if (b->n < range_min_len) range_min_len = b->n;
  if (b->n > range_max_len) range_max_len = b->n;

  hegel_shape_free (sh);
}

/* ---- Test 2: pinned element 7 ---- */

static hegel_schema_t  pinned_schema;
static int             pinned_total;

static
void
testListPinned (
hegel_testcase *            tc)
{
  Bag *                b;
  hegel_shape *        sh;
  int                  i;

  sh = hegel_schema_draw (tc, pinned_schema, (void **) &b);

  pinned_total++;
  HEGEL_ASSERT (b->n >= 1 && b->n <= 5,
                "pinned list length %d not in [1,5]", b->n);
  for (i = 0; i < b->n; i++) {
    HEGEL_ASSERT (b->items[i] == 7,
                  "pinned elem[%d]=%d, expected 7", i, b->items[i]);
  }

  hegel_shape_free (sh);
}

/* ---- Test 3: empty-allowed ---- */

static hegel_schema_t  empty_schema;
static int             empty_total;
static int             empty_zero;
static int             empty_nonzero;

static
void
testListEmpty (
hegel_testcase *            tc)
{
  Bag *                b;
  hegel_shape *        sh;
  int                  i;

  sh = hegel_schema_draw (tc, empty_schema, (void **) &b);

  empty_total++;
  HEGEL_ASSERT (b->n >= 0 && b->n <= 5,
                "empty-allowed list length %d not in [0,5]", b->n);
  for (i = 0; i < b->n; i++) {
    HEGEL_ASSERT (b->items[i] == 0,
                  "empty-allowed elem[%d]=%d, expected 0",
                  i, b->items[i]);
  }
  if (b->n == 0) empty_zero++;
  else           empty_nonzero++;

  hegel_shape_free (sh);
}

/* ---- Runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  {
    hegel_schema_t arr = HEGEL_ARRAY (HEGEL_INT (0, 9), 2, 10);
    range_schema = HEGEL_STRUCT (Bag,
        HEGEL_FACET (arr, value),
        HEGEL_FACET (arr, size));
    hegel_schema_free (arr);
  }
  {
    hegel_schema_t arr = HEGEL_ARRAY (HEGEL_INT (7, 7), 1, 5);
    pinned_schema = HEGEL_STRUCT (Bag,
        HEGEL_FACET (arr, value),
        HEGEL_FACET (arr, size));
    hegel_schema_free (arr);
  }
  {
    hegel_schema_t arr = HEGEL_ARRAY (HEGEL_INT (0, 0), 0, 5);
    empty_schema = HEGEL_STRUCT (Bag,
        HEGEL_FACET (arr, value),
        HEGEL_FACET (arr, size));
    hegel_schema_free (arr);
  }

  printf ("Testing HEGEL_ARRAY int (length variation)...\n");
  hegel_run_test_nofork_n (testListRange, N_CASES);
  if (range_max_len <= range_min_len) {
    fprintf (stderr,
             "length variation: only length %d seen across %d cases\n",
             range_min_len, range_total);
    return (1);
  }
  printf ("  PASSED (lengths %d..%d in %d cases)\n",
          range_min_len, range_max_len, range_total);

  printf ("Testing HEGEL_ARRAY int (pinned element 7)...\n");
  hegel_run_test_nofork_n (testListPinned, N_CASES);
  printf ("  PASSED (%d cases)\n", pinned_total);

  printf ("Testing HEGEL_ARRAY int (empty-allowed)...\n");
  hegel_run_test_nofork_n (testListEmpty, N_CASES);
  if (empty_zero == 0 || empty_nonzero == 0) {
    fprintf (stderr,
             "empty-allowed collapsed: %d empty / %d nonempty / %d total\n",
             empty_zero, empty_nonzero, empty_total);
    return (1);
  }
  printf ("  PASSED (%d empty, %d nonempty)\n", empty_zero, empty_nonzero);

  hegel_schema_free (range_schema);
  hegel_schema_free (pinned_schema);
  hegel_schema_free (empty_schema);
  return (0);
}
