/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Smoke test: verify the snapshotted (buggy) idr.c links and basic
** idr_alloc works.  Not a PBT — just confirms the build chain. */

#include <stdio.h>
#include <stdlib.h>
#include <linux/idr.h>

extern void rcu_register_thread (void);
extern void radix_tree_init (void);

int
main (void)
{
  rcu_register_thread ();
  radix_tree_init ();

  DEFINE_IDR (idr);
  int id = idr_alloc (&idr, (void *) 0x1, 0, 0, GFP_KERNEL);
  printf ("smoke: idr_alloc -> %d (expected 0)\n", id);
  if (id != 0) return 1;
  idr_destroy (&idr);
  printf ("smoke OK\n");
  return 0;
}
