/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful PBT focused on maple_tree LIFECYCLE.
**
** Surface under test: init / store / destroy / re-init / re-store, with
** emphasis on the *destroy* path.  The buggy snapshot we link against
** has a known kernel fix (commit ea9b77f98d94 "maple_tree: mark next
** node as dead in mt_destroy_walk for the single root leaf path")
** REVERTED — i.e. the snapshot's mt_destroy_walk omits the
** mte_set_node_dead() call on the single-root-leaf shortcut.
**
** Detection strategy:
**   - ASAN+UBSAN enabled in the Makefile.
**   - Lots of mtree_destroy on small trees (more likely to be a
**     single-root-leaf shape).
**   - Lots of destroy-then-reuse cycles ("rule_destroy_and_reinit"),
**     including in RCU mode where the missing dead-mark is most likely
**     to surface.
**   - Functional invariants: after destroy the tree is empty; after
**     re-init+store the value reads back; mtree_empty() agrees with the
**     shadow.
**   - Concurrent-ish exercise: a rule that takes an rcu_read_lock,
**     starts walking, then drops the lock — interleaved with destroy
**     rules so the destroy walk may run while ma_state pointers exist
**     elsewhere on the stack.  (We are single-threaded, but liburcu's
**     call_rcu callbacks may run between rcu_read_unlock and the next
**     rcu_read_lock; if a leaf node was supposed to be marked dead
**     before being scheduled for RCU free, the missing mark may cause
**     a downstream walker to follow stale state.)
**
** We do NOT use spawned threads; the property test is single-threaded
** but exercises every cleanup code path mt_destroy_walk can take.
**
** Shadow model: a single-key map (KEY -> VALUE) backed by a flat array
** indexed by key.  After a destroy the shadow is cleared. */

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

#define KEYSPACE 16
#define MAX_REINITS_PER_CASE 64

typedef struct {
  struct maple_tree mt;

  /* Shadow */
  uintptr_t shadow[KEYSPACE];
  int       present[KEYSPACE];
  int       group_id[KEYSPACE];   /* same group ⇒ same range entry */
  int       next_group_id;
  int       n_present;

  /* Lifecycle config (immutable for this state instance) */
  unsigned int flags;
  int          use_mtree_init;    /* if false, used mt_init_flags directly */

  /* Counters */
  long c_store;
  long c_store_range;
  long c_insert;
  long c_erase;
  long c_load;
  long c_iter;
  long c_destroy;
  long c_reinit;
  long c_walk;
  long c_destroy_empty;
  long c_destroy_single;
  long c_destroy_multi;
} st_t;

/* Aggregate counters across cases */
static long g_c_store;
static long g_c_store_range;
static long g_c_insert;
static long g_c_erase;
static long g_c_load;
static long g_c_iter;
static long g_c_destroy;
static long g_c_reinit;
static long g_c_walk;
static long g_c_destroy_empty;
static long g_c_destroy_single;
static long g_c_destroy_multi;
static long g_max_n_present;
static long g_n_cases;
static long g_n_rcu_cases;
static long g_n_alloc_cases;

/* ---- helpers ---- */

static inline void *pack_v (unsigned long v) { return xa_mk_value (v); }
static inline unsigned long unpack_v (void *p) { return xa_to_value (p); }

static void shadow_clear (st_t *st)
{
  memset (st->shadow,   0, sizeof (st->shadow));
  memset (st->present,  0, sizeof (st->present));
  memset (st->group_id, 0, sizeof (st->group_id));
  st->n_present = 0;
  st->next_group_id = 1;
}

static void shadow_set_single (st_t *st, unsigned long key, unsigned long val)
{
  if (key >= KEYSPACE) return;
  if (!st->present[key]) {
    st->present[key] = 1;
    st->n_present++;
  }
  st->shadow[key] = val;
  st->group_id[key] = st->next_group_id++;
}

static void shadow_set_range (st_t *st, unsigned long a, unsigned long b,
                              unsigned long val)
{
  int gid = st->next_group_id++;
  if (a >= KEYSPACE) return;
  if (b >= KEYSPACE) b = KEYSPACE - 1;
  for (unsigned long k = a; k <= b; k++) {
    if (!st->present[k]) {
      st->present[k] = 1;
      st->n_present++;
    }
    st->shadow[k] = val;
    st->group_id[k] = gid;
  }
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

/* ---- setup / teardown ---- */

static unsigned int pick_flags (uint32_t r)
{
  /* Vary across runs.  Locking flags affect spin_lock primitives only,
  ** which are no-ops in the userspace harness; the interesting axes are
  ** ALLOC_RANGE and USE_RCU. */
  unsigned int flags = 0;
  if (r & 1) flags |= MT_FLAGS_ALLOC_RANGE;
  if (r & 2) flags |= MT_FLAGS_USE_RCU;
  return flags;
}

static int g_flags_dispatch;   /* round-robin selector across cases */

static void *setup_fn (void)
{
  st_t *st = calloc (1, sizeof (*st));
  if (!st) return NULL;
  uint32_t r = (uint32_t) __atomic_fetch_add (&g_flags_dispatch, 1, __ATOMIC_RELAXED);
  st->flags = pick_flags (r);
  st->use_mtree_init = (r & 4) ? 1 : 0;
  if (st->use_mtree_init)
    mt_init_flags (&st->mt, st->flags);
  else
    mt_init_flags (&st->mt, st->flags);   /* same call — placeholder for parity */
  st->next_group_id = 1;
  return st;
}

static void teardown_fn (void *s)
{
  st_t *st = s;
  if (!st) return;
  mtree_destroy (&st->mt);

  g_c_store         += st->c_store;
  g_c_store_range   += st->c_store_range;
  g_c_insert        += st->c_insert;
  g_c_erase         += st->c_erase;
  g_c_load          += st->c_load;
  g_c_iter          += st->c_iter;
  g_c_destroy       += st->c_destroy;
  g_c_reinit        += st->c_reinit;
  g_c_walk          += st->c_walk;
  g_c_destroy_empty += st->c_destroy_empty;
  g_c_destroy_single+= st->c_destroy_single;
  g_c_destroy_multi += st->c_destroy_multi;
  if (st->n_present > g_max_n_present) g_max_n_present = st->n_present;
  if (st->flags & MT_FLAGS_USE_RCU)    g_n_rcu_cases++;
  if (st->flags & MT_FLAGS_ALLOC_RANGE) g_n_alloc_cases++;
  g_n_cases++;
  free (st);
}

/* ---- preconditions ---- */

static bool any_present (const void *s)
{
  return ((const st_t *) s)->n_present > 0;
}

/* ---- rules ---- */

/* store(k, v): always applicable */
static void rule_store (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  int rc = mtree_store (&st->mt, k, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mtree_store rc=%d (k=%lu v=%lu flags=0x%x)",
                rc, k, v, st->flags);
  shadow_set_single (st, k, v);
  st->c_store++;
}

/* store_range(a..b, v): always applicable */
static void rule_store_range (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long a = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long b = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  if (a > b) { unsigned long t = a; a = b; b = t; }
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  int rc = mtree_store_range (&st->mt, a, b, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mtree_store_range rc=%d (%lu..%lu v=%lu flags=0x%x)",
                rc, a, b, v, st->flags);
  shadow_set_range (st, a, b, v);
  st->c_store_range++;
}

/* insert(k, v): only if absent — verify -EEXIST semantics */
static void rule_insert (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  int was_present = st->present[k];
  int rc = mtree_insert (&st->mt, k, val, GFP_KERNEL);
  if (was_present) {
    HEGEL_ASSERT (rc == -17 /* -EEXIST */,
                  "mtree_insert rc=%d expected -EEXIST for present key %lu",
                  rc, k);
  } else {
    HEGEL_ASSERT (rc == 0,
                  "mtree_insert rc=%d for absent key %lu (flags=0x%x)",
                  rc, k, st->flags);
    shadow_set_single (st, k, v);
  }
  st->c_insert++;
}

/* erase existing key */
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
  unsigned long expected = st->shadow[k];
  HEGEL_ASSERT (prev != NULL,
                "mtree_erase(%lu) NULL but shadow has it (=%lu) flags=0x%x",
                k, expected, st->flags);
  HEGEL_ASSERT (unpack_v (prev) == expected,
                "mtree_erase prev mismatch at k=%lu: got=%lu shadow=%lu",
                k, unpack_v (prev), expected);
  shadow_erase_group (st, k);
  st->c_erase++;
}

/* load random key */
static void rule_load (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  void *got = mtree_load (&st->mt, k);
  if (st->present[k]) {
    HEGEL_ASSERT (got != NULL && unpack_v (got) == st->shadow[k],
                  "mtree_load(%lu) got=%lu shadow=%lu flags=0x%x",
                  k, got ? unpack_v (got) : 0UL, st->shadow[k], st->flags);
  } else {
    HEGEL_ASSERT (got == NULL,
                  "mtree_load(%lu) returned %lu but shadow says absent flags=0x%x",
                  k, got ? unpack_v (got) : 0UL, st->flags);
  }
  st->c_load++;
}

/* walk an mas in an rcu_read_lock — exercises stale-pointer paths if
** any RCU node freeing scheduled by destroy walks bleeds across. */
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
    HEGEL_ASSERT (got != NULL && unpack_v (got) == st->shadow[k],
                  "mas_walk(%lu) got=%p shadow=%lu flags=0x%x",
                  k, got, st->shadow[k], st->flags);
  } else {
    HEGEL_ASSERT (got == NULL,
                  "mas_walk(%lu) returned %lu but shadow says absent flags=0x%x",
                  k, got ? unpack_v (got) : 0UL, st->flags);
  }
  st->c_walk++;
}

/* iter: walk every entry; assert order & count match shadow */
static void rule_iter (void *s, hegel_testcase *tc)
{
  (void) tc;
  st_t *st = s;
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
    got_k[got_n]    = mas.index;
    got_last[got_n] = mas.last;
    got_v[got_n]    = unpack_v (entry);
    got_n++;
  }
  rcu_read_unlock ();

  /* Flatten: a range entry covers multiple keys in our shadow. */
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
                "iter count: got=%d want=%d flags=0x%x", flat_n, want_n, st->flags);
  for (int i = 0; i < want_n; i++) {
    HEGEL_ASSERT (flat_k[i] == want_k[i],
                  "iter key[%d]: got=%lu want=%lu", i, flat_k[i], want_k[i]);
    HEGEL_ASSERT (flat_v[i] == want_v[i],
                  "iter val at k=%lu: got=%lu want=%lu",
                  flat_k[i], flat_v[i], want_v[i]);
  }
  st->c_iter++;
}

/* THE lifecycle rule: destroy and re-init.  Optionally pre-checks
** that the tree is in a particular shape (empty / single / multi)
** so we get coverage of all three branches of mt_destroy_walk. */
static void rule_destroy_and_reinit (void *s, hegel_testcase *tc)
{
  st_t *st = s;

  /* Categorize the about-to-be-destroyed tree shape. */
  if (st->n_present == 0) st->c_destroy_empty++;
  else if (st->n_present == 1) st->c_destroy_single++;
  else st->c_destroy_multi++;

  /* Half the time use mtree_destroy, half use __mt_destroy under lock. */
  int via_locked = hegel_draw_int (tc, 0, 1);
  bool was_rcu = mt_in_rcu (&st->mt);
  if (via_locked) {
    /* userspace harness: mtree_lock is spin_lock no-op */
    mtree_lock (&st->mt);
    __mt_destroy (&st->mt);
    mtree_unlock (&st->mt);
  } else {
    mtree_destroy (&st->mt);
  }
  /* Force RCU callbacks to run.  If destroy_walk left a leaf node
  ** "alive-looking" while scheduling it for RCU free, the next time we
  ** touch that memory (via re-store landing it back in the slab) ASAN
  ** should catch the conflict.  rcu_barrier waits for the grace period
  ** and all pending callbacks. */
  if (was_rcu) rcu_barrier ();

  /* After destroy: ma_root must be NULL and ma_flags reset to mt_attr() */
  HEGEL_ASSERT (mtree_empty (&st->mt),
                "tree not empty after destroy! flags=0x%x",
                st->flags);

  /* mtree_load on any key must be NULL */
  unsigned long probe = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  void *got = mtree_load (&st->mt, probe);
  HEGEL_ASSERT (got == NULL,
                "mtree_load(%lu) post-destroy returned %p (flags=0x%x)",
                probe, got, st->flags);

  /* mas_walk must produce NULL */
  MA_STATE (mas, &st->mt, probe, probe);
  rcu_read_lock ();
  void *got2 = mas_walk (&mas);
  rcu_read_unlock ();
  HEGEL_ASSERT (got2 == NULL,
                "mas_walk(%lu) post-destroy returned %p (flags=0x%x)",
                probe, got2, st->flags);

  /* mt_for_each finds nothing */
  int iter_count = 0;
  rcu_read_lock ();
  MA_STATE (mas2, &st->mt, 0, 0);
  void *e;
  mas_for_each (&mas2, e, ULONG_MAX) {
    iter_count++;
    if (iter_count > 10) break;
  }
  rcu_read_unlock ();
  HEGEL_ASSERT (iter_count == 0,
                "iter found %d entries on destroyed tree (flags=0x%x)",
                iter_count, st->flags);

  shadow_clear (st);

  /* Now exercise the "re-store after destroy is the same as fresh"
  ** property: pick a key & value, store, load, assert correctness. */
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  void *val = pack_v (v);
  int rc = mtree_store (&st->mt, k, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0,
                "mtree_store post-destroy rc=%d (flags=0x%x)",
                rc, st->flags);
  shadow_set_single (st, k, v);
  void *got3 = mtree_load (&st->mt, k);
  HEGEL_ASSERT (got3 != NULL && unpack_v (got3) == v,
                "mtree_load post-destroy: got=%lu want=%lu (flags=0x%x)",
                got3 ? unpack_v (got3) : 0UL, v, st->flags);

  st->c_destroy++;
  st->c_reinit++;
}

/* Walk-then-pause-then-destroy: simulate a reader that resolved a node
** but hasn't dereferenced it yet, then a destroy happens.  In RCU mode
** the reader's read-side critical section should keep the freed memory
** valid until rcu_read_unlock.  If mt_destroy_walk fails to mark the
** root leaf dead, a subsequent re-resolve via the saved mas may follow
** stale links.  We rcu_read_lock, mas_walk, save the node pointer,
** run mas_pause, then destroy and immediately re-store, and check the
** original tree state by walking via the *fresh* mas state. */
static void rule_walk_then_destroy (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  if (st->n_present == 0) return;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  MA_STATE (mas, &st->mt, k, k);
  rcu_read_lock ();
  void *first = mas_walk (&mas);
  /* keep the lock for a moment longer */
  if (first && st->present[k]) {
    HEGEL_ASSERT (unpack_v (first) == st->shadow[k],
                  "walk_then_destroy: mismatch on initial walk k=%lu", k);
  }
  rcu_read_unlock ();

  /* Destroy via mtree_destroy.  This walks and (for our snapshot in
  ** RCU mode) schedules the leaf for RCU free without marking it dead. */
  mtree_destroy (&st->mt);
  shadow_clear (st);

  /* Force grace period.  After this, any RCU-deferred frees have run. */
  rcu_barrier ();

  /* Re-init and verify clean state.  ASAN should catch any leftover
  ** UAF from the destroy path that touches freshly-freed memory. */
  /* mtree_destroy already reset ma_flags via mt_attr; init not needed. */
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  int rc = mtree_store (&st->mt, k, pack_v (v), GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "post-walk-destroy store rc=%d", rc);
  shadow_set_single (st, k, v);
  void *got = mtree_load (&st->mt, k);
  HEGEL_ASSERT (got != NULL && unpack_v (got) == v,
                "post-walk-destroy load mismatch: got=%lu want=%lu",
                got ? unpack_v (got) : 0UL, v);
  st->c_destroy++;
}

/* RCU reader spanning a destroy: a reader positions an iterator inside
** one read-side critical section, the tree is destroyed and repopulated
** with one entry (the writers complete), and the reader then continues
** iterating.  That iteration step starts after the writes returned, so
** it must see the new tree — the entry stored after the destroy if it
** lies past the reader's position, else nothing — not entries from the
** old nodes, which stay readable memory until the grace period ends. */
static void rule_reader_across_destroy (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  if (!mt_in_rcu (&st->mt)) return;
  unsigned long start = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long k = (unsigned long) hegel_draw_int (tc, 1, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
  MA_STATE (mas, &st->mt, start, start);
  rcu_read_lock ();
  /* A tree whose only entry sits in the root pointer has no node to
  ** retire: a reader there may keep its old view, which RCU allows. */
  bool in_node = xa_is_node (rcu_dereference (st->mt.ma_root));
  void *first = mas_find (&mas, ULONG_MAX);
  unsigned long pos = mas.last;
  mtree_destroy (&st->mt);
  shadow_clear (st);
  int rc = mtree_store (&st->mt, k, pack_v (v), GFP_KERNEL);
  void *next = first ? mas_find (&mas, ULONG_MAX) : NULL;
  unsigned long idx = mas.index;
  rcu_read_unlock ();
  rcu_barrier ();
  HEGEL_ASSERT (rc == 0, "reader_across_destroy: store rc=%d", rc);
  shadow_set_single (st, k, v);
  if (!first || !in_node) return;
  void *want = (k > pos) ? pack_v (v) : NULL;
  HEGEL_ASSERT (next == want,
                "mas_find after destroy+store(%lu) from pos %lu returned %lu at "
                "index %lu, want %lu (flags=0x%x)",
                k, pos, next ? unpack_v (next) : 0UL, idx,
                want ? v : 0UL, st->flags);
  st->c_destroy++;
}

/* Cycle the RCU mode on/off — exercises mt_clear_in_rcu / mt_set_in_rcu
** plus subsequent destroy paths under each mode. */
static void rule_toggle_rcu (void *s, hegel_testcase *tc)
{
  (void) tc;
  st_t *st = s;
  if (mt_in_rcu (&st->mt)) {
    mt_clear_in_rcu (&st->mt);
    st->flags &= ~MT_FLAGS_USE_RCU;
  } else {
    mt_set_in_rcu (&st->mt);
    st->flags |= MT_FLAGS_USE_RCU;
  }
}

/* Invariant: mtree_empty agrees with shadow */
static void inv_empty_agrees (const void *s, hegel_testcase *tc)
{
  (void) tc;
  const st_t *st = s;
  bool empty = mtree_empty (&st->mt);
  HEGEL_ASSERT (empty == (st->n_present == 0),
                "mtree_empty()=%d but shadow says %s (n_present=%d, flags=0x%x)",
                (int) empty,
                st->n_present == 0 ? "empty" : "non-empty",
                st->n_present, st->flags);
}

/* Invariant: every shadow-present key reads back the matching value */
static void inv_all_keys_present (const void *s, hegel_testcase *tc)
{
  (void) tc;
  st_t *st = (st_t *) s;
  for (int k = 0; k < KEYSPACE; k++) {
    if (!st->present[k]) continue;
    void *got = mtree_load (&st->mt, (unsigned long) k);
    HEGEL_ASSERT (got != NULL && unpack_v (got) == st->shadow[k],
                  "post-step load(%d) got=%lu shadow=%lu flags=0x%x",
                  k, got ? unpack_v (got) : 0UL, st->shadow[k], st->flags);
  }
}

/* Invariant: no spurious keys */
static void inv_no_phantom_keys (const void *s, hegel_testcase *tc)
{
  (void) tc;
  const st_t *st = s;
  for (int k = 0; k < KEYSPACE; k++) {
    if (st->present[k]) continue;
    void *got = mtree_load ((struct maple_tree *) &st->mt, (unsigned long) k);
    HEGEL_ASSERT (got == NULL,
                  "phantom: mtree_load(%d) returned %p but shadow absent flags=0x%x",
                  k, got, st->flags);
  }
}

/* ---- main ---- */

int main (int argc, char *argv[])
{
  unsigned long n_cases = 400;
  if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

  rcu_register_thread ();
  radix_tree_init ();
  maple_tree_init ();

  printf ("maple_tree destroy/lifecycle PBT (n_cases=%lu, keyspace=%d)\n",
          n_cases, KEYSPACE);

  hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  /* Bias toward store + destroy so the leaf-only destroy path gets
  ** plenty of exercise.  store_2 and destroy_2 are duplicate
  ** registrations to increase relative weight. */
  if (hegel_state_machine_add_rule (sm, "store",        rule_store)        != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_2",      rule_store)        != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_range",  rule_store_range)  != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "insert",       rule_insert)       != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "load",         rule_load)         != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "walk",         rule_walk)         != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "iter",         rule_iter)         != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "destroy",      rule_destroy_and_reinit) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "destroy_2",    rule_destroy_and_reinit) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "toggle_rcu",   rule_toggle_rcu)   != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "walk_then_destroy", rule_walk_then_destroy) != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "reader_across_destroy",
                                    rule_reader_across_destroy) != 0) return 1;

  if (hegel_state_machine_add_rule_with_precondition
        (sm, "erase_existing", any_present, rule_erase_existing) != 0) return 1;

  if (hegel_state_machine_add_invariant
        (sm, "empty_agrees",     inv_empty_agrees) != 0) return 1;
  if (hegel_state_machine_add_invariant
        (sm, "all_keys_present", inv_all_keys_present) != 0) return 1;
  if (hegel_state_machine_add_invariant
        (sm, "no_phantom_keys",  inv_no_phantom_keys) != 0) return 1;

  hegel_state_machine_run_n (sm, n_cases);
  hegel_state_machine_free (sm);

  fprintf (stderr,
           "[maple_destroy PBT coverage]\n"
           "  cases=%ld (rcu=%ld alloc_range=%ld)  max_n_present=%ld\n"
           "  store=%ld store_range=%ld insert=%ld erase=%ld\n"
           "  load=%ld walk=%ld iter=%ld\n"
           "  destroy=%ld (empty=%ld single=%ld multi=%ld)  reinit=%ld\n",
           g_n_cases, g_n_rcu_cases, g_n_alloc_cases, g_max_n_present,
           g_c_store, g_c_store_range, g_c_insert, g_c_erase,
           g_c_load, g_c_walk, g_c_iter,
           g_c_destroy, g_c_destroy_empty, g_c_destroy_single, g_c_destroy_multi,
           g_c_reinit);
  printf ("PASSED\n");
  return 0;
}
