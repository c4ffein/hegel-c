/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Userspace stub for <linux/highmem.h>.  Scatterlist's sg_miter_*
** funcs call kmap_local_page / kmap_atomic / kmap and friends.  We
** model "page" as a void* pointing directly to the data, so map = id.
** That gives correct semantics for the functions we test
** (sg_init_table, sg_chain, sg_set_buf, sg_nents, sg_nents_for_len,
** sg_last, sg_copy_buffer if we keep buffers <= PAGE_SIZE and
** page-aligned).  Pages and "kernel virtual addresses" are
** indistinguishable in this harness. */

#ifndef _LINUX_HIGHMEM_LOCAL_STUB_H
#define _LINUX_HIGHMEM_LOCAL_STUB_H

#include <linux/types.h>

/* In tools/include/linux/mm.h, PAGE_SIZE/PAGE_MASK/PAGE_SHIFT are defined.
** offset_in_page is sometimes provided by linux/mm.h too — we redefine
** here only if it isn't.  page_address / virt_to_page treat addresses
** as their own page since we mmap page-aligned regions. */

#ifndef offset_in_page
#define offset_in_page(p) ((unsigned long)(p) & (PAGE_SIZE - 1))
#endif

static inline void *page_address(const void *page) { return (void *)page; }
static inline void *kmap(void *page)                { return page_address(page); }
static inline void  kunmap(void *page)              { (void)page; }
static inline void *kmap_atomic(void *page)         { return page_address(page); }
static inline void  kunmap_atomic(void *addr)       { (void)addr; }
static inline void *kmap_local_page(void *page)     { return page_address(page); }
static inline void  kunmap_local(void *addr)        { (void)addr; }
static inline void  flush_dcache_page(void *page)   { (void)page; }
static inline int   pagefault_disabled(void)        { return 1; }

/* virt_to_page rounds down to page boundary.  Caller's offset is
** then offset_in_page(buf). */
static inline void *virt_to_page(const void *addr)
{
	return (void *)((unsigned long)addr & ~(PAGE_SIZE - 1));
}

#endif
