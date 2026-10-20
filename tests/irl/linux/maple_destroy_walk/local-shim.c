/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Userspace shim around maple_tree.c.
** Mirrors tools/testing/shared/maple-shim.c but includes the local copy
** of the pinned kernel's lib/maple_tree.c (see ../pin.mk). */

#include "maple-shared.h"
#include <linux/slab.h>

#include "maple_tree_pinned.c"   /* lib/maple_tree.c of the pinned kernel (generated) */

void maple_rcu_cb(struct rcu_head *head) {
  struct maple_node *node = container_of(head, struct maple_node, rcu);
  kmem_cache_free(maple_node_cache, node);
}
