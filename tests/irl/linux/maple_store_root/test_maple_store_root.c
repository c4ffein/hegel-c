/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Focused stateful PBT against mas_store_root / small-tree behavior.
**
** Targets: root and "just expanded past root" tree shapes.  Biases the
** rule distribution toward NULL stores and toward very small trees
** (KEYSPACE=8, only a few rules per case in practice).
**
** State machine:
**   rule_store_nonnull         — mtree_store(k, v) with v != NULL
**   rule_store_null            — mtree_store(k, NULL)   (HEAVY weight)
**   rule_store_range_nonnull   — mtree_store_range(a, b, v) with v != NULL
**   rule_store_range_null      — mtree_store_range(a, b, NULL)  (HEAVY)
**   rule_erase_existing        — mtree_erase(k) when shadow non-empty
**   rule_load_random           — mtree_load(k), compare with shadow
**
** Invariants:
**   inv_load_matches_shadow    — for every key, mtree_load(k) == shadow[k]
**   inv_empty_iff_shadow_empty — mtree_empty() == (shadow has no non-NULL entries)
**
** The "empty iff" invariant is the one expected to fire on the buggy
** mas_store_root.  Storing NULL at a non-zero key on an already-empty
** tree wrongly takes the mas_root_expand path and leaves ma_root
** pointing at a real node (so mtree_empty() returns false) even though
** every load returns NULL. */

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

/* Small keyspace — keep the tree near the root/small-tree boundary. */
#define KEYSPACE   8

typedef struct {
  struct maple_tree mt;
  /* Per-key shadow value.  0 means "absent / NULL".  Non-zero means
  ** the encoded xa_mk_value(v) pointer the tree should return for k. */
  unsigned long shadow[KEYSPACE];
  int           n_present;
  /* Per-key id of the store call that wrote it: keys sharing an id and
  ** contiguous are one tree entry (what mtree_erase removes at once). */
  int           group[KEYSPACE];
  int           next_group;

  /* Per-case coverage counters. */
  long c_store_nn;
  long c_store_null;
  long c_store_range_nn;
  long c_store_range_null;
  long c_erase;
  long c_load_hit;
  long c_load_miss;
  long c_inv_empty_checks;
} st_t;

static long g_n_cases;
static long g_c_store_nn, g_c_store_null;
static long g_c_store_range_nn, g_c_store_range_null;
static long g_c_erase, g_c_load_hit, g_c_load_miss;
static long g_c_inv_empty_checks;
static long g_max_n_present;

/* ---- value packing ---- */

static inline void *pack_v (unsigned long v)
{
  return xa_mk_value (v);
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
  g_c_store_nn         += st->c_store_nn;
  g_c_store_null       += st->c_store_null;
  g_c_store_range_nn   += st->c_store_range_nn;
  g_c_store_range_null += st->c_store_range_null;
  g_c_erase            += st->c_erase;
  g_c_load_hit         += st->c_load_hit;
  g_c_load_miss        += st->c_load_miss;
  g_c_inv_empty_checks += st->c_inv_empty_checks;
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

/* ---- shadow updates ---- */

static void shadow_set_single (st_t *st, unsigned long k, unsigned long v)
{
  if (k >= KEYSPACE) return;
  int was = (st->shadow[k] != 0);
  int now = (v != 0);
  st->shadow[k] = v;
  st->group[k] = ++st->next_group;
  if (!was && now) st->n_present++;
  if (was && !now) st->n_present--;
}

static void shadow_set_range (st_t *st, unsigned long a, unsigned long b,
                              unsigned long v)
{
  unsigned long first = a < b ? a : b;
  unsigned long last  = a < b ? b : a;
  if (last >= KEYSPACE) last = KEYSPACE - 1;
  int gid = ++st->next_group;
  for (unsigned long k = first; k <= last; k++) {
    int was = (st->shadow[k] != 0);
    int now = (v != 0);
    st->shadow[k] = v;
    st->group[k] = gid;
    if (!was && now) st->n_present++;
    if (was && !now) st->n_present--;
  }
}

/* ---- rules ---- */

static void rule_store_nonnull (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 100);
  void *val = pack_v (v);
  int rc = mtree_store (&st->mt, k, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mtree_store(k=%lu, v!=NULL) rc=%d", k, rc);
  shadow_set_single (st, k, (unsigned long) val);
  st->c_store_nn++;
}

static void rule_store_null (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  int rc = mtree_store (&st->mt, k, NULL, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mtree_store(k=%lu, NULL) rc=%d", k, rc);
  shadow_set_single (st, k, 0);
  st->c_store_null++;
}

static void rule_store_range_nonnull (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long a = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long b = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 100);
  unsigned long first = a < b ? a : b;
  unsigned long last  = a < b ? b : a;
  void *val = pack_v (v);
  int rc = mtree_store_range (&st->mt, first, last, val, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mtree_store_range(%lu..%lu, v!=NULL) rc=%d",
                first, last, rc);
  shadow_set_range (st, first, last, (unsigned long) val);
  st->c_store_range_nn++;
}

static void rule_store_range_null (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long a = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long b = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long first = a < b ? a : b;
  unsigned long last  = a < b ? b : a;
  int rc = mtree_store_range (&st->mt, first, last, NULL, GFP_KERNEL);
  HEGEL_ASSERT (rc == 0, "mtree_store_range(%lu..%lu, NULL) rc=%d",
                first, last, rc);
  shadow_set_range (st, first, last, 0);
  st->c_store_range_null++;
}

static void rule_erase_existing (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  /* Walk dense shadow to ordinal — same friction as test_maple_basic. */
  int ord = hegel_draw_int (tc, 0, st->n_present - 1);
  int seen = 0;
  unsigned long k = 0;
  for (int i = 0; i < KEYSPACE; i++) {
    if (st->shadow[i] != 0) {
      if (seen == ord) { k = (unsigned long) i; break; }
      seen++;
    }
  }
  unsigned long expected = st->shadow[k];
  void *prev = mtree_erase (&st->mt, k);
  HEGEL_ASSERT (prev != NULL,
                "mtree_erase(%lu) returned NULL but shadow had %lu",
                k, expected);
  HEGEL_ASSERT ((unsigned long) prev == expected,
                "mtree_erase(%lu) prev mismatch: got=%lu want=%lu",
                k, (unsigned long) prev, expected);
  /* mtree_erase clears the entire range covering k.  For our shadow,
  ** that means: walk left/right over the contiguous keys written by the
  ** same store call (adjacent entries are not merged, even when their
  ** values are equal). */
  int gid = st->group[k];
  for (long i = (long) k; i >= 0 && st->shadow[i] && st->group[i] == gid; i--) {
    st->shadow[i] = 0;
    st->n_present--;
  }
  for (long i = (long) k + 1;
       i < KEYSPACE && st->shadow[i] && st->group[i] == gid; i++) {
    st->shadow[i] = 0;
    st->n_present--;
  }
  st->c_erase++;
}

static void rule_load_random (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long k = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  void *got = mtree_load (&st->mt, k);
  if (st->shadow[k]) {
    HEGEL_ASSERT (got != NULL,
                  "mtree_load(%lu) returned NULL but shadow has %lu",
                  k, st->shadow[k]);
    HEGEL_ASSERT ((unsigned long) got == st->shadow[k],
                  "mtree_load(%lu) got=%lu shadow=%lu",
                  k, (unsigned long) got, st->shadow[k]);
    st->c_load_hit++;
  } else {
    HEGEL_ASSERT (got == NULL,
                  "mtree_load(%lu) returned %lu but shadow says absent",
                  k, (unsigned long) got);
    st->c_load_miss++;
  }
}

/* ---- invariants ---- */

static void inv_load_matches_shadow (const void *s, hegel_testcase *tc)
{
  (void) tc;
  const st_t *st_const = s;
  /* mtree_load is mutating in the sense of needing a tree pointer but
  ** semantically a read.  Cast away const to call it. */
  st_t *st = (st_t *) st_const;
  for (int i = 0; i < KEYSPACE; i++) {
    void *got = mtree_load (&st->mt, (unsigned long) i);
    if (st->shadow[i]) {
      HEGEL_ASSERT (got != NULL,
                    "inv_load: mtree_load(%d) NULL but shadow has %lu",
                    i, st->shadow[i]);
      HEGEL_ASSERT ((unsigned long) got == st->shadow[i],
                    "inv_load: mtree_load(%d) got=%lu shadow=%lu",
                    i, (unsigned long) got, st->shadow[i]);
    } else {
      HEGEL_ASSERT (got == NULL,
                    "inv_load: mtree_load(%d) returned %lu but shadow says absent",
                    i, (unsigned long) got);
    }
  }
}

/* The bug-detector: mtree_empty() must agree with "shadow contains no
** non-NULL entry".  On the buggy mas_store_root, storing NULL at any
** non-zero key on an empty tree creates a real node — mtree_empty
** returns false even though every load returns NULL. */
static void inv_empty_iff_shadow_empty (const void *s, hegel_testcase *tc)
{
  (void) tc;
  st_t *st = (st_t *) s;
  bool empty = mtree_empty (&st->mt);
  bool shadow_empty = (st->n_present == 0);
  HEGEL_ASSERT (empty == shadow_empty,
                "mtree_empty()=%d but shadow_empty=%d (n_present=%d)",
                (int) empty, (int) shadow_empty, st->n_present);
  st->c_inv_empty_checks++;
}

/* ---- main ---- */

int main (int argc, char *argv[])
{
  unsigned long n_cases = 300;
  if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

  rcu_register_thread ();
  radix_tree_init ();
  maple_tree_init ();

  printf ("maple_store_root focused PBT (n_cases=%lu, keyspace=%d)\n",
          n_cases, KEYSPACE);

  hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  /* NULL-heavy rule weighting (duplicate names = higher selection
  ** probability — same idiom test_maple_basic uses).  Goal: hit small
  ** trees with lots of NULL store operations. */
  if (hegel_state_machine_add_rule (sm, "store_null_1",       rule_store_null)         != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_null_2",       rule_store_null)         != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_null_3",       rule_store_null)         != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_range_null_1", rule_store_range_null)   != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_range_null_2", rule_store_range_null)   != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_nn",           rule_store_nonnull)      != 0) return 1;
  if (hegel_state_machine_add_rule (sm, "store_range_nn",     rule_store_range_nonnull)!= 0) return 1;
  if (hegel_state_machine_add_rule (sm, "load",               rule_load_random)        != 0) return 1;

  if (hegel_state_machine_add_rule_with_precondition
        (sm, "erase_existing", any_present, rule_erase_existing) != 0)
    return 1;

  if (hegel_state_machine_add_invariant
        (sm, "load_matches_shadow", inv_load_matches_shadow) != 0)
    return 1;
  if (hegel_state_machine_add_invariant
        (sm, "empty_iff_shadow_empty", inv_empty_iff_shadow_empty) != 0)
    return 1;

  hegel_state_machine_run_n (sm, n_cases);
  hegel_state_machine_free (sm);

  fprintf (stderr,
           "[maple_store_root PBT coverage]\n"
           "  cases=%ld  max_n_present=%ld\n"
           "  store_nn=%ld  store_null=%ld\n"
           "  store_range_nn=%ld  store_range_null=%ld\n"
           "  erase=%ld  load_hit=%ld  load_miss=%ld\n"
           "  inv_empty_checks=%ld\n",
           g_n_cases, g_max_n_present,
           g_c_store_nn, g_c_store_null,
           g_c_store_range_nn, g_c_store_range_null,
           g_c_erase, g_c_load_hit, g_c_load_miss,
           g_c_inv_empty_checks);
  printf ("PASSED\n");
  return 0;
}
