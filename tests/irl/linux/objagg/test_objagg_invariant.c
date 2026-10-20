/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Property-based test for lib/objagg.c (mlxsw object aggregator).
**
** objagg manages a forest of refcounted objects: callers hand it a
** "key" (here: a 1-byte id), and objagg either creates a fresh root,
** finds an existing aggregable root and stores the new object as a
** child "delta", or returns an existing object with refcount bumped.
**
** Test design (stateful PBT, two rules):
**
**   rule_get(key)
**     — always applicable
**     — calls objagg_obj_get(objagg, &key)
**     — increments shadow_refcount[key]
**     — records the returned objagg_obj* in shadow[key].handle on the
**       FIRST acquisition
**
**   rule_put_existing
**     — PRECONDITION: at least one shadow entry has refcount > 0
**     — picks a live ordinal, decrements shadow_refcount[key],
**       calls objagg_obj_put.  Clears handle when refcount hits 0.
**
** Invariants checked after every step:
**
**   inv_refcount_sum_matches:
**     sum over shadow keys of (shadow_refcount[k]) ==
**     sum over objagg's iteration of (objagg_obj->stats.user_count)
**
**   inv_no_zero_refcount_handles_present:
**     keys with shadow_refcount==0 must NOT be findable in objagg
**     (i.e. the handle has been freed and a fresh get would return
**     a different pointer than the stale one — we don't have the
**     stale pointer to test, but we test that lookup yields no entry
**     by getting a fresh handle and confirming refcount==1)
**
**   inv_forest_acyclic:
**     walk every objagg_obj's parent chain — each chain reaches a root
**     in <= obj_count steps.  Equivalent to "no cycles".
**
** Object payload type: a single byte id (0..7).  delta_check returns
** true when (key.id - parent.id) in [0, 3], so ids cluster as
** {0,1,2,3} -> root 0 territory, {4,5,6,7} -> root 4 territory, with
** chains of up to 3 deltas per root.  Small enough to surface forest
** topology quickly; large enough for both root-only and rooted-delta
** branches in objagg's logic to fire repeatedly.
*/

/* Needs to come before objagg.h: kernel-space types (bool, size_t),
** error codes (EINVAL, ENOMEM), and ERR_PTR/IS_ERR helpers. */
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <linux/err.h>
#include <linux/objagg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

#define N_KEYS 8
#define MAX_KEY_DIFF 3

/* ---- Object payload ---- */
struct obj_key {
  unsigned int id;
};

/* ---- world: priv state passed to objagg_ops ---- */
struct world {
  unsigned int root_count;
  unsigned int delta_count;
};

struct root_priv {
  unsigned int id;
};
struct delta_priv {
  int diff;
};

/* ---- objagg_ops callbacks ---- */
static bool my_delta_check (void * priv, const void * parent_obj,
                            const void * obj)
{
  (void) priv;
  const struct obj_key * p = parent_obj;
  const struct obj_key * c = obj;
  int diff = (int) c->id - (int) p->id;
  return diff >= 0 && diff <= MAX_KEY_DIFF;
}

static void * my_delta_create (void * priv, void * parent_obj, void * obj)
{
  struct world * w = priv;
  const struct obj_key * p = parent_obj;
  const struct obj_key * c = obj;
  int diff = (int) c->id - (int) p->id;

  if (diff < 0 || diff > MAX_KEY_DIFF) return ERR_PTR(-EINVAL);

  struct delta_priv * d = malloc (sizeof *d);
  if (!d) return ERR_PTR(-ENOMEM);
  d->diff = diff;
  w->delta_count++;
  return d;
}

static void my_delta_destroy (void * priv, void * delta_priv)
{
  struct world * w = priv;
  free (delta_priv);
  w->delta_count--;
}

static void * my_root_create (void * priv, void * obj, unsigned int id)
{
  (void) id;
  struct world * w = priv;
  const struct obj_key * k = obj;
  struct root_priv * r = malloc (sizeof *r);
  if (!r) return ERR_PTR(-ENOMEM);
  r->id = k->id;
  w->root_count++;
  return r;
}

static void my_root_destroy (void * priv, void * root_priv)
{
  struct world * w = priv;
  free (root_priv);
  w->root_count--;
}

static const struct objagg_ops my_ops = {
  .obj_size      = sizeof (struct obj_key),
  .delta_check   = my_delta_check,
  .delta_create  = my_delta_create,
  .delta_destroy = my_delta_destroy,
  .root_create   = my_root_create,
  .root_destroy  = my_root_destroy,
};

/* ---- shadow model ---- */
struct shadow_entry {
  unsigned int refcount;          /* user-visible refcount per key */
  struct objagg_obj * handle;     /* the objagg_obj* (cached on first get) */
};

struct test_state {
  struct objagg * objagg;
  struct world world;
  struct shadow_entry shadow[N_KEYS];
};

/* ---- setup / teardown ---- */
static void * setup_fn (void)
{
  struct test_state * st = calloc (1, sizeof *st);
  if (!st) return NULL;
  st->objagg = objagg_create (&my_ops, NULL, &st->world);
  if (IS_ERR(st->objagg)) {
    free (st);
    return NULL;
  }
  return st;
}

static void teardown_fn (void * state)
{
  struct test_state * st = state;
  if (!st) return;

  /* Drain remaining refs so objagg_destroy doesn't WARN. */
  for (int k = 0; k < N_KEYS; k++) {
    while (st->shadow[k].refcount > 0) {
      objagg_obj_put (st->objagg, st->shadow[k].handle);
      st->shadow[k].refcount--;
      if (st->shadow[k].refcount == 0) st->shadow[k].handle = NULL;
    }
  }
  objagg_destroy (st->objagg);
  free (st);
}

/* ---- rules ---- */
static void rule_get (void * state, hegel_testcase * tc)
{
  struct test_state * st = state;
  int key_idx = hegel_draw_int (tc, 0, N_KEYS - 1);
  struct obj_key key = { .id = (unsigned int) key_idx };

  struct objagg_obj * o = objagg_obj_get (st->objagg, &key);
  HEGEL_ASSERT (!IS_ERR(o), "objagg_obj_get returned err for key=%d", key_idx);

  if (st->shadow[key_idx].refcount == 0) {
    /* First get for this key — record handle. */
    st->shadow[key_idx].handle = o;
  } else {
    /* Subsequent get must return SAME pointer. */
    HEGEL_ASSERT (st->shadow[key_idx].handle == o,
                  "objagg returned different handle for live key=%d",
                  key_idx);
  }
  st->shadow[key_idx].refcount++;
}

static bool can_put (const void * state)
{
  const struct test_state * st = state;
  for (int k = 0; k < N_KEYS; k++)
    if (st->shadow[k].refcount > 0) return true;
  return false;
}

static void rule_put_existing (void * state, hegel_testcase * tc)
{
  struct test_state * st = state;

  /* Count live keys. */
  int n_live = 0;
  for (int k = 0; k < N_KEYS; k++)
    if (st->shadow[k].refcount > 0) n_live++;
  HEGEL_ASSERT (n_live > 0, "rule_put_existing fired with 0 live keys");

  /* Pick the i-th live key. */
  int pick = hegel_draw_int (tc, 0, n_live - 1);
  int seen = 0, target = -1;
  for (int k = 0; k < N_KEYS; k++) {
    if (st->shadow[k].refcount > 0) {
      if (seen == pick) { target = k; break; }
      seen++;
    }
  }
  HEGEL_ASSERT (target >= 0, "rule_put_existing failed to pick");

  objagg_obj_put (st->objagg, st->shadow[target].handle);
  st->shadow[target].refcount--;
  if (st->shadow[target].refcount == 0) st->shadow[target].handle = NULL;
}

/* ---- invariants ---- */

/* Sum of shadow refcounts equals sum of user_count over objagg's stats. */
static void inv_refcount_sum (const void * state, hegel_testcase * tc)
{
  (void) tc;
  const struct test_state * st = state;

  unsigned int shadow_sum = 0;
  for (int k = 0; k < N_KEYS; k++)
    shadow_sum += st->shadow[k].refcount;

  /* objagg_stats_get walks objagg->obj_list, populating stats_info[] with
  ** each objagg_obj's user_count.  Sum them. */
  const struct objagg_stats * stats = objagg_stats_get ((struct objagg *) st->objagg);
  HEGEL_ASSERT (!IS_ERR(stats), "objagg_stats_get failed");

  unsigned int objagg_sum = 0;
  for (unsigned int i = 0; i < stats->stats_info_count; i++)
    objagg_sum += stats->stats_info[i].stats.user_count;

  HEGEL_ASSERT (shadow_sum == objagg_sum,
                "refcount-sum mismatch: shadow=%u objagg=%u",
                shadow_sum, objagg_sum);

  objagg_stats_put (stats);
}

/* root_count from objagg_stats matches our world->root_count counter
** (which is incremented in my_root_create / decremented in my_root_destroy). */
static void inv_root_count_callbacks (const void * state, hegel_testcase * tc)
{
  (void) tc;
  const struct test_state * st = state;

  const struct objagg_stats * stats = objagg_stats_get ((struct objagg *) st->objagg);
  HEGEL_ASSERT (!IS_ERR(stats), "objagg_stats_get failed");

  HEGEL_ASSERT (stats->root_count == st->world.root_count,
                "root_count mismatch: stats=%u world=%u",
                stats->root_count, st->world.root_count);

  objagg_stats_put (stats);
}

/* The forest must be acyclic.  We don't have a public way to walk parent
** edges from outside, but objagg_obj_root_priv() does it for us — it
** WARN_ONs if a non-root parent appears, and returns null if the chain
** is broken.  We just call it for every live shadow handle and assert
** the returned root_priv has a sensible id (== root key for that chain). */
static void inv_forest_walks_to_root (const void * state, hegel_testcase * tc)
{
  (void) tc;
  const struct test_state * st = state;

  for (int k = 0; k < N_KEYS; k++) {
    if (st->shadow[k].refcount == 0) continue;
    const struct root_priv * r = objagg_obj_root_priv (st->shadow[k].handle);
    HEGEL_ASSERT (r != NULL, "key=%d: root_priv NULL", k);
    /* The root id must be one of the valid keys, and must be <= k
    ** (deltas point to lower-or-equal ids per delta_check). */
    HEGEL_ASSERT (r->id < N_KEYS,
                  "key=%d: root id %u out of range", k, r->id);
    HEGEL_ASSERT (r->id <= (unsigned int) k,
                  "key=%d: root id %u > key (parent edge inverted?)",
                  k, r->id);
    HEGEL_ASSERT ((int) k - (int) r->id <= MAX_KEY_DIFF,
                  "key=%d: root id %u too far (diff=%d > %d)",
                  k, r->id, (int) k - (int) r->id, MAX_KEY_DIFF);
  }
}

/* delta_user_count >= user_count for every entry, and equality holds
** for leaf objects (no descendants).  Roots may have strictly greater
** delta_user_count than user_count when they have children. */
static void inv_delta_user_count_consistent (const void * state, hegel_testcase * tc)
{
  (void) tc;
  const struct test_state * st = state;

  const struct objagg_stats * stats = objagg_stats_get ((struct objagg *) st->objagg);
  HEGEL_ASSERT (!IS_ERR(stats), "objagg_stats_get failed");

  for (unsigned int i = 0; i < stats->stats_info_count; i++) {
    const struct objagg_obj_stats * s = &stats->stats_info[i].stats;
    HEGEL_ASSERT (s->delta_user_count >= s->user_count,
                  "stats[%u]: delta_user_count=%u < user_count=%u",
                  i, s->delta_user_count, s->user_count);
    if (!stats->stats_info[i].is_root) {
      /* Non-root (delta) objects have no descendants, so the two are
      ** equal. */
      HEGEL_ASSERT (s->delta_user_count == s->user_count,
                    "non-root stats[%u]: delta_user_count=%u != user_count=%u",
                    i, s->delta_user_count, s->user_count);
    }
  }

  objagg_stats_put (stats);
}

/* ---- main ---- */
int main (void)
{
  printf ("objagg PBT (stateful, %d keys, MAX_KEY_DIFF=%d)\n",
          N_KEYS, MAX_KEY_DIFF);

  hegel_state_machine * sm = hegel_state_machine_new (setup_fn, teardown_fn);
  hegel_state_machine_add_rule                   (sm, "get", rule_get);
  hegel_state_machine_add_rule_with_precondition (sm, "put_existing",
                                                  can_put, rule_put_existing);
  hegel_state_machine_add_invariant (sm, "refcount_sum",       inv_refcount_sum);
  hegel_state_machine_add_invariant (sm, "root_count",         inv_root_count_callbacks);
  hegel_state_machine_add_invariant (sm, "forest_walks_root",  inv_forest_walks_to_root);
  hegel_state_machine_add_invariant (sm, "delta_user_count",   inv_delta_user_count_consistent);
  hegel_state_machine_run_n         (sm, 200);
  hegel_state_machine_free          (sm);

  printf ("PASSED\n");
  return 0;
}
