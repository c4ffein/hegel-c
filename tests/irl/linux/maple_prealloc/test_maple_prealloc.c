/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful PBT focused on mas_preallocate + mas_store interaction.
**
** Maple tree's preallocation API:
**   int  mas_preallocate(mas, entry, gfp)  — reserve nodes ahead of a store
**   void mas_store_prealloc(mas, entry)    — store using preallocated nodes
**   void mas_store(mas, entry)             — store (respects MA_STATE_PREALLOC
**                                            flag if set, else allocates)
**   void mas_destroy(mas)                  — drops any reserved allocations
**
** Documented contract (header + comments in maple_tree.c):
**   - After mas_preallocate returns 0, a single subsequent store at the same
**     location MUST succeed without further allocation.
**   - mas_destroy() releases pre-allocated nodes (no leak).
**   - The MA_STATE_PREALLOC flag tells mas_store to use the reserved alloc
**     pool instead of GFP_NOWAIT-allocating inside the store path.
**
** Rules:
**   rule_store                   - direct mtree_store(k, v), no prealloc
**   rule_prealloc_then_store     - prealloc(k), mas_store(k, v) on the same mas
**   rule_double_prealloc         - prealloc twice on the same mas, then store
**                                  (probes the "stale prealloc state" path)
**   rule_prealloc_then_destroy   - prealloc(k), mas_destroy(mas) — leak probe
**   rule_prealloc_then_other_key - prealloc(k1), then store at k2 with mas_store
**                                  (mismatched location)
**   rule_erase_existing          - mtree_erase
**   rule_load                    - mtree_load, compare with shadow
**
** Invariants:
**   inv_iter_matches_shadow      - mas_for_each yields the shadow exactly
**                                  (range-merge aware, copied from
**                                   test_maple_basic.c, simplified for
**                                   single-key stores only)
**
** Per the CLAUDE.md preallocate brief, we want to catch:
**   - mas_store after mas_preallocate fails or WARN_ONs.
**   - Double prealloc leaks the first allocation (ASAN catches the leak
**     if the run ends without mas_destroy clearing it).
**   - Tree state diverges between "direct store" and "prealloc + store".
**   - mas_preallocate on a no-op store leaves the tree corrupted.
*/

#include <assert.h>
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

#define KEYSPACE   64

typedef struct {
  struct maple_tree mt;
  uintptr_t shadow[KEYSPACE];
  int       present[KEYSPACE];
  int       group_id[KEYSPACE];
  int       next_group_id;
  int       n_present;

  long c_store;
  long c_prealloc_store;
  long c_double_prealloc;
  long c_prealloc_destroy;
  long c_prealloc_mismatch;
  long c_erase;
  long c_load_hit;
  long c_load_miss;
} st_t;

static long g_c_store, g_c_prealloc_store, g_c_double_prealloc;
static long g_c_prealloc_destroy, g_c_prealloc_mismatch;
static long g_c_erase, g_c_load_hit, g_c_load_miss;
static long g_n_cases;

static inline void *pack_v (unsigned long v) { return xa_mk_value (v); }

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
  g_c_store           += st->c_store;
  g_c_prealloc_store  += st->c_prealloc_store;
  g_c_double_prealloc += st->c_double_prealloc;
  g_c_prealloc_destroy+= st->c_prealloc_destroy;
  g_c_prealloc_mismatch+= st->c_prealloc_mismatch;
  g_c_erase           += st->c_erase;
  g_c_load_hit        += st->c_load_hit;
  g_c_load_miss       += st->c_load_miss;
  g_n_cases++;
  free (st);
}

static bool any_present (const void *s)
{
  const st_t *st = s;
  return st->n_present > 0;
}

static void shadow_set_single (st_t *st, unsigned long key, void *val)
{
  if (key >= KEYSPACE) return;
  unsigned long u = (unsigned long) val;
  if (!st->present[key]) {
    st->present[key] = 1;
    st->n_present++;
  }
  st->shadow[key] = u;
  st->group_id[key] = st->next_group_id++;
}

static void shadow_erase_group (st_t *st, unsigned long key)
{
  if (key >= KEYSPACE || !st->present[key]) return;
  int gid = st->group_id[key];
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

/* ---- Plain store, no prealloc ---- */
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

/* ---- Store a run of distinct values ----
**
** Grows the tree past one leaf (16 slots), so later stores can need more
** than one new node — the regime where preallocation sizes matter. */
static void rule_store_run (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k0 = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  int n = hegel_draw_int (tc, 1, 32);
  for (int i = 0; i < n && k0 + (unsigned long) i < KEYSPACE; i++) {
    unsigned long k = k0 + (unsigned long) i;
    void *val = pack_v (1001 + (unsigned long) ((st->next_group_id + i) % 1000));
    int rc = mtree_store (&st->mt, k, val, GFP_KERNEL);
    HEGEL_ASSERT (rc == 0, "mtree_store rc=%d (k=%lu)", rc, k);
    shadow_set_single (st, k, val);
  }
  st->c_store++;
}

/* ---- Preallocate, then store via mas_store on the same ma_state ----
**
** This is the central documented use case for mas_preallocate.  After
** prealloc returns 0, the store path must NOT allocate (MA_STATE_PREALLOC
** flag tells mas_store to consume the reserved pool). */
static void rule_prealloc_then_store (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  MA_STATE (mas, &st->mt, k, k);
  int rc = mas_preallocate (&mas, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mas_preallocate rc=%d (k=%lu)", rc, k);
  void *prev = mas_store (&mas, val);
  (void) prev;
  /* mas_destroy is called by mas_store on the non-prealloc path; on
  ** the prealloc path the flag is cleared and any leftovers freed.
  ** Either way, no extra call is needed here per the API contract. */
  shadow_set_single (st, k, val);
  st->c_prealloc_store++;
}

/* ---- Double preallocate, then store ----
**
** Calls mas_preallocate twice on the same ma_state before any store.
** This is the bug-bait path: the second prealloc must either invalidate
** the first allocation (free it / refresh count) or be a documented no-op.
** Storing afterwards must still succeed. */
static void rule_double_prealloc (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k1 = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long k2 = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v  = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  MA_STATE (mas, &st->mt, k1, k1);
  int rc1 = mas_preallocate (&mas, val, GFP_KERNEL);
  HEGEL_ASSERT (rc1 == 0, "first mas_preallocate rc=%d (k=%lu)", rc1, k1);
  /* Update the state to point at k2 for the second prealloc — this is
  ** what callers in mm/ do when their write target shifts (VMA splits). */
  mas_set (&mas, k2);
  int rc2 = mas_preallocate (&mas, val, GFP_KERNEL);
  HEGEL_ASSERT (rc2 == 0, "second mas_preallocate rc=%d (k=%lu)", rc2, k2);
  /* Store at k2 — the active location. */
  void *prev = mas_store (&mas, val);
  (void) prev;
  shadow_set_single (st, k2, val);
  st->c_double_prealloc++;
}

/* ---- Re-preallocate for a wider range, then store it ----
**
** The mmap_region() retry pattern: preallocate for one write, then the
** target grows (vma merge retry) and the same ma_state is preallocated
** again before mas_store_prealloc().  The second, larger request must
** really allocate — fba46a5d83ca fixed it being ignored. */
static void shadow_set_range (st_t *st, unsigned long lo, unsigned long hi, void *val)
{
  int gid = st->next_group_id++;
  for (unsigned long k = lo; k <= hi && k < KEYSPACE; k++) {
    if (!st->present[k]) {
      st->present[k] = 1;
      st->n_present++;
    }
    st->shadow[k] = (unsigned long) val;
    st->group_id[k] = gid;
  }
}

static void rule_reprealloc_range (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k1 = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long lo = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long hi = (unsigned long) hegel_draw_int (tc, (int) lo, KEYSPACE - 1);
  unsigned long v  = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  MA_STATE (mas, &st->mt, k1, k1);
  int rc1 = mas_preallocate (&mas, val, GFP_KERNEL);
  HEGEL_ASSERT (rc1 == 0, "first mas_preallocate rc=%d (k=%lu)", rc1, k1);
  mas_set_range (&mas, lo, hi);
  int rc2 = mas_preallocate (&mas, val, GFP_KERNEL);
  HEGEL_ASSERT (rc2 == 0, "second mas_preallocate rc=%d ([%lu, %lu])", rc2, lo, hi);
  mas_store_prealloc (&mas, val);
  shadow_set_range (st, lo, hi, val);
  st->c_double_prealloc++;
}

/* ---- Preallocate, then destroy without storing ----
**
** Catches leaks: the prealloc'd pool must be freed by mas_destroy. */
static void rule_prealloc_then_destroy (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  MA_STATE (mas, &st->mt, k, k);
  int rc = mas_preallocate (&mas, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mas_preallocate rc=%d (k=%lu)", rc, k);
  mas_destroy (&mas);
  /* Shadow unchanged; tree state must be identical to before the
  ** prealloc.  Verified by inv_iter_matches_shadow. */
  st->c_prealloc_destroy++;
}

/* ---- Preallocate at one key, store at a different key ----
**
** Callers may preallocate for a planned location and then have the
** location change.  mas_set + mas_store should still work.  If the
** prealloc'd nodes are now wrong-shaped, the store must either
** transparently allocate more, or use what's there.  Either way, no
** crash / leak / WARN. */
static void rule_prealloc_then_other_key (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k_pre   = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long k_store = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v       = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  MA_STATE (mas, &st->mt, k_pre, k_pre);
  int rc = mas_preallocate (&mas, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mas_preallocate rc=%d (k=%lu)", rc, k_pre);
  mas_set (&mas, k_store);
  void *prev = mas_store (&mas, val);
  (void) prev;
  shadow_set_single (st, k_store, val);
  st->c_prealloc_mismatch++;
}

/* ---- Erase ---- */
static void rule_erase_existing (void *s, hegel_testcase *tc)
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
                "mtree_erase NULL for known key %lu (shadow=%lu)",
                k, st->shadow[k]);
  HEGEL_ASSERT ((unsigned long) prev == st->shadow[k],
                "mtree_erase prev mismatch at k=%lu: got=%lu shadow=%lu",
                k, (unsigned long) prev, st->shadow[k]);
  shadow_erase_group (st, k);
  st->c_erase++;
}

/* ---- Load ---- */
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
                  "mtree_load(%lu)=%lu but shadow says absent",
                  k, (unsigned long) got);
    st->c_load_miss++;
  }
}

/* ---- Invariant: full iteration matches shadow ----
** Same logic as test_maple_basic.c — range-merge aware. */
static void inv_iter_matches_shadow (const void *s, hegel_testcase *tc)
{
  (void) tc;
  st_t *st = (st_t *) s;
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

  unsigned long flat_k[KEYSPACE * 4];
  unsigned long flat_v[KEYSPACE * 4];
  int flat_n = 0;
  for (int i = 0; i < got_n; i++) {
    for (unsigned long k = got_k[i]; k <= got_last[i]; k++) {
      if (flat_n >= (int) (sizeof (flat_k) / sizeof (flat_k[0]))) break;
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
}

int main (int argc, char *argv[])
{
  unsigned long n_cases = 200;
  if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

  rcu_register_thread ();
  radix_tree_init ();
  maple_tree_init ();

  printf ("maple_tree preallocate PBT (n_cases=%lu, keyspace=%d)\n",
          n_cases, KEYSPACE);

  hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  /* Plain operations. */
  if (hegel_state_machine_add_rule (sm, "store",                rule_store) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "load",                 rule_load) != 0) return 1;

  /* Prealloc-focused rules (multiple registrations for weight). */
  if (hegel_state_machine_add_rule (sm, "prealloc_store",       rule_prealloc_then_store) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "prealloc_store_2",     rule_prealloc_then_store) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "double_prealloc",      rule_double_prealloc) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "double_prealloc_2",    rule_double_prealloc) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "prealloc_destroy",     rule_prealloc_then_destroy) != 0) return 1;
  /* prealloc_other_key is NOT registered: preallocating for one range and
  ** then storing another (without mas_destroy) breaks the maple API
  ** contract, so it crashes on correct kernels too — found by running this
  ** test against the fixed pin (fba46a5d83ca) and v7.1. */
  (void) rule_prealloc_then_other_key;
  if (hegel_state_machine_add_rule (sm, "reprealloc_range", rule_reprealloc_range) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_run", rule_store_run) != 0) return 1;

  if (hegel_state_machine_add_rule_with_precondition
        (sm, "erase_existing", any_present, rule_erase_existing) != 0)
    return 1;

  if (hegel_state_machine_add_invariant
        (sm, "iter_matches_shadow", inv_iter_matches_shadow) != 0)
    return 1;

  hegel_state_machine_run_n (sm, n_cases);
  hegel_state_machine_free (sm);

  fprintf (stderr,
           "[maple_prealloc PBT coverage]\n"
           "  cases=%ld\n"
           "  store=%ld  prealloc_store=%ld  double_prealloc=%ld\n"
           "  prealloc_destroy=%ld  prealloc_mismatch=%ld\n"
           "  erase=%ld  load_hit=%ld  load_miss=%ld\n",
           g_n_cases,
           g_c_store, g_c_prealloc_store, g_c_double_prealloc,
           g_c_prealloc_destroy, g_c_prealloc_mismatch,
           g_c_erase, g_c_load_hit, g_c_load_miss);
  printf ("PASSED\n");
  return 0;
}
