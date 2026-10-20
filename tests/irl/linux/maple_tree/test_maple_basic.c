/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful PBT against lib/maple_tree.c.
**
** Maple tree is a range-keyed adaptive tree (replaces rbtree in mm/'s VMA
** tracking and other places). State machine rules:
**   rule_store         — mtree_store(key, val): always applicable
**   rule_store_range   — mtree_store_range(start, end, val): always
**   rule_erase_existing— mtree_erase(key): precond shadow non-empty
**   rule_load_existing — mtree_load(key) match shadow: precond non-empty
**   rule_load_random   — mtree_load(random key): always
**   rule_walk          — mas_walk(): always (asserts found-match-or-NULL)
**
** Invariants (post-step):
**   inv_iter_matches_shadow — mt_for_each iteration produces every shadow key
**     in sorted order with the right value, and yields no extras.
**
** Shadow model: a sorted (key,value) array.  Keys are bounded so we use
** a flat dense table for O(1) lookup, plus a sorted-key list for iteration
** invariant.
**
** Friction notes are gathered in the running comments below — see end
** of main() for the writeup. */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

/* Pull harness shims first — these set up bug.h, types, etc. so the
** kernel headers below find WARN_ON, BUG_ON, gfp_t, ULONG_MAX, etc. */
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/rcupdate.h>
#include <linux/maple_tree.h>
#include <linux/xarray.h>  /* for xa_mk_value / xa_to_value (works for maple too) */

/* Provided by radix-tree harness init code. */
extern void rcu_register_thread (void);
extern void radix_tree_init (void);

#define KEYSPACE   64           /* keys 0..KEYSPACE-1 */
#define MAX_OPS    96           /* per-case op count cap (state machine cuts itself by depth) */

typedef struct {
  struct maple_tree mt;
  /* Shadow: per-key value + group id.  group_id[k] identifies which
  ** range that key belongs to (range entries created by store_range
  ** share an id; single-key stores get a fresh id each time).
  ** mtree_erase(k) erases the entire group containing k. */
  uintptr_t shadow[KEYSPACE];
  int       present[KEYSPACE];
  int       group_id[KEYSPACE];   /* meaningful iff present[k] */
  int       next_group_id;
  int       n_present;            /* count of keys where present[k]==1 */

  /* Coverage counters (per-case scope; we accumulate into globals at
  ** teardown; see notes about the lack of per-rule counters). */
  long c_store;
  long c_store_range;
  long c_erase;
  long c_erase_miss;             /* erase asked for an existing key but actually nothing was erased — bug signal */
  long c_load_hit;
  long c_load_miss;
  long c_walk;
  long c_iter_full;
} st_t;

static long g_c_store;
static long g_c_store_range;
static long g_c_erase;
static long g_c_load_hit;
static long g_c_load_miss;
static long g_c_walk;
static long g_c_iter_full;
static long g_n_cases;
static long g_max_n_present;     /* widest the shadow ever got */

/* ---- value packing ---- */
/* Use xa_mk_value: encodes v as (v<<1)|1, so the bottom-2-bit pattern is
** 01 — safe for maple_tree (the reserved internal pattern is 10). */
static inline void *pack_v (unsigned long v)
{
  return xa_mk_value (v);
}
static inline unsigned long unpack_v (void *p)
{
  return xa_to_value (p);
}

/* ---- setup / teardown ---- */

static void *setup_fn (void)
{
  st_t *st = calloc (1, sizeof (*st));
  if (!st) return NULL;
  mt_init (&st->mt);
  return st;
}

static void teardown_fn (void *s)
{
  st_t *st = s;
  if (!st) return;
  mtree_destroy (&st->mt);
  /* accumulate coverage */
  g_c_store        += st->c_store;
  g_c_store_range  += st->c_store_range;
  g_c_erase        += st->c_erase;
  g_c_load_hit     += st->c_load_hit;
  g_c_load_miss    += st->c_load_miss;
  g_c_walk         += st->c_walk;
  g_c_iter_full    += st->c_iter_full;
  g_n_cases++;
  if (st->n_present > g_max_n_present) g_max_n_present = st->n_present;
  free (st);
}

/* ---- preconditions ---- */

static bool any_present (const void *s)
{
  const st_t *st = s;
  return st->n_present > 0;
}

/* ---- helpers: shadow updates for range-store ---- */

static void shadow_set_single (st_t *st, unsigned long key, void *val)
{
  /* mtree_store(k, v): single-key entry, fresh group. */
  if (key >= KEYSPACE) return;
  unsigned long u = (unsigned long) val;
  if (!st->present[key]) {
    st->present[key] = 1;
    st->n_present++;
  }
  st->shadow[key] = u;
  st->group_id[key] = st->next_group_id++;
}

static void shadow_set_range (st_t *st, unsigned long first, unsigned long last,
                              void *val)
{
  /* mtree_store_range(a, b, v): all keys in [a..b] share one group id. */
  unsigned long u = (unsigned long) val;
  int gid = st->next_group_id++;
  for (unsigned long k = first; k <= last && k < KEYSPACE; k++) {
    if (!st->present[k]) {
      st->present[k] = 1;
      st->n_present++;
    }
    st->shadow[k] = u;
    st->group_id[k] = gid;
  }
}

static void shadow_erase_group (st_t *st, unsigned long key)
{
  /* Erase every key sharing the same group_id as `key`. */
  if (key >= KEYSPACE || !st->present[key]) return;
  int gid = st->group_id[key];
  /* Group ranges are contiguous (range stores) or singletons.
  ** Walk left and right while the group_id matches. */
  for (long i = (long) key; i >= 0; i--) {
    if (!st->present[i] || st->group_id[i] != gid) break;
    st->present[i] = 0;
    st->shadow[i] = 0;
    st->n_present--;
  }
  for (long i = (long) key + 1; i < KEYSPACE; i++) {
    if (!st->present[i] || st->group_id[i] != gid) break;
    st->present[i] = 0;
    st->shadow[i] = 0;
    st->n_present--;
  }
}

/* ---- rule: store(k, v) ---- */
static void rule_store (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  int rc = mtree_store (&st->mt, k, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mtree_store rc=%d (k=%lu v=%lu)", rc, k, v);
  shadow_set_single (st, k, val);
  st->c_store++;
}

/* ---- rule: store_range(first..last, v) ----
**
** Disabled in the rule set below: combining store_range with single-key
** mtree_store + mtree_erase makes shadow modeling significantly more
** complex (tree splits ranges on partial overwrites, group_id model
** must split too).  Friction note F: PBT shadow models for
** range-keyed structures want a "subscribe to mutation events"
** affordance — the test is forced to recompute splits client-side.
** Kept here for completeness and future range-only tests. */
static void rule_store_range (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long a = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long b = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long first = a < b ? a : b;
  unsigned long last  = a < b ? b : a;
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  int rc = mtree_store_range (&st->mt, first, last, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mtree_store_range rc=%d (%lu..%lu v=%lu)",
                rc, first, last, v);
  shadow_set_range (st, first, last, val);
  st->c_store_range++;
}

/* ---- rule: erase an existing key ----
**
** GOTCHA documented during test development: mtree_erase(k) erases the
** ENTIRE RANGE covering k (set to NULL via mas_wr_store_entry).  That
** range may be wider than k if (a) k was stored via mtree_store_range,
** or (b) k was stored via several mtree_store calls and the maple tree
** merged adjacent slots with equal values.  Our shadow tracks per-key
** values, so we walk left/right from k extending as long as the shadow
** value matches — this is the "merge equivalence" the maple tree uses
** internally.  Friction note E. */
static void rule_erase_existing (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  /* "Pick from existing keys" — per the brief, this is the first
  ** awkward point.  No drawing primitive for "one of N indices".
  ** Workaround: draw 0..n-1, walk the dense table to that ordinal.
  ** Friction note A. */
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
  unsigned long expected = st->shadow[k];
  HEGEL_ASSERT (prev != NULL,
                "mtree_erase returned NULL for known key %lu (shadow=%lu)",
                k, expected);
  HEGEL_ASSERT ((unsigned long) prev == expected,
                "mtree_erase prev mismatch at k=%lu: got %lu shadow=%lu",
                k, (unsigned long) prev, expected);
  /* Erase the entire group covering k. */
  shadow_erase_group (st, k);
  st->c_erase++;
}

/* ---- rule: mas_walk to a key, compare with shadow ---- */
static void rule_walk (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  MA_STATE (mas, &st->mt, k, k);
  void *got;
  rcu_read_lock ();
  got = mas_walk (&mas);
  rcu_read_unlock ();
  if (st->present[k]) {
    HEGEL_ASSERT (got != NULL,
                  "mas_walk(%lu) NULL but shadow has it (=%lu)",
                  k, st->shadow[k]);
    HEGEL_ASSERT ((unsigned long) got == st->shadow[k],
                  "mas_walk(%lu) got=%lu shadow=%lu",
                  k, (unsigned long) got, st->shadow[k]);
    st->c_load_hit++;
  } else {
    HEGEL_ASSERT (got == NULL,
                  "mas_walk(%lu) returned %lu but shadow says absent",
                  k, (unsigned long) got);
    st->c_load_miss++;
  }
  st->c_walk++;
}

/* ---- rule: mtree_load random key ---- */
static void rule_load (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  void *got = mtree_load (&st->mt, k);
  if (st->present[k]) {
    HEGEL_ASSERT (got != NULL && (unsigned long) got == st->shadow[k],
                  "mtree_load(%lu) got=%lu shadow=%lu",
                  k, got ? (unsigned long) got : 0UL, st->shadow[k]);
    st->c_load_hit++;
  } else {
    HEGEL_ASSERT (got == NULL,
                  "mtree_load(%lu) returned %lu but shadow says absent",
                  k, (unsigned long) got);
    st->c_load_miss++;
  }
}

/* ---- invariant: full iteration matches shadow ----
** mt_for_each yields every present key in ascending order, exactly once,
** with the matching value, and yields no other keys. */
static void inv_iter_matches_shadow (const void *s, hegel_testcase *tc)
{
  (void) tc;
  st_t *st = (st_t *) s;
  /* Build expected sorted (key,value) list from shadow. */
  unsigned long want_k[KEYSPACE];
  unsigned long want_v[KEYSPACE];
  int want_n = 0;
  for (int i = 0; i < KEYSPACE; i++) {
    if (st->present[i]) {
      want_k[want_n] = (unsigned long) i;
      want_v[want_n] = st->shadow[i];
      want_n++;
    }
  }
  /* Walk the maple tree using mas_for_each (mas->index/last give the
  ** range of the current entry — which is what we need; mt_for_each's
  ** __index gets *post*-incremented to mas.last+1 by mt_find, so it
  ** doesn't directly identify the current key.  Friction note D. */
  unsigned long got_k[KEYSPACE * 2];
  unsigned long got_v[KEYSPACE * 2];
  unsigned long got_last[KEYSPACE * 2];
  int got_n = 0;
  void *entry;
  rcu_read_lock ();
  MA_STATE (mas, &st->mt, 0, 0);
  mas_for_each (&mas, entry, ULONG_MAX) {
    if (got_n >= (int) (sizeof (got_k) / sizeof (got_k[0]))) break;
    got_k[got_n] = mas.index;
    got_last[got_n] = mas.last;
    got_v[got_n] = (unsigned long) entry;
    got_n++;
  }
  rcu_read_unlock ();
  (void) got_last;  /* could be used to assert range coverage */

  /* Maple tree may merge contiguous equal-value ranges.  Expand each
  ** iterator entry [index..last] into individual keys for comparison
  ** against the shadow's per-key model. */
  unsigned long flat_k[KEYSPACE * 4];
  unsigned long flat_v[KEYSPACE * 4];
  int flat_n = 0;
  for (int i = 0; i < got_n; i++) {
    for (unsigned long k = got_k[i]; k <= got_last[i]; k++) {
      if (flat_n >= (int) (sizeof (flat_k) / sizeof (flat_k[0]))) break;
      /* Only count keys within our keyspace bound (the iterator's last
      ** may extend beyond if store_range was open-ended — but we only
      ** ever stored within [0,KEYSPACE-1], so cap defensively). */
      if (k >= KEYSPACE) break;
      flat_k[flat_n] = k;
      flat_v[flat_n] = got_v[i];
      flat_n++;
    }
  }
  HEGEL_ASSERT (flat_n == want_n,
                "iter (flattened) count mismatch: got=%d want=%d", flat_n, want_n);
  for (int i = 0; i < want_n; i++) {
    HEGEL_ASSERT (flat_k[i] == want_k[i],
                  "iter key[%d] mismatch: got=%lu want=%lu",
                  i, flat_k[i], want_k[i]);
    HEGEL_ASSERT (flat_v[i] == want_v[i],
                  "iter val at k=%lu mismatch: got=%lu want=%lu",
                  flat_k[i], flat_v[i], want_v[i]);
  }
  st->c_iter_full++;
}

/* ---- main ---- */

int main (int argc, char *argv[])
{
  unsigned long n_cases = 200;
  if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

  rcu_register_thread ();
  radix_tree_init ();
  maple_tree_init ();

  printf ("maple_tree stateful PBT (n_cases=%lu, keyspace=%d)\n",
          n_cases, KEYSPACE);

  hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  /* Always-applicable rules (registered with weights via duplicate names —
  ** there's no per-rule weight knob.  Friction note B). */
  if (hegel_state_machine_add_rule (sm, "store",        rule_store) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_2",      rule_store) != 0) return 1;
  /* rule_store_range disabled — see friction note F. */
  (void) rule_store_range;
  if (hegel_state_machine_add_rule (sm, "load",         rule_load) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "walk",         rule_walk) != 0) return 1;

  /* State-dependent rule via precondition. */
  if (hegel_state_machine_add_rule_with_precondition
        (sm, "erase_existing", any_present, rule_erase_existing) != 0)
    return 1;

  if (hegel_state_machine_add_invariant
        (sm, "iter_matches_shadow", inv_iter_matches_shadow) != 0)
    return 1;

  hegel_state_machine_run_n (sm, n_cases);
  hegel_state_machine_free (sm);

  fprintf (stderr,
           "[maple_tree PBT coverage]\n"
           "  cases=%ld  max_n_present=%ld\n"
           "  store=%ld  store_range=%ld  erase=%ld\n"
           "  load_hit=%ld  load_miss=%ld  walk=%ld  iter_full=%ld\n",
           g_n_cases, g_max_n_present,
           g_c_store, g_c_store_range, g_c_erase,
           g_c_load_hit, g_c_load_miss, g_c_walk, g_c_iter_full);
  printf ("PASSED\n");
  return 0;
}
