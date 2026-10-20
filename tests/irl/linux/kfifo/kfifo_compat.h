/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/kfifo.c against the radix-tree harness.
** kfifo.c uses a few helpers absent from tools/include/ — wire them up
** here so the include path doesn't need to be widened (avoiding the
** "vdso/limits.h" conflict from including the full kernel include/).
*/
#ifndef HEGEL_KFIFO_COMPAT_H
#define HEGEL_KFIFO_COMPAT_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>

/* min3() — used by __kfifo_out_linear; absent from tools/include. */
#ifndef min3
#define min3(x, y, z) min((typeof(x))min(x, y), z)
#endif

/* NUMA_NO_NODE — used by kfifo.h's static-inline __kfifo_alloc.  In the
** real kernel this comes from <linux/numa.h>; tools/include doesn't
** define it. */
#ifndef NUMA_NO_NODE
#define NUMA_NO_NODE (-1)
#endif

/* kmalloc_array_node() — kfifo.c calls this in __kfifo_alloc_node.
** The harness's slab.h declares kmalloc/kfree, so we just route through. */
static inline void *
kmalloc_array_node(size_t n, size_t size, gfp_t gfp, int node)
{
        (void)node;
        return kmalloc(n * size, gfp);
}

#endif /* HEGEL_KFIFO_COMPAT_H */
