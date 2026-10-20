/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful PBT against the SNAPSHOTTED (buggy) lib/maple_tree.c,
** focused on the ALLOCATION API surface that test_maple_basic.c does
** not exercise:
**   mtree_alloc_cyclic / mas_alloc_cyclic — cyclic id allocator
**   mtree_alloc_range  — first-fit forward allocator
**   mtree_alloc_rrange — first-fit reverse (top-down) allocator
**
** Rules (state machine, nofork):
**   rule_alloc_cyclic     — mtree_alloc_cyclic in a small range
**   rule_alloc_range      — mtree_alloc_range (size=1, full range)
**   rule_alloc_rrange     — mtree_alloc_rrange (size=1, full range)
**   rule_erase_low        — erase a low id (forces wrap on next cyclic)
**   rule_erase_high       — erase a high id
**   rule_erase_existing   — erase a randomly-picked occupied id
**   rule_fill_to_near_full — burst-fill via cyclic until ENOSPC, then free one
**
** Shadow model: dense bool[KEYSPACE] of which ids are occupied.  We
** also maintain `next` per-tree for the cyclic allocator (parallel to
** what mtree_alloc_cyclic does internally) and assert it matches the
** allocator's view.
**
** Properties:
**   P1  alloc returns id in [range_lo, range_hi]
**   P2  alloc returns id that was previously FREE (shadow check)
**   P3  alloc ENOSPC <=> shadow shows range completely full
**   P4  cyclic wrap return value (0/1) matches model
**   P5  *next after alloc is start+1
**   P6  invariant: load(id)==value iff shadow[id]==occupied
**
** Bounded keyspace keeps the shadow cheap and makes wraparound likely. */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

#include <linux/bug.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/rcupdate.h>
#include <linux/maple_tree.h>
#include <linux/xarray.h>

extern void rcu_register_thread (void);
extern void radix_tree_init (void);

/* Small keyspace so the cyclic allocator wraps frequently within a
** single test case. */
#define KEYSPACE 16

typedef struct {
  struct maple_tree mt;
  bool          present[KEYSPACE]; /* shadow occupancy */
  unsigned long values[KEYSPACE];  /* shadow value (when present) */
  int           n_present;
  unsigned long next;              /* shadow `next` cursor for cyclic alloc */

  /* Coverage counters. */
  long c_cyc;
  long c_cyc_ok;
  long c_cyc_wrap;
  long c_cyc_busy;
  long c_range;
  long c_rrange;
  long c_erase;
  long c_fill;
  long c_load;
} st_t;

/* Globals accumulated across cases. */
static long g_cases;
static long g_c_cyc, g_c_cyc_wrap, g_c_cyc_busy;
static long g_c_range, g_c_rrange, g_c_erase, g_c_fill, g_c_load;

static inline void *
pack_v (unsigned long v)
{
  return xa_mk_value (v);
}

static inline unsigned long
unpack_v (void *p)
{
  return xa_to_value (p);
}

/* --------- setup / teardown --------- */

static void *
setup_fn (void)
{
  st_t *st = calloc (1, sizeof (*st));
  if (!st) return NULL;
  /* MT_FLAGS_ALLOC_RANGE is required by mtree_alloc_* functions. */
  mt_init_flags (&st->mt, MT_FLAGS_ALLOC_RANGE);
  st->next = 0;
  return st;
}

static void
teardown_fn (void *s)
{
  st_t *st = s;
  if (!st) return;
  mtree_destroy (&st->mt);
  g_cases++;
  g_c_cyc       += st->c_cyc;
  g_c_cyc_wrap  += st->c_cyc_wrap;
  g_c_cyc_busy  += st->c_cyc_busy;
  g_c_range     += st->c_range;
  g_c_rrange    += st->c_rrange;
  g_c_erase     += st->c_erase;
  g_c_fill      += st->c_fill;
  g_c_load      += st->c_load;
  free (st);
}

/* --------- preconditions --------- */

static bool
any_present (const void *s)
{
  const st_t *st = s;
  return st->n_present > 0;
}

static bool
any_present_low (const void *s)
{
  const st_t *st = s;
  for (int i = 0; i < KEYSPACE / 2; i++)
    if (st->present[i]) return true;
  return false;
}

static bool
any_present_high (const void *s)
{
  const st_t *st = s;
  for (int i = KEYSPACE / 2; i < KEYSPACE; i++)
    if (st->present[i]) return true;
  return false;
}

/* --------- helpers --------- */

/* Whether the inclusive [lo..hi] subrange of the shadow is full. */
static bool
shadow_range_full (const st_t *st, unsigned long lo, unsigned long hi)
{
  if (lo >= KEYSPACE) lo = KEYSPACE - 1;
  if (hi >= KEYSPACE) hi = KEYSPACE - 1;
  for (unsigned long i = lo; i <= hi; i++)
    if (!st->present[i]) return false;
  return true;
}

/* Find the lowest free slot in [lo..hi]; returns KEYSPACE if none. */
static unsigned long
shadow_first_free (const st_t *st, unsigned long lo, unsigned long hi)
{
  if (hi >= KEYSPACE) hi = KEYSPACE - 1;
  for (unsigned long i = lo; i <= hi; i++)
    if (!st->present[i]) return i;
  return KEYSPACE;
}

/* Find the highest free slot in [lo..hi]; returns KEYSPACE if none. */
static unsigned long
shadow_last_free (const st_t *st, unsigned long lo, unsigned long hi)
{
  if (hi >= KEYSPACE) hi = KEYSPACE - 1;
  for (long i = (long) hi; i >= (long) lo; i--)
    if (!st->present[i]) return (unsigned long) i;
  return KEYSPACE;
}

/* --------- rule: mtree_alloc_cyclic --------- */
/* This is the headline rule: small range, repeatedly invoked, will
** quickly drive the allocator into wraparound territory. */
static void
rule_alloc_cyclic (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  /* Draw a sub-range within the keyspace; bias towards the full one. */
  unsigned long lo = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long hi = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  if (hi < lo) { unsigned long t = lo; lo = hi; hi = t; }
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);

  unsigned long start = ULONG_MAX;
  unsigned long next_before = st->next;
  int rc = mtree_alloc_cyclic (&st->mt, &start, pack_v (v),
                               lo, hi, &st->next, GFP_KERNEL);
  st->c_cyc++;

  if (rc == -EBUSY) {
    /* P3: ENOSPC must mean the range was actually full at the moment
    ** the search began.  The cyclic search inspects [next..hi] then
    ** [lo..hi], so "full" = full [lo..hi]. */
    st->c_cyc_busy++;
    HEGEL_ASSERT (shadow_range_full (st, lo, hi),
                  "alloc_cyclic -EBUSY but shadow has a free slot in [%lu..%lu] "
                  "(next_before=%lu)", lo, hi, next_before);
    return;
  }

  HEGEL_ASSERT (rc == 0 || rc == 1,
                "alloc_cyclic returned unexpected rc=%d (lo=%lu hi=%lu next_before=%lu)",
                rc, lo, hi, next_before);

  /* P1: returned id in [lo..hi]. */
  HEGEL_ASSERT (start >= lo && start <= hi,
                "alloc_cyclic returned start=%lu outside [%lu..%lu]",
                start, lo, hi);

  /* Defensive bound (we tightened keyspace, but be careful in case
  ** anyone changes lo/hi to be larger). */
  HEGEL_ASSERT (start < KEYSPACE,
                "alloc_cyclic returned start=%lu beyond shadow KEYSPACE=%d",
                start, KEYSPACE);

  /* P2: the chosen slot was previously free. */
  HEGEL_ASSERT (!st->present[start],
                "alloc_cyclic chose start=%lu but shadow says occupied "
                "(prev value=%lu, new value=%lu)",
                start, st->values[start], v);

  /* P4: wrap return value semantics.
  ** rc == 1 ("wrapped") means the allocator either:
  **   (a) had to retry from `min` because next..hi had no space, or
  **   (b) wrapped through ULONG_MAX (impossible at our scale).
  ** Approximate model: if next_before clamped above lo and there was
  ** no free slot in [max(next_before, lo) .. hi], rc must be 1. */
  unsigned long search_start = next_before > lo ? next_before : lo;
  bool free_above_next = (shadow_first_free (st, search_start, hi) != KEYSPACE);
  if (!free_above_next) {
    HEGEL_ASSERT (rc == 1,
                  "alloc_cyclic should have wrapped (no free in [%lu..%lu]) but rc=%d, "
                  "start=%lu (lo=%lu, hi=%lu, next_before=%lu)",
                  search_start, hi, rc, start, lo, hi, next_before);
    st->c_cyc_wrap++;
  }

  /* Update shadow. */
  st->present[start] = true;
  st->values[start] = v;
  st->n_present++;

  /* P5: *next is start + 1 after success (allocator's documented behavior). */
  HEGEL_ASSERT (st->next == start + 1,
                "alloc_cyclic *next mismatch: got %lu, expected %lu (start=%lu)",
                st->next, start + 1, start);
}

/* --------- rule: mtree_alloc_range (forward, first-fit) --------- */
static void
rule_alloc_range (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long lo = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long hi = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  if (hi < lo) { unsigned long t = lo; lo = hi; hi = t; }
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);

  unsigned long start = ULONG_MAX;
  int rc = mtree_alloc_range (&st->mt, &start, pack_v (v),
                              1 /* size */, lo, hi, GFP_KERNEL);
  st->c_range++;

  if (rc == -EBUSY) {
    HEGEL_ASSERT (shadow_range_full (st, lo, hi),
                  "alloc_range -EBUSY but shadow has free slot in [%lu..%lu]",
                  lo, hi);
    return;
  }

  HEGEL_ASSERT (rc == 0,
                "alloc_range unexpected rc=%d (lo=%lu hi=%lu)", rc, lo, hi);
  HEGEL_ASSERT (start >= lo && start <= hi,
                "alloc_range start=%lu outside [%lu..%lu]", start, lo, hi);
  HEGEL_ASSERT (start < KEYSPACE,
                "alloc_range start=%lu beyond KEYSPACE", start);
  HEGEL_ASSERT (!st->present[start],
                "alloc_range chose occupied slot %lu", start);

  /* P (forward first-fit): the returned id must be the LOWEST free in [lo..hi]. */
  unsigned long want = shadow_first_free (st, lo, hi);
  HEGEL_ASSERT (start == want,
                "alloc_range first-fit violation: got %lu, want lowest-free %lu in [%lu..%lu]",
                start, want, lo, hi);

  st->present[start] = true;
  st->values[start] = v;
  st->n_present++;
}

/* --------- rule: mtree_alloc_rrange (reverse, top-down) --------- */
static void
rule_alloc_rrange (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long lo = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long hi = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  if (hi < lo) { unsigned long t = lo; lo = hi; hi = t; }
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);

  unsigned long start = ULONG_MAX;
  int rc = mtree_alloc_rrange (&st->mt, &start, pack_v (v),
                               1 /* size */, lo, hi, GFP_KERNEL);
  st->c_rrange++;

  if (rc == -EBUSY) {
    HEGEL_ASSERT (shadow_range_full (st, lo, hi),
                  "alloc_rrange -EBUSY but shadow has free slot in [%lu..%lu]",
                  lo, hi);
    return;
  }

  HEGEL_ASSERT (rc == 0,
                "alloc_rrange unexpected rc=%d (lo=%lu hi=%lu)", rc, lo, hi);
  HEGEL_ASSERT (start >= lo && start <= hi,
                "alloc_rrange start=%lu outside [%lu..%lu]", start, lo, hi);
  HEGEL_ASSERT (start < KEYSPACE,
                "alloc_rrange start=%lu beyond KEYSPACE", start);
  HEGEL_ASSERT (!st->present[start],
                "alloc_rrange chose occupied slot %lu", start);

  /* P (top-down): the returned id must be the HIGHEST free in [lo..hi]. */
  unsigned long want = shadow_last_free (st, lo, hi);
  HEGEL_ASSERT (start == want,
                "alloc_rrange top-down violation: got %lu, want highest-free %lu in [%lu..%lu]",
                start, want, lo, hi);

  st->present[start] = true;
  st->values[start] = v;
  st->n_present++;
}

/* --------- rule: erase a known-occupied id chosen by ordinal --------- */
static void
rule_erase_existing (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  int ord = hegel_draw_int (tc, 0, st->n_present - 1);
  int seen = 0;
  unsigned long k = 0;
  for (int i = 0; i < KEYSPACE; i++) {
    if (st->present[i]) {
      if (seen == ord) { k = (unsigned long) i; break; }
      seen++;
    }
  }
  void *prev = mtree_erase (&st->mt, k);
  HEGEL_ASSERT (prev != NULL,
                "erase returned NULL for known-occupied id %lu (shadow value=%lu)",
                k, st->values[k]);
  HEGEL_ASSERT (unpack_v (prev) == st->values[k],
                "erase mismatch at %lu: got=%lu shadow=%lu",
                k, unpack_v (prev), st->values[k]);
  st->present[k] = false;
  st->values[k] = 0;
  st->n_present--;
  st->c_erase++;
}

/* --------- rule: erase a low-half id (forces wrap on next cyclic) --------- */
static void
rule_erase_low (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  /* Pick the n-th occupied id in the low half. */
  int low_count = 0;
  for (int i = 0; i < KEYSPACE / 2; i++)
    if (st->present[i]) low_count++;
  int ord = hegel_draw_int (tc, 0, low_count - 1);
  int seen = 0;
  unsigned long k = 0;
  for (int i = 0; i < KEYSPACE / 2; i++) {
    if (st->present[i]) {
      if (seen == ord) { k = (unsigned long) i; break; }
      seen++;
    }
  }
  void *prev = mtree_erase (&st->mt, k);
  HEGEL_ASSERT (prev != NULL, "erase_low NULL at %lu", k);
  HEGEL_ASSERT (unpack_v (prev) == st->values[k],
                "erase_low value mismatch at %lu", k);
  st->present[k] = false;
  st->values[k] = 0;
  st->n_present--;
  st->c_erase++;
}

/* --------- rule: erase a high-half id --------- */
static void
rule_erase_high (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  int high_count = 0;
  for (int i = KEYSPACE / 2; i < KEYSPACE; i++)
    if (st->present[i]) high_count++;
  int ord = hegel_draw_int (tc, 0, high_count - 1);
  int seen = 0;
  unsigned long k = 0;
  for (int i = KEYSPACE / 2; i < KEYSPACE; i++) {
    if (st->present[i]) {
      if (seen == ord) { k = (unsigned long) i; break; }
      seen++;
    }
  }
  void *prev = mtree_erase (&st->mt, k);
  HEGEL_ASSERT (prev != NULL, "erase_high NULL at %lu", k);
  HEGEL_ASSERT (unpack_v (prev) == st->values[k],
                "erase_high value mismatch at %lu", k);
  st->present[k] = false;
  st->values[k] = 0;
  st->n_present--;
  st->c_erase++;
}

/* --------- rule: fill cycle (alloc until EBUSY) ----------
** This explicitly walks the cyclic allocator through wraparound: it
** keeps issuing mtree_alloc_cyclic over [0..KEYSPACE-1] until ENOSPC,
** then frees the lowest occupied id, then issues one more alloc that
** MUST succeed (any free slot exists so cyclic must find it). */
static void
rule_fill_cycle (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  st->c_fill++;
  unsigned long lo = 0;
  unsigned long hi = KEYSPACE - 1;
  unsigned long v_base = (unsigned long) hegel_draw_int (tc, 1, 1000);

  /* Burst-fill via cyclic. */
  int safety = 4 * KEYSPACE;
  while (st->n_present < KEYSPACE && safety-- > 0) {
    unsigned long start = ULONG_MAX;
    int rc = mtree_alloc_cyclic (&st->mt, &start, pack_v (v_base),
                                 lo, hi, &st->next, GFP_KERNEL);
    if (rc == -EBUSY) {
      HEGEL_ASSERT (shadow_range_full (st, lo, hi),
                    "fill_cycle EBUSY but shadow not full");
      break;
    }
    HEGEL_ASSERT (rc == 0 || rc == 1, "fill_cycle rc=%d", rc);
    HEGEL_ASSERT (start <= hi && !st->present[start],
                  "fill_cycle bad start=%lu (occupied=%d)",
                  start, st->present[start]);
    st->present[start] = true;
    st->values[start] = v_base;
    st->n_present++;
  }

  /* If completely full, free the lowest occupied and re-alloc.  The
  ** allocator MUST find that newly-free slot. */
  if (st->n_present == KEYSPACE) {
    unsigned long free_me = 0;  /* lowest id 0 was definitely allocated */
    void *prev = mtree_erase (&st->mt, free_me);
    HEGEL_ASSERT (prev != NULL, "fill_cycle: erase(0) returned NULL");
    st->present[free_me] = false;
    st->values[free_me] = 0;
    st->n_present--;

    unsigned long start = ULONG_MAX;
    int rc = mtree_alloc_cyclic (&st->mt, &start, pack_v (v_base),
                                 lo, hi, &st->next, GFP_KERNEL);
    HEGEL_ASSERT (rc == 0 || rc == 1,
                  "fill_cycle: post-erase alloc rc=%d (must succeed: id=0 was free)",
                  rc);
    HEGEL_ASSERT (start == free_me,
                  "fill_cycle: post-erase alloc returned %lu, expected %lu (only free slot)",
                  start, free_me);
    st->present[start] = true;
    st->values[start] = v_base;
    st->n_present++;
  }
}

/* --------- rule: random load (round-trip) --------- */
static void
rule_load (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  void *got = mtree_load (&st->mt, k);
  st->c_load++;
  if (st->present[k]) {
    HEGEL_ASSERT (got != NULL, "load(%lu) NULL but shadow says occupied", k);
    HEGEL_ASSERT (unpack_v (got) == st->values[k],
                  "load(%lu) value mismatch: got=%lu shadow=%lu",
                  k, unpack_v (got), st->values[k]);
  } else {
    HEGEL_ASSERT (got == NULL,
                  "load(%lu) returned %lu but shadow says free",
                  k, got ? unpack_v (got) : 0UL);
  }
}

/* --------- invariant: shadow matches tree, every slot --------- */
static void
inv_shadow_matches_tree (const void *s, hegel_testcase *tc)
{
  (void) tc;
  const st_t *st = s;
  for (int i = 0; i < KEYSPACE; i++) {
    void *got = mtree_load ((struct maple_tree *) &st->mt, (unsigned long) i);
    if (st->present[i]) {
      HEGEL_ASSERT (got != NULL,
                    "invariant: shadow[%d] occupied but tree has nothing", i);
      HEGEL_ASSERT (unpack_v (got) == st->values[i],
                    "invariant: shadow[%d]=%lu but tree[%d]=%lu",
                    i, st->values[i], i, unpack_v (got));
    } else {
      HEGEL_ASSERT (got == NULL,
                    "invariant: shadow[%d] free but tree has %lu",
                    i, unpack_v (got));
    }
  }
}

/* --------- main --------- */

int
main (int argc, char *argv[])
{
  unsigned long n_cases = 300;
  if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

  rcu_register_thread ();
  radix_tree_init ();
  maple_tree_init ();

  printf ("maple_tree rediscovery PBT (n_cases=%lu, keyspace=%d)\n",
          n_cases, KEYSPACE);

  hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  /* Bias rule selection towards alloc_cyclic — it's the headline. */
  if (hegel_state_machine_add_rule (sm, "alloc_cyclic",   rule_alloc_cyclic) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "alloc_cyclic_2", rule_alloc_cyclic) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "alloc_cyclic_3", rule_alloc_cyclic) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "alloc_range",    rule_alloc_range) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "alloc_rrange",   rule_alloc_rrange) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "fill_cycle",     rule_fill_cycle) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "load",           rule_load) != 0) return 1;

  if (hegel_state_machine_add_rule_with_precondition
        (sm, "erase_existing", any_present, rule_erase_existing) != 0) return 1;
  if (hegel_state_machine_add_rule_with_precondition
        (sm, "erase_low", any_present_low, rule_erase_low) != 0) return 1;
  if (hegel_state_machine_add_rule_with_precondition
        (sm, "erase_high", any_present_high, rule_erase_high) != 0) return 1;

  if (hegel_state_machine_add_invariant
        (sm, "shadow_matches_tree", inv_shadow_matches_tree) != 0) return 1;

  hegel_state_machine_run_n (sm, n_cases);
  hegel_state_machine_free (sm);

  fprintf (stderr,
           "[maple_tree rediscovery coverage]\n"
           "  cases=%ld\n"
           "  cyclic={total=%ld, wrap=%ld, busy=%ld}\n"
           "  range=%ld  rrange=%ld  erase=%ld  fill=%ld  load=%ld\n",
           g_cases, g_c_cyc, g_c_cyc_wrap, g_c_cyc_busy,
           g_c_range, g_c_rrange, g_c_erase, g_c_fill, g_c_load);
  printf ("PASSED\n");
  return 0;
}
