/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Minimal userspace shim for <linux/scatterlist.h>.  kfifo.c uses
** struct scatterlist + sg_set_buf + sg_dma_address/len in
** setup_sgl_buf().  We don't exercise the dma path from hegel-c, but
** kfifo.c is one TU so we need definitions that compile.
*/
#ifndef HEGEL_KFIFO_SCATTERLIST_H
#define HEGEL_KFIFO_SCATTERLIST_H

#include <linux/types.h>
#include <linux/dma-mapping.h>

struct scatterlist {
        unsigned long page_link;
        unsigned int  offset;
        unsigned int  length;
        dma_addr_t    dma_address;
        unsigned int  dma_length;
};

static inline void sg_set_buf(struct scatterlist *sg, const void *buf,
                              unsigned int buflen)
{
        (void)sg; (void)buf; (void)buflen;
}

#define sg_dma_address(sg) ((sg)->dma_address)
#define sg_dma_len(sg)     ((sg)->dma_length)

#endif
