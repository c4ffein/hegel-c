/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: variable pools hand back ids that were added, and consumed ids
**       are not handed out again.
**
** Layer 1: a tiny handle table — open() returns a slot, close() frees
**          it (correct implementation).
** Layer 2: a short random program of opens and closes.  Opens add the
**          new handle's slot to a pool; closes draw (consume) a pool id,
**          so the engine picks — and shrinks — which handle to close.
**          The table must never close a slot twice.  Runs under both
**          runners.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdbool.h>

#include "hegel_c.h"

#define SLOTS 64

/* ---- Layer 1: function under test ---- */

static bool in_use[SLOTS];

static
int
table_open (void)
{
  int                 i;

  for (i = 0; i < SLOTS; i ++)
    if (!in_use[i]) {
      in_use[i] = true;
      return (i);
    }
  return (-1);
}

static
int
table_close (
int                         slot)
{
  if (slot < 0 || slot >= SLOTS || !in_use[slot])
    return (-1);
  in_use[slot] = false;
  return (0);
}

/* ---- Layer 2: hegel test ---- */

/* Pool ids are opaque int64s chosen by the engine: map them to slots. */
static int64_t map_var[256];
static int     map_slot[256];
static int     map_n;

static
int
slot_of (
int64_t                     var)
{
  int                 i;

  for (i = 0; i < map_n; i ++)
    if (map_var[i] == var)
      return (map_slot[i]);
  return (-1);
}

static
void
testPool (
hegel_testcase *            tc)
{
  hegel_pool          pool;
  int                 open_count;
  int                 steps;
  int                 i;

  for (i = 0; i < SLOTS; i ++)
    in_use[i] = false;
  map_n = 0;
  pool = hegel_pool_new (tc);
  open_count = 0;
  steps = hegel_draw_int (tc, 1, 30);
  for (i = 0; i < steps; i ++) {
    if (open_count == 0 || (open_count < SLOTS && hegel_draw_bool (tc))) {
      int64_t         var;
      int             slot;

      slot = table_open ();
      HEGEL_ASSERT (slot >= 0, "table full with %d open", open_count);
      var = hegel_pool_add (tc, pool);
      HEGEL_ASSERT (slot_of (var) < 0, "pool id %lld handed out twice", (long long) var);
      map_var[map_n] = var;
      map_slot[map_n ++] = slot;
      open_count ++;
    } else {
      int64_t         var;

      var = hegel_pool_draw (tc, pool, true);
      HEGEL_ASSERT (slot_of (var) >= 0, "pool drew unknown id %lld", (long long) var);
      HEGEL_ASSERT (table_close (slot_of (var)) == 0, "slot %d closed twice", slot_of (var));
      open_count --;
    }
  }
  hegel_pool_free (tc, pool);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing variable pools...\n");
  if (hegel_run_test_result_n (testPool, 100) != 0)
    return (1);
  hegel_run_test_nofork_n (testPool, 100);
  printf ("OK\n");

  return (0);
}
