/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: shrinking with an engine-owned length (HEGEL_LEN) and a
**       back-filled count (HEGEL_LEN_OF), no bindings:
**
**     HEGEL_STRUCT (Bag,
**         HEGEL_ARR_OF (HEGEL_LEN (0, 30), HEGEL_INT (0, 100)),
**         HEGEL_LEN_OF (Bag, items));
**
** Layer 1: two predicates on a bag — contains_77() and the one from
**          test_binding_shrink_bag, third_is_7_of_many() (n > 3 &&
**          items[2] == 7).
** Layer 2: nofork runs that record the last failing bag.
** Layer 3: for seeds 1..3, the minimal failures are [77] and
**          [0,0,7,0], with n always equal to the drawn length.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int *               items;
  int                 n;
} Bag;

/* ---- Layer 1: functions under test ---- */

static
int
contains_77 (
const Bag *         b)
{
  int                 i;

  for (i = 0; i < b->n; i ++)
    if (b->items[i] == 77)
      return (1);
  return (0);
}

static
int
third_is_7_of_many (
const Bag *         b)
{
  return (b->n > 3 && b->items[2] == 7);
}

/* ---- Layer 2: hegel test ---- */

static hegel_schema_t schema;
static int          (*predicate) (const Bag *);
static int            last_n;
static int            last_items[32];
static int            count_mismatch;

static
void
test_bag (
hegel_testcase *    tc)
{
  Bag *               b;
  hegel_shape *       sh;
  int                 bad;

  sh = hegel_schema_draw (tc, schema, (void **) &b);
  if (hegel_shape_array_len (hegel_shape_field (sh, 0)) != b->n)
    count_mismatch = 1;
  bad = predicate (b);
  if (bad) {
    last_n = b->n;
    memcpy (last_items, b->items, (size_t) b->n * sizeof (int));
  }
  hegel_shape_free (sh);
  HEGEL_ASSERT (!bad, "n=%d", last_n);
}

/* ---- Layer 3: runner ---- */

static int            failures;

static
void
check (
int                 ok,
const char *        what,
int                 seed)
{
  printf ("  seed %d: %-50s %s\n", seed, what, ok ? "ok" : "FAILED");
  if (!ok)
    failures ++;
}

static
void
shrink (
int                 (*pred) (const Bag *),
int                 seed)
{
  hegel_run_options o = { .test_cases = 1000, .has_seed = 1, .seed = seed };

  predicate = pred;
  last_n = -1;
  hegel_run_test_nofork_with (test_bag, &o);
}

int
main (void)
{
  int                 seed;

  schema = HEGEL_STRUCT (Bag,
      HEGEL_ARR_OF (HEGEL_LEN (0, 30), HEGEL_INT (0, 100)),
      HEGEL_LEN_OF (Bag, items));

  printf ("Shrinking with HEGEL_LEN + HEGEL_LEN_OF...\n");
  for (seed = 1; seed <= 3; seed ++) {
    shrink (contains_77, seed);
    check (last_n == 1 && last_items[0] == 77, "contains_77 -> [77]", seed);
    shrink (third_is_7_of_many, seed);
    check (last_n == 4 && last_items[0] == 0 && last_items[1] == 0
           && last_items[2] == 7 && last_items[3] == 0,
           "third_is_7_of_many -> [0,0,7,0]", seed);
  }
  check (!count_mismatch, "n always equals the drawn length", 0);

  hegel_schema_free (schema);
  if (failures) {
    fprintf (stderr, "%d check(s) failed\n", failures);
    return (1);
  }
  return (0);
}
