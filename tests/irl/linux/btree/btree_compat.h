/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/btree.c against the radix-tree harness.
** btree.c uses MAX() (from include/linux/minmax.h in the real kernel),
** kmem_cache_destroy() (not in the harness's linux.c), and references
** mempool_t (provided locally — see include/linux/mempool.h).
**
** This file is -include'd before btree.c is parsed (see Makefile). */

#ifndef HEGEL_BTREE_COMPAT_H
#define HEGEL_BTREE_COMPAT_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/export.h>     /* EXPORT_SYMBOL_GPL — defined empty */
#include <stdlib.h>

/* btree.c does: #define NODESIZE MAX(L1_CACHE_BYTES, 128) */
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* __init and __exit section attributes don't apply in userspace — strip. */
#ifndef __init
#define __init
#endif
#ifndef __exit
#define __exit
#endif

/* kmem_cache_destroy: free the cache descriptor.
**
** Friction note: the harness's linux.c parks up to 10 freed objects in
** cachep->objs (chained through the radix_tree_node.parent field —
** offset ~4 bytes into the freed object).  Btree nodes don't have
** that layout (they're flat unsigned-long arrays), so the chain is
** implicit and not safe to walk from a generic destroy.  Result: a
** ~5-object leak (640 bytes) at process exit per cache that ASAN
** reports.  Not a btree.c bug; harness sloppiness.  Could be fixed by
** caching kmem_cache_create call-sites + draining via the objp's
** reinterpretation, or by switching the harness to a free()-everything
** policy when nr_objs <= 10 (defeats the cache, but irrelevant for PBT).
**
** For now we just free the cache descriptor and accept the small leak. */
static inline void kmem_cache_destroy(struct kmem_cache *s)
{
        free(s);
}

/* shared.h's module_init/module_exit are no-ops, but btree.c's static
** btree_module_init() is what creates btree_cachep — without it
** btree_alloc() crashes.  Override module_init to install the function
** as a __constructor__ so it runs before main() and sets up the cache. */
#undef module_init
#undef module_exit
#define module_init(fn) \
        __attribute__((constructor)) static void hegel_init_##fn(void) { fn(); }
#define module_exit(fn) \
        __attribute__((destructor))  static void hegel_exit_##fn(void) { fn(); }

#endif /* HEGEL_BTREE_COMPAT_H */
