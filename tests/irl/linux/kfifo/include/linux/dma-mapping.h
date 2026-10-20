/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Userspace shim for <linux/dma-mapping.h>.  kfifo.c includes this for
** dma_addr_t and DMA_MAPPING_ERROR which are used in setup_sgl_buf for
** the kfifo_dma_*_prepare functions — those functions are not exercised
** by hegel-c (we only test the in-process kfifo_in/_out paths) but the
** kfifo.c TU still references the symbols, so we need a definition.
*/
#ifndef HEGEL_KFIFO_DMA_MAPPING_H
#define HEGEL_KFIFO_DMA_MAPPING_H

#include <linux/types.h>

typedef u64 dma_addr_t;

#define DMA_MAPPING_ERROR ((dma_addr_t)~0)

#endif
