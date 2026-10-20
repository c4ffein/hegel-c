/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful PBT for lib/idr.c — the kernel ID allocator (IDA simple
** variant: ida_alloc_range / ida_free / ida_destroy).
**
** The idr/ida is a thin wrapper over xarray; bugs would manifest as:
**   - duplicate IDs handed out
**   - IDs returned outside the requested [min, max] window
**   - free/alloc round-trip not freeing the bit
**   - bookkeeping divergence between shadow and the bitmap
**
** State machine:
**   rule_alloc(min, max)   — always applicable.  ida_alloc_range; if it
**                            returns >= 0 we add it to the shadow set
**                            (must not already be present), assert it is
**                            in [min, max], and assert it does NOT exceed
**                            INT_MAX (kernel contract).
**   rule_alloc_then_drop   — same as rule_alloc but if a non-error id is
**                            handed out we immediately free it again.
**                            Stresses the alloc/free round-trip without
**                            growing the bitmap.
**   rule_free_existing     — PRECONDITION: shadow non-empty.  Pick an
**                            ordinal, ida_free that ID, remove from
**                            shadow.
**
** Invariants checked after every rule:
**   1. ida_find_first_range(min,max) returns the lowest shadow entry in
**      [min,max] (or -ENOENT-equivalent when none).
**   2. For every id we expect to be allocated: ida_exists(id) is true.
**   3. ida_is_empty() iff shadow is empty.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

#include <linux/idr.h>
#include <linux/xarray.h>

/* Provided by radix-tree harness init code. */
extern void rcu_register_thread (void);
extern void radix_tree_init (void);

/* Test parameters: keep ID space small enough that ENOSPC and re-alloc
** of freed slots both happen frequently within a sequence. */
#define MAX_TRACK   64
#define ID_HI_CAP   200

typedef struct {
  struct ida ida;
  /* Shadow set as a sorted dense array; small enough to scan linearly. */
  int  ids[MAX_TRACK];
  int  n;

  /* Coverage counters (per-case scope). */
  long c_alloc_ok;
  long c_alloc_nospc;
  long c_alloc_drop;
  long c_free;
  long c_inv;
} st_t;

/* Aggregate counters across cases — printed at end of main(). */
static long g_alloc_ok;
static long g_alloc_nospc;
static long g_alloc_drop;
static long g_free;
static long g_inv;

/* ---- shadow helpers ---- */

static int
shadow_find (st_t *s, int id)
{
  for (int i = 0; i < s->n; i++)
    if (s->ids[i] == id) return i;
  return -1;
}

static void
shadow_insert (st_t *s, int id)
{
  HEGEL_ASSERT (s->n < MAX_TRACK,
                "shadow_insert: shadow full (n=%d) — increase MAX_TRACK", s->n);
  HEGEL_ASSERT (shadow_find (s, id) < 0,
                "shadow_insert: duplicate id=%d already in shadow", id);
  /* Insert in sorted order so ida_find_first_range invariants are easy. */
  int i = s->n;
  while (i > 0 && s->ids[i - 1] > id) { s->ids[i] = s->ids[i - 1]; i--; }
  s->ids[i] = id;
  s->n++;
}

static void
shadow_remove_at (st_t *s, int idx)
{
  memmove (&s->ids[idx], &s->ids[idx + 1], (s->n - idx - 1) * sizeof (int));
  s->n--;
}

static int
shadow_first_in_range (st_t *s, int min, int max)
{
  for (int i = 0; i < s->n; i++)
    if (s->ids[i] >= min && s->ids[i] <= max) return s->ids[i];
  return -1;
}

/* ---- setup / teardown ---- */

static void *
ida_setup (void)
{
  st_t *s = calloc (1, sizeof (*s));
  if (!s) return NULL;
  ida_init (&s->ida);
  return s;
}

static void
ida_teardown (void *state)
{
  if (!state) return;
  st_t *s = state;
  /* Aggregate counters before destroying. */
  g_alloc_ok    += s->c_alloc_ok;
  g_alloc_nospc += s->c_alloc_nospc;
  g_alloc_drop  += s->c_alloc_drop;
  g_free        += s->c_free;
  g_inv         += s->c_inv;
  ida_destroy (&s->ida);
  free (s);
}

/* ---- preconditions ---- */

static bool
have_ids (const void *state)
{
  const st_t *s = state;
  return s->n > 0;
}

static bool
have_room (const void *state)
{
  const st_t *s = state;
  return s->n < MAX_TRACK;
}

/* ---- rules ---- */

static void
rule_alloc (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  /* Draw a [min, max] window in [0, ID_HI_CAP], with min <= max. */
  int a = hegel_draw_int (tc, 0, ID_HI_CAP);
  int b = hegel_draw_int (tc, 0, ID_HI_CAP);
  unsigned int min_id = (unsigned int) (a < b ? a : b);
  unsigned int max_id = (unsigned int) (a < b ? b : a);

  int got = ida_alloc_range (&s->ida, min_id, max_id, GFP_KERNEL);
  if (got >= 0) {
    HEGEL_ASSERT ((unsigned int) got >= min_id && (unsigned int) got <= max_id,
                  "ida_alloc_range returned id=%d outside [%u,%u]",
                  got, min_id, max_id);
    HEGEL_ASSERT (shadow_find (s, got) < 0,
                  "ida_alloc_range returned id=%d already allocated", got);
    shadow_insert (s, got);
    s->c_alloc_ok++;
  } else {
    HEGEL_ASSERT (got == -ENOSPC || got == -ENOMEM,
                  "ida_alloc_range returned unexpected error %d", got);
    /* If ENOSPC, every id in [min,max] must already be in shadow. */
    if (got == -ENOSPC) {
      int gap = -1;
      for (unsigned int x = min_id; x <= max_id; x++) {
        if (shadow_find (s, (int) x) < 0) { gap = (int) x; break; }
        if (x == max_id) break;
      }
      HEGEL_ASSERT (gap < 0,
                    "ida_alloc_range returned ENOSPC but id=%d in [%u,%u] is free",
                    gap, min_id, max_id);
    }
    s->c_alloc_nospc++;
  }
}

static void
rule_alloc_then_drop (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  int a = hegel_draw_int (tc, 0, ID_HI_CAP);
  int b = hegel_draw_int (tc, 0, ID_HI_CAP);
  unsigned int min_id = (unsigned int) (a < b ? a : b);
  unsigned int max_id = (unsigned int) (a < b ? b : a);

  int got = ida_alloc_range (&s->ida, min_id, max_id, GFP_KERNEL);
  if (got >= 0) {
    HEGEL_ASSERT ((unsigned int) got >= min_id && (unsigned int) got <= max_id,
                  "ida_alloc_range/drop returned id=%d outside [%u,%u]",
                  got, min_id, max_id);
    HEGEL_ASSERT (shadow_find (s, got) < 0,
                  "ida_alloc_range/drop returned id=%d already allocated", got);
    /* Free immediately — round-trip without growing the shadow. */
    ida_free (&s->ida, (unsigned int) got);
    s->c_alloc_drop++;
  } else {
    HEGEL_ASSERT (got == -ENOSPC || got == -ENOMEM,
                  "ida_alloc_range/drop returned unexpected error %d", got);
    s->c_alloc_nospc++;
  }
}

static void
rule_free_existing (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  int pick = hegel_draw_int (tc, 0, s->n - 1);
  int id   = s->ids[pick];

  ida_free (&s->ida, (unsigned int) id);
  shadow_remove_at (s, pick);
  s->c_free++;
}

/* ---- invariants ---- */

static void
inv_consistent (const void *cstate, hegel_testcase *tc)
{
  (void) tc;
  st_t *s = (st_t *) cstate;   /* invariants are read-only by contract */
  s->c_inv++;

  /* (1) ida_is_empty() iff shadow empty. */
  bool sut_empty = ida_is_empty (&s->ida);
  bool shadow_empty = (s->n == 0);
  HEGEL_ASSERT (sut_empty == shadow_empty,
                "ida_is_empty=%d but shadow_n=%d (mismatch)",
                sut_empty, s->n);

  /* (2) Every shadow id ida_exists. */
  for (int i = 0; i < s->n; i++) {
    int id = s->ids[i];
    HEGEL_ASSERT (ida_exists ((struct ida *) &s->ida, (unsigned int) id),
                  "shadow id=%d not reported by ida_exists", id);
  }

  /* (3) ida_find_first_range over a few probes matches shadow. */
  static const struct { int lo, hi; } probes[] = {
    {0, ID_HI_CAP}, {0, 0}, {1, 5}, {10, 50}, {ID_HI_CAP / 2, ID_HI_CAP},
  };
  for (size_t p = 0; p < sizeof probes / sizeof probes[0]; p++) {
    int lo = probes[p].lo, hi = probes[p].hi;
    int sut = ida_find_first_range ((struct ida *) &s->ida,
                                    (unsigned int) lo, (unsigned int) hi);
    int shadow_first = shadow_first_in_range (s, lo, hi);
    if (shadow_first < 0) {
      /* Kernel returns a negative errno (varies: -ENOENT in newer trees,
      ** -EINVAL in others) when nothing is found.  Just check < 0. */
      HEGEL_ASSERT (sut < 0,
                    "ida_find_first_range([%d,%d])=%d but shadow has none",
                    lo, hi, sut);
    } else {
      HEGEL_ASSERT (sut == shadow_first,
                    "ida_find_first_range([%d,%d])=%d but shadow first=%d",
                    lo, hi, sut, shadow_first);
    }
  }
}

/* ---- main ---- */

int
main (void)
{
  rcu_register_thread ();
  radix_tree_init ();

  printf ("ida stateful PBT...\n");

  hegel_state_machine *sm = hegel_state_machine_new (ida_setup, ida_teardown);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  if (hegel_state_machine_add_rule_with_precondition
        (sm, "alloc",          have_room, rule_alloc)            != 0) return 1;
  if (hegel_state_machine_add_rule_with_precondition
        (sm, "alloc_then_drop", have_room, rule_alloc_then_drop) != 0) return 1;
  if (hegel_state_machine_add_rule_with_precondition
        (sm, "free_existing",   have_ids,  rule_free_existing)   != 0) return 1;

  if (hegel_state_machine_add_invariant
        (sm, "consistent",      inv_consistent)                  != 0) return 1;

  hegel_state_machine_run_n (sm, 200);

  printf ("PASSED   alloc_ok=%ld nospc=%ld alloc_drop=%ld free=%ld inv=%ld\n",
          g_alloc_ok, g_alloc_nospc, g_alloc_drop, g_free, g_inv);

  hegel_state_machine_free (sm);
  return 0;
}
