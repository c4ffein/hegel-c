/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Property test for xarray iteration with multi-index entries and
** xas_pause(). Targets the buggy snapshotted xarray.c.
**
** Properties checked:
**   P1 (test_full_enumeration) — xas_for_each() from index 0 visits every
**        stored entry exactly once at its canonical index (in order).
**   P2 (test_pause_resume)     — walking with xas_pause()+resume at an
**        arbitrary subset of iteration positions yields the same
**        sequence as an uninterrupted walk.
**   P2b (test_pause_every)     — same as P2 but pause+resume after every
**        yielded entry, and start the walk at an arbitrary index. This
**        explores the "start mid-multi-index-entry, then pause" path
**        that triggers the xa_index-low-bits bug in xas_pause(). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

#include <linux/xarray.h>

extern void rcu_register_thread (void);
extern void radix_tree_init (void);

/* We work in a bounded keyspace so we can keep a flat shadow.
** With order in [0, 6] each multi-index entry covers up to 64 indices.
** Keep the universe modest so a complete walk is fast. */
#define KEYSPACE      4096
#define MAX_ENTRIES   16
#define MAX_ORDER     10

/* Description of one entry to install. */
struct store_op {
  unsigned long index;          /* canonical (multi-of-2^order) index */
  unsigned int  order;          /* order in [0, MAX_ORDER] */
  unsigned int  value;          /* odd-tagged value */
  int           valid;          /* zeroed if a later store clobbered it */
};

/* Install a multi-index entry into the xarray. The entry covers
** indices [base, base + (1 << order)). Uses xa_store_range — the
** public API for installing entries that span a range. */
static int install (struct xarray *xa, unsigned long base,
                    unsigned int order, void *val)
{
  unsigned long span = 1UL << order;
  unsigned long first = base;
  unsigned long last  = base + span - 1;
  void *r = xa_store_range (xa, first, last, val, GFP_KERNEL);
  if (xa_is_err (r)) return -1;
  return 0;
}

/* ---------------- iteration helpers ---------------- */

/* Walk the whole array via xas_for_each. Fill (idx, val) pairs.
** Returns count, or -1 on overflow. */
static int walk_xas (struct xarray *xa, unsigned long *idx_out,
                     void **val_out, int max)
{
  int n = 0;
  void *entry;
  XA_STATE (xas, xa, 0);

  rcu_read_lock ();
  xas_for_each (&xas, entry, ULONG_MAX) {
    if (n >= max) { rcu_read_unlock (); return -1; }
    idx_out[n] = xas.xa_index;
    val_out[n] = entry;
    n++;
  }
  rcu_read_unlock ();
  return n;
}

/* Walk with a pause+resume at every iteration position in `pause_mask`.
** bit i set => pause after yielding the i-th entry. */
static int walk_xas_with_pause_mask (struct xarray *xa,
                                     uint64_t pause_mask,
                                     unsigned long *idx_out, void **val_out,
                                     int max)
{
  int n = 0;
  void *entry;
  XA_STATE (xas, xa, 0);

  rcu_read_lock ();
  xas_for_each (&xas, entry, ULONG_MAX) {
    if (n >= max) { rcu_read_unlock (); return -1; }
    idx_out[n] = xas.xa_index;
    val_out[n] = entry;
    if (n < 64 && (pause_mask & (1ULL << n))) {
      xas_pause (&xas);
      rcu_read_unlock ();
      rcu_read_lock ();
    }
    n++;
  }
  rcu_read_unlock ();
  return n;
}


/* ---------------- properties ---------------- */

/* Bias the operation generator toward order > 0, since the basic test
** already covers single-index. */
static void draw_op (hegel_testcase *tc, struct store_op *op)
{
  /* 80% multi-index, 20% single-index */
  int multi = hegel_draw_int (tc, 0, 4);
  if (multi == 0) {
    op->order = 0;
  } else {
    op->order = hegel_draw_int (tc, 1, MAX_ORDER);
  }
  unsigned long span = 1UL << op->order;
  /* base must be aligned to 2^order and span must fit in keyspace */
  unsigned long max_base = (KEYSPACE / span) - 1;
  op->index = ((unsigned long) hegel_draw_int (tc, 0, (int) max_base)) * span;
  /* xa_mk_value requires a value that fits in (LONG_BITS - 1); we use
  ** small ints. Use a non-zero value so we can distinguish from NULL. */
  op->value = (unsigned int) hegel_draw_int (tc, 1, 1000);
  op->valid = 1;
}

/* Draw a set of non-overlapping multi-index entries. We focus the test
** on iteration of a clean tree of distinct entries — overlap semantics
** of xas_store at smaller order is not what we're testing. */
static int draw_disjoint_ops (hegel_testcase *tc, struct store_op *ops, int max_ops)
{
  int n_ops = hegel_draw_int (tc, 1, max_ops);
  int kept = 0;
  for (int i = 0; i < n_ops; i++) {
    struct store_op op;
    draw_op (tc, &op);
    unsigned long lo = op.index;
    unsigned long hi = op.index + (1UL << op.order) - 1;
    int overlap = 0;
    for (int j = 0; j < kept; j++) {
      unsigned long jlo = ops[j].index;
      unsigned long jhi = ops[j].index + (1UL << ops[j].order) - 1;
      if (lo <= jhi && jlo <= hi) { overlap = 1; break; }
    }
    if (overlap) continue;
    ops[kept++] = op;
  }
  /* Sort by index. */
  for (int i = 1; i < kept; i++) {
    for (int j = i; j > 0 && ops[j].index < ops[j - 1].index; j--) {
      struct store_op tmp = ops[j];
      ops[j] = ops[j - 1];
      ops[j - 1] = tmp;
    }
  }
  return kept;
}

void test_full_enumeration (hegel_testcase *tc)
{
  DEFINE_XARRAY (xa);

  struct store_op ops[MAX_ENTRIES];
  int n_ops = draw_disjoint_ops (tc, ops, MAX_ENTRIES);
  for (int i = 0; i < n_ops; i++) {
    void *val = xa_mk_value (ops[i].value);
    if (install (&xa, ops[i].index, ops[i].order, val) != 0) {
      hegel_health_fail ("xas_store failed");
      goto cleanup;
    }
  }

  unsigned long got_idx[KEYSPACE];
  void *got_val[KEYSPACE];
  int n_got = walk_xas (&xa, got_idx, got_val, KEYSPACE);
  HEGEL_ASSERT (n_got >= 0, "walk overflow");

  /* P1: full-enumeration matches expected. */
  HEGEL_ASSERT (n_got == n_ops,
                "P1 walk count mismatch: got=%d expected=%d", n_got, n_ops);
  for (int i = 0; i < n_ops; i++) {
    HEGEL_ASSERT (got_idx[i] == ops[i].index,
                  "P1: at slot %d, walk index=%lu expected=%lu (order=%u)",
                  i, got_idx[i], ops[i].index, ops[i].order);
    HEGEL_ASSERT (got_val[i] == xa_mk_value (ops[i].value),
                  "P1: at slot %d, walk value=%p expected value=%u",
                  i, got_val[i], ops[i].value);
  }

  /* P3: strictly increasing indices. */
  for (int i = 1; i < n_got; i++) {
    HEGEL_ASSERT (got_idx[i] > got_idx[i - 1],
                  "P3: indices not strictly increasing: [%d]=%lu [%d]=%lu",
                  i - 1, got_idx[i - 1], i, got_idx[i]);
  }

cleanup:
  xa_destroy (&xa);
}

void test_pause_resume (hegel_testcase *tc)
{
  DEFINE_XARRAY (xa);

  struct store_op ops[MAX_ENTRIES];
  int n_ops = draw_disjoint_ops (tc, ops, MAX_ENTRIES);
  for (int i = 0; i < n_ops; i++) {
    void *val = xa_mk_value (ops[i].value);
    if (install (&xa, ops[i].index, ops[i].order, val) != 0) {
      hegel_health_fail ("xas_store failed");
      goto cleanup;
    }
  }

  unsigned long ref_idx[KEYSPACE];
  void *ref_val[KEYSPACE];
  int n_ref = walk_xas (&xa, ref_idx, ref_val, KEYSPACE);
  HEGEL_ASSERT (n_ref >= 0, "ref walk overflow");

  /* Draw an arbitrary pause-bitmask: bit i set => pause after yielding
  ** the i-th entry. This explores all possible pause-position combos. */
  uint64_t pause_mask = (uint64_t) hegel_draw_u64 (tc, 0, UINT64_MAX);

  unsigned long got_idx[KEYSPACE];
  void *got_val[KEYSPACE];
  int n_got = walk_xas_with_pause_mask (&xa, pause_mask, got_idx, got_val,
                                        KEYSPACE);
  HEGEL_ASSERT (n_got >= 0, "paused walk overflow");

  /* P2: pause+resume identical to single walk. */
  HEGEL_ASSERT (n_got == n_ref,
                "P2: paused-walk count=%d != ref count=%d (pause_mask=0x%llx)",
                n_got, n_ref, (unsigned long long) pause_mask);
  for (int i = 0; i < n_ref; i++) {
    HEGEL_ASSERT (got_idx[i] == ref_idx[i],
                  "P2: index mismatch at slot %d: paused=%lu ref=%lu "
                  "(pause_mask=0x%llx)",
                  i, got_idx[i], ref_idx[i], (unsigned long long) pause_mask);
    HEGEL_ASSERT (got_val[i] == ref_val[i],
                  "P2: value mismatch at slot %d: paused=%p ref=%p "
                  "(pause_mask=0x%llx)",
                  i, got_val[i], ref_val[i], (unsigned long long) pause_mask);
  }

cleanup:
  xa_destroy (&xa);
}

/* Multi-pause test: pause at every iteration step, starting at `start`. */
static int walk_xas_pause_every (struct xarray *xa, unsigned long start,
                                 unsigned long *idx_out, void **val_out,
                                 int max)
{
  int n = 0;
  void *entry;
  XA_STATE (xas, xa, start);

  rcu_read_lock ();
  xas_for_each (&xas, entry, ULONG_MAX) {
    if (n >= max) { rcu_read_unlock (); return -1; }
    idx_out[n] = xas.xa_index;
    val_out[n] = entry;
    n++;
    /* Pause + drop-and-reacquire lock + continue. */
    xas_pause (&xas);
    rcu_read_unlock ();
    rcu_read_lock ();
  }
  rcu_read_unlock ();
  return n;
}

/* Walk without pause, starting at `start`. */
static int walk_xas_from_no_pause (struct xarray *xa, unsigned long start,
                                   unsigned long *idx_out, void **val_out,
                                   int max)
{
  int n = 0;
  void *entry;
  XA_STATE (xas, xa, start);

  rcu_read_lock ();
  xas_for_each (&xas, entry, ULONG_MAX) {
    if (n >= max) { rcu_read_unlock (); return -1; }
    idx_out[n] = xas.xa_index;
    val_out[n] = entry;
    n++;
  }
  rcu_read_unlock ();
  return n;
}

void test_pause_every (hegel_testcase *tc)
{
  DEFINE_XARRAY (xa);

  struct store_op ops[MAX_ENTRIES];
  int n_ops = draw_disjoint_ops (tc, ops, MAX_ENTRIES);
  for (int i = 0; i < n_ops; i++) {
    void *val = xa_mk_value (ops[i].value);
    if (install (&xa, ops[i].index, ops[i].order, val) != 0) {
      hegel_health_fail ("xas_store failed");
      goto cleanup;
    }
  }

  /* Pick a starting index — biased to be either 0 or mid-multi-index. */
  unsigned long start = (unsigned long) hegel_draw_int (tc, 0, KEYSPACE - 1);

  /* Reference: uninterrupted walk from same start. */
  unsigned long ref_idx[KEYSPACE];
  void *ref_val[KEYSPACE];
  int n_ref = walk_xas_from_no_pause (&xa, start, ref_idx, ref_val, KEYSPACE);
  HEGEL_ASSERT (n_ref >= 0, "ref walk overflow");

  /* Pause-every walk should yield the same sequence. */
  unsigned long got_idx[KEYSPACE];
  void *got_val[KEYSPACE];
  int n_got = walk_xas_pause_every (&xa, start, got_idx, got_val, KEYSPACE);
  HEGEL_ASSERT (n_got >= 0, "pause-every walk overflow");

  HEGEL_ASSERT (n_got == n_ref,
                "P2b: pause-every (start=%lu) count=%d != ref count=%d",
                start, n_got, n_ref);
  for (int i = 0; i < n_ref; i++) {
    HEGEL_ASSERT (got_idx[i] == ref_idx[i],
                  "P2b: index mismatch at slot %d (start=%lu): paused=%lu ref=%lu",
                  i, start, got_idx[i], ref_idx[i]);
    HEGEL_ASSERT (got_val[i] == ref_val[i],
                  "P2b: value mismatch at slot %d (start=%lu): paused=%p ref=%p",
                  i, start, got_val[i], ref_val[i]);
  }

cleanup:
  xa_destroy (&xa);
}

int main (void)
{
  rcu_register_thread ();
  radix_tree_init ();

  /* Use _n to run more cases and explore deeper trees. */
  int rc = 0;
  rc |= hegel_run_test_result_n (test_full_enumeration, 500);
  rc |= hegel_run_test_result_n (test_pause_resume, 500);
  rc |= hegel_run_test_result_n (test_pause_every, 500);
  return rc;
}
