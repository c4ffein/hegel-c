/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Minimal userspace mempool shim for compiling lib/btree.c against the
** radix-tree harness.  The real kernel mempool keeps a reserve of
** elements pre-allocated to guarantee progress under memory pressure.
** For PBT we don't need that — just delegate alloc/free to the
** caller-supplied alloc_fn/free_fn (which for btree wrap kmem_cache).
*/

#ifndef HEGEL_BTREE_MEMPOOL_H
#define HEGEL_BTREE_MEMPOOL_H

#include <linux/types.h>
#include <linux/gfp.h>
#include <stdlib.h>

typedef void *(mempool_alloc_t)(gfp_t gfp_mask, void *pool_data);
typedef void  (mempool_free_t) (void *element, void *pool_data);

typedef struct mempool {
        mempool_alloc_t *alloc;
        mempool_free_t  *free;
        void            *pool_data;
} mempool_t;

static inline mempool_t *mempool_create(int min_nr, mempool_alloc_t *alloc_fn,
                                        mempool_free_t *free_fn, void *pool_data)
{
        mempool_t *p = malloc(sizeof(*p));
        (void) min_nr;
        if (!p) return NULL;
        p->alloc = alloc_fn;
        p->free = free_fn;
        p->pool_data = pool_data;
        return p;
}

static inline void *mempool_alloc(mempool_t *pool, gfp_t gfp)
{
        return pool->alloc(gfp, pool->pool_data);
}

static inline void mempool_free(void *element, mempool_t *pool)
{
        if (!element) return;
        pool->free(element, pool->pool_data);
}

static inline void mempool_destroy(mempool_t *pool)
{
        free(pool);
}

#endif /* HEGEL_BTREE_MEMPOOL_H */
