/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Builds the DMA-agnostic first half of the pinned lib/scatterlist.c
** (everything up to sg_zero_buffer's EXPORT_SYMBOL: sg_nents*, sg_last,
** sg_init_*, sg_{alloc,free}_*table*, sgl_alloc/free, the page iterators,
** sg_miter_*, sg_{p,}copy_*, sg_zero_buffer) for userspace PBT.
**
** Why not the whole file: its bottom half (extract_*_to_sg) works on real
** iov_iters and pinned user pages and pulls in dozens of unstubbed kernel
** types and helpers (struct iov_iter, bio_vec, folio_queue, xa_state,
** iov_iter_extract_pages, vmalloc_to_page, xas_for_each, ...).  Stubbing
** all of it would exercise nothing we test.  So the Makefile extracts the
** head of the pinned file, verbatim, cut at
**   EXPORT_SYMBOL(sg_zero_buffer);
** (a marker, not a line number, so it holds across kernel versions) into
** scatterlist_head_pinned.c, which is included below. */

#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <stdlib.h>
#include <string.h>

/* slab stubs.  The radix-tree harness's slab.o is built with ASAN; pulling
** it in would force ASAN on these binaries too.  The tests never reach
** nents == SG_MAX_SINGLE_ALLOC, so the page-allocator branch is dead. */
#ifndef ___GFP_ZERO_BIT
#  define ___GFP_ZERO_BIT 8
#endif
void *kmalloc(size_t sz, gfp_t gfp) {
	void *p = malloc(sz);
	if (p && ((unsigned)gfp & (1u << ___GFP_ZERO_BIT))) memset(p, 0, sz);
	return p;
}
void *kmalloc_array(size_t n, size_t sz, gfp_t gfp) {
	if (sz && n > (size_t)-1 / sz) return NULL;
	return kmalloc(n * sz, gfp);
}
void *kcalloc(size_t n, size_t sz, gfp_t gfp) {
	(void)gfp;
	return calloc(n, sz);
}
void kfree(void *p) { free(p); }

#include "scatterlist_head_pinned.c"
