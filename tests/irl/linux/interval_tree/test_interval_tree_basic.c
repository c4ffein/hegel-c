/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful PBT against lib/interval_tree.c.
**
** interval_tree is an augmented rbtree (each node carries
** __subtree_last) used by mm/userfaultfd, drivers/iommu, and others
** for "find all intervals overlapping [lo, hi]" queries.  The C wrapper
** in lib/interval_tree.c is just an INTERVAL_TREE_DEFINE macro
** expansion of the generic in include/linux/interval_tree_generic.h.
**
** State machine rules:
**   rule_insert        — insert a fresh interval [s, e] (s <= e).
**                        Precondition: shadow has < MAX_NODES.
**   rule_remove_existing — pick an existing node by ordinal, remove.
**                          Precondition: shadow non-empty.
**   rule_query_overlap — issue an overlap query [qs, qe], compare
**                        result set to shadow filter.
**
** Shadow: array of (start, end, node*) up to MAX_NODES.
**
** Invariant after each rule: for several fixed (Q_LO, Q_HI) probes plus
** the freshly drawn (qs, qe) inside rule_query_overlap, walking the tree
** via interval_tree_iter_first / _iter_next yields exactly the set of
** shadow entries where start <= Q_HI && end >= Q_LO.  No extras, no
** missing.
**
** Friction notes are gathered at the end of main().
*/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

/* Pull harness shims first — these set up bug.h, types, etc. so the
** kernel headers below find WARN_ON, BUG_ON, gfp_t, etc. */
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/rbtree.h>
#include <linux/interval_tree.h>

/* interval_tree + rbtree don't touch RCU or the radix-tree node cache,
** so we skip rcu_register_thread / radix_tree_init.  The harness's
** linux.o still satisfies a few generic symbols (printk, etc.). */

#define MAX_NODES   32
#define KEYSPACE    256        /* endpoints in [0, KEYSPACE) */
#define MAX_LEN     32         /* per-interval (last - start) cap */

typedef struct {
  unsigned long start;
  unsigned long last;
  struct interval_tree_node *node;   /* heap-allocated, owned by shadow */
} shadow_entry_t;

typedef struct {
  struct rb_root_cached root;
  shadow_entry_t shadow[MAX_NODES];
  int n_present;

  /* Coverage counters (per-case scope). */
  long c_insert;
  long c_remove;
  long c_query_drawn;
  long c_inv_probes;
  long c_inv_hits;        /* total nodes returned across all probes */
} st_t;

static long g_c_insert;
static long g_c_remove;
static long g_c_query_drawn;
static long g_c_inv_probes;
static long g_c_inv_hits;
static long g_n_cases;
static long g_max_n_present;
static long g_total_max_size_cases;   /* cases that reached MAX_NODES */

/* Fixed query probes — exercised every step.  Mix of small/large
** windows including the whole space and a degenerate point. */
static const struct { unsigned long lo, hi; } G_PROBES[] = {
  {  0,            0           },
  {  0,            KEYSPACE-1  },
  { 50,            60          },
  {100,           200          },
  {KEYSPACE/2,    KEYSPACE/2   },
};
#define N_PROBES ((int) (sizeof (G_PROBES) / sizeof (G_PROBES[0])))

/* ---- setup / teardown ---- */

static void *setup_fn (void)
{
  st_t *st = calloc (1, sizeof (*st));
  if (!st) return NULL;
  st->root = (struct rb_root_cached) RB_ROOT_CACHED;
  return st;
}

static void teardown_fn (void *s)
{
  st_t *st = s;
  if (!st) return;
  /* Unlink every shadow node and free the heap memory. */
  for (int i = 0; i < st->n_present; i++) {
    interval_tree_remove (st->shadow[i].node, &st->root);
    free (st->shadow[i].node);
  }
  /* accumulate coverage */
  g_c_insert       += st->c_insert;
  g_c_remove       += st->c_remove;
  g_c_query_drawn  += st->c_query_drawn;
  g_c_inv_probes   += st->c_inv_probes;
  g_c_inv_hits     += st->c_inv_hits;
  g_n_cases++;
  if (st->n_present > g_max_n_present) g_max_n_present = st->n_present;
  if (st->n_present == MAX_NODES) g_total_max_size_cases++;
  free (st);
}

/* ---- preconditions ---- */

static bool shadow_nonempty (const void *s)
{
  return ((const st_t *) s)->n_present > 0;
}

static bool shadow_not_full (const void *s)
{
  return ((const st_t *) s)->n_present < MAX_NODES;
}

/* ---- core: walk tree for [qs, qe], collect into ptr array ---- */
static int
walk_tree_into (const st_t *st, unsigned long qs, unsigned long qe,
                struct interval_tree_node **out, int cap)
{
  int n = 0;
  /* The interval tree iterators take a non-const root pointer; we
  ** discipline ourselves not to mutate during walks. */
  struct rb_root_cached *r = (struct rb_root_cached *) &st->root;
  for (struct interval_tree_node *node = interval_tree_iter_first (r, qs, qe);
       node;
       node = interval_tree_iter_next (node, qs, qe)) {
    if (n >= cap) break;
    out[n++] = node;
  }
  return n;
}

/* Bitmap helpers (small, MAX_NODES-bit). */
static void bm_set    (uint64_t *bm, int i) { bm[i >> 6] |=  (1ULL << (i & 63)); }
static int  bm_test   (const uint64_t *bm, int i)
                                            { return (bm[i >> 6] >> (i & 63)) & 1; }
#define BM_WORDS ((MAX_NODES + 63) / 64)

/* Compare tree-walk result against shadow-filter for (qs, qe). */
static void
check_query (st_t *st, unsigned long qs, unsigned long qe,
             hegel_testcase *tc)
{
  (void) tc;
  /* Build expected bitmap from shadow. */
  uint64_t want[BM_WORDS] = {0};
  int want_n = 0;
  for (int i = 0; i < st->n_present; i++) {
    unsigned long s = st->shadow[i].start, l = st->shadow[i].last;
    if (s <= qe && qs <= l) {
      bm_set (want, i);
      want_n++;
    }
  }

  /* Walk the tree. */
  struct interval_tree_node *got_nodes[MAX_NODES * 2];
  int got_n = walk_tree_into (st, qs, qe, got_nodes,
                              (int) (sizeof got_nodes / sizeof got_nodes[0]));

  HEGEL_ASSERT (got_n == want_n,
                "iter count mismatch for [%lu,%lu]: got=%d want=%d",
                qs, qe, got_n, want_n);

  /* Map each got_nodes[i] back to a shadow index by pointer identity. */
  uint64_t got_bm[BM_WORDS] = {0};
  for (int i = 0; i < got_n; i++) {
    int found = -1;
    for (int j = 0; j < st->n_present; j++) {
      if (st->shadow[j].node == got_nodes[i]) { found = j; break; }
    }
    HEGEL_ASSERT (found >= 0,
                  "iter returned node %p not in shadow (qs=%lu qe=%lu)",
                  (void *) got_nodes[i], qs, qe);
    HEGEL_ASSERT (!bm_test (got_bm, found),
                  "iter returned node idx=%d twice (qs=%lu qe=%lu)",
                  found, qs, qe);
    bm_set (got_bm, found);

    /* And the node's own [start,last] really overlaps [qs,qe]. */
    HEGEL_ASSERT (got_nodes[i]->start <= qe && qs <= got_nodes[i]->last,
                  "iter returned node [%lu,%lu] not overlapping [%lu,%lu]",
                  got_nodes[i]->start, got_nodes[i]->last, qs, qe);
  }

  /* Bitmap equal check. */
  for (int w = 0; w < BM_WORDS; w++) {
    HEGEL_ASSERT (got_bm[w] == want[w],
                  "bitmap mismatch w=%d got=0x%lx want=0x%lx (qs=%lu qe=%lu)",
                  w, (unsigned long) got_bm[w], (unsigned long) want[w], qs, qe);
  }

  st->c_inv_probes++;
  st->c_inv_hits += got_n;
}

/* ---- rule: insert(start, end) ---- */
static void rule_insert (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  /* Precondition guards against full, but defensively no-op. */
  if (st->n_present >= MAX_NODES) return;

  unsigned long start = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long len   = (unsigned long) hegel_draw_int (tc, 0, MAX_LEN - 1);
  unsigned long last  = start + len;
  if (last >= KEYSPACE) last = KEYSPACE - 1;

  struct interval_tree_node *n = calloc (1, sizeof (*n));
  HEGEL_ASSERT (n != NULL, "calloc failed");
  n->start = start;
  n->last  = last;
  interval_tree_insert (n, &st->root);

  shadow_entry_t *e = &st->shadow[st->n_present++];
  e->start = start;
  e->last  = last;
  e->node  = n;
  st->c_insert++;
}

/* ---- rule: remove an existing node by ordinal ----
** Precondition: shadow non-empty.  Friction note A — same as maple_tree:
** "pick from existing" wants a `hegel_draw_index_of` primitive. */
static void rule_remove_existing (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  int ord = hegel_draw_int (tc, 0, st->n_present - 1);
  shadow_entry_t *e = &st->shadow[ord];

  interval_tree_remove (e->node, &st->root);
  free (e->node);

  /* Compact the shadow array. */
  if (ord != st->n_present - 1)
    st->shadow[ord] = st->shadow[st->n_present - 1];
  st->n_present--;
  st->c_remove++;
}

/* ---- rule: query_overlap(qs, qe) — performs its own check immediately,
** in addition to the post-step invariant fan-out. */
static void rule_query_overlap (void *s, hegel_testcase *tc)
{
  st_t *st = s;
  unsigned long a = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long b = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);
  unsigned long qs = a < b ? a : b;
  unsigned long qe = a < b ? b : a;
  check_query (st, qs, qe, tc);
  st->c_query_drawn++;
}

/* ---- invariant: every fixed probe must round-trip. ---- */
static void inv_probes (const void *s, hegel_testcase *tc)
{
  st_t *st = (st_t *) s;
  for (int i = 0; i < N_PROBES; i++)
    check_query (st, G_PROBES[i].lo, G_PROBES[i].hi, tc);
}

/* ---- main ---- */

int main (int argc, char *argv[])
{
  unsigned long n_cases = 200;
  if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

  printf ("interval_tree stateful PBT (n_cases=%lu, max_nodes=%d, keyspace=%d)\n",
          n_cases, MAX_NODES, KEYSPACE);

  hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

  /* Insert is gated by "not full" precondition.  Two registrations to
  ** bias toward inserts (otherwise pure-empty start makes remove
  ** unreachable in short runs).  Friction note B (per-rule weights). */
  if (hegel_state_machine_add_rule_with_precondition
        (sm, "insert",   shadow_not_full, rule_insert) != 0) return 1;
  if (hegel_state_machine_add_rule_with_precondition
        (sm, "insert_2", shadow_not_full, rule_insert) != 0) return 1;
  if (hegel_state_machine_add_rule_with_precondition
        (sm, "remove",   shadow_nonempty, rule_remove_existing) != 0) return 1;
  if (hegel_state_machine_add_rule
        (sm, "query",    rule_query_overlap) != 0) return 1;

  if (hegel_state_machine_add_invariant
        (sm, "probes_match_shadow", inv_probes) != 0) return 1;

  hegel_state_machine_run_n (sm, n_cases);
  hegel_state_machine_free (sm);

  fprintf (stderr,
           "[interval_tree PBT coverage]\n"
           "  cases=%ld  max_n_present=%ld  cases_at_max=%ld\n"
           "  insert=%ld  remove=%ld  query_drawn=%ld\n"
           "  invariant_probes=%ld  invariant_total_hits=%ld\n",
           g_n_cases, g_max_n_present, g_total_max_size_cases,
           g_c_insert, g_c_remove, g_c_query_drawn,
           g_c_inv_probes, g_c_inv_hits);
  printf ("PASSED\n");
  return 0;
}
