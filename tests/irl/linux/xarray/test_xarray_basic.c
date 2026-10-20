/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Property: store-then-load round-trips a value through an XArray.
** For any sequence of (key, value) inserts with distinct keys,
** xa_load(xa, k) returns the most recently stored value at k.
**
** Builds against lib/xarray.c (the actual kernel data structure)
** linked through the same in-tree harness's helper objects (linux.o,
** xarray-shared.o, radix-tree.o, idr.o) plus our own minimal stubs
** for slab/find_bit/bitmap/hweight/vsprintf.  No qemu needed —
** xarray is pure-algorithmic and runs in userspace once the stubs
** stand in for kernel internals.
**
** ASAN + UBSAN are enabled in the Makefile; out-of-bounds and
** undefined-behavior bugs are caught during the test run. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hegel_c.h"

#include <linux/xarray.h>

/* Provided by radix-tree harness init code. */
extern void rcu_register_thread (void);
extern void radix_tree_init (void);

#define MAX_OPS 64

void test_xa_store_load (hegel_testcase *tc)
{
  DEFINE_XARRAY (xa);

  /* Track what we expect at each key in a flat shadow array.
  ** Keys are bounded so we can use a direct-mapped table. */
  enum { KEYSPACE = 32 };
  uintptr_t shadow[KEYSPACE];
  int present[KEYSPACE];
  memset (shadow, 0, sizeof shadow);
  memset (present, 0, sizeof present);

  int n_ops = hegel_draw_int (tc, 1, MAX_OPS);
  for (int i = 0; i < n_ops; i++) {
    int key = hegel_draw_int (tc, 0, KEYSPACE - 1);
    /* Encode the value as an odd integer so xa_mk_value treats it as
    ** an internal value (not a pointer).  Use small ints to avoid
    ** xa_mk_value's 31-bit tagging overflow on 32-bit hosts. */
    int v = hegel_draw_int (tc, 1, 1000);
    void *val = xa_mk_value (v);

    void *prev = xa_store (&xa, key, val, GFP_KERNEL);
    /* xa_store returns the previous value (or NULL).  Validate it
    ** matches our shadow. */
    if (present[key]) {
      HEGEL_ASSERT (prev == (void *) shadow[key],
                    "xa_store returned wrong prev: got %p, expected %p (key=%d)",
                    prev, (void *) shadow[key], key);
    } else {
      HEGEL_ASSERT (prev == NULL,
                    "xa_store on empty slot returned non-NULL: %p (key=%d)",
                    prev, key);
    }

    shadow[key]  = (uintptr_t) val;
    present[key] = 1;
  }

  /* Final consistency check: load every populated key and confirm. */
  for (int k = 0; k < KEYSPACE; k++) {
    void *got = xa_load (&xa, k);
    if (present[k]) {
      HEGEL_ASSERT (got == (void *) shadow[k],
                    "xa_load mismatch at key=%d: got %p, expected %p",
                    k, got, (void *) shadow[k]);
    } else {
      HEGEL_ASSERT (got == NULL,
                    "xa_load on never-set key=%d returned non-NULL: %p",
                    k, got);
    }
  }

  xa_destroy (&xa);
}

int main (void)
{
  rcu_register_thread ();
  radix_tree_init ();
  hegel_run_test_n (test_xa_store_load, 200);
  return 0;
}
