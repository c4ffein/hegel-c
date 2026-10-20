/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Smoke test: snapshotted (buggy) xarray.c links and a trivial store/load
** round-trips.  Confirms the build chain only. */

#include <stdio.h>
#include <stdlib.h>

#include <linux/bug.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/xarray.h>

extern void rcu_register_thread (void);
extern void radix_tree_init (void);

int
main (void)
{
  rcu_register_thread ();
  radix_tree_init ();

  DEFINE_XARRAY (xa);
  void *prev = xa_store (&xa, 7, xa_mk_value (42), GFP_KERNEL);
  printf ("smoke: xa_store -> %p (expected NULL)\n", prev);
  if (prev != NULL) return 1;
  void *got = xa_load (&xa, 7);
  printf ("smoke: xa_load(7) -> %p (expected xa_mk_value(42))\n", got);
  if (got != xa_mk_value (42)) return 1;
  xa_destroy (&xa);
  printf ("smoke OK\n");
  return 0;
}
