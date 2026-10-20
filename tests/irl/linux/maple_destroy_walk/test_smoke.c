/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Smoke test: snapshotted maple_tree.c links and a trivial mtree_alloc
** round-trips.  Confirms the build chain only. */

#include <stdio.h>
#include <stdlib.h>

#include <linux/bug.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/rcupdate.h>
#include <linux/maple_tree.h>

extern void rcu_register_thread (void);
extern void maple_tree_init (void);

int
main (void)
{
  rcu_register_thread ();
  maple_tree_init ();

  DEFINE_MTREE (mt);
  int r = mtree_insert_range (&mt, 1, 10, (void *) 0xabc, GFP_KERNEL);
  printf ("smoke: mtree_insert_range -> %d (expected 0)\n", r);
  if (r != 0) return 1;
  void *v = mtree_load (&mt, 5);
  printf ("smoke: mtree_load(5) -> %p (expected 0xabc)\n", v);
  if (v != (void *) 0xabc) return 1;
  mtree_destroy (&mt);
  printf ("smoke OK\n");
  return 0;
}
