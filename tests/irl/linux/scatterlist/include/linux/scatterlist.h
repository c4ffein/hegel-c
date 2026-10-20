/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Re-export real kernel <linux/scatterlist.h> via an absolute path so
** -iquote ordering picks up our local <linux/highmem.h> etc. without
** us having to copy scatterlist.h itself. */
#ifndef _LINUX_SCATTERLIST_LOCAL_REEXPORT_H
#define _LINUX_SCATTERLIST_LOCAL_REEXPORT_H

/* Pull stdlib for malloc/free in our inline stubs.  phys_addr_t is
** defined by tools/include/linux/types.h transitively via mm.h. */
#include <stdlib.h>
#include <stdint.h>

/* Need PAGE_SIZE first. */
#include <linux/types.h>
#include <linux/mm.h>

/* Pull our local highmem stub so virt_to_page/offset_in_page/
** page_address are defined before the kernel header tries to use them
** in inline functions. */
#include <linux/highmem.h>

/* Plus types/macros the kernel header references that aren't in
** tools/include — we provide them here as no-ops or trivial defs. */
#include <stdint.h>
typedef uintptr_t dma_addr_t;

#define VM_WARN_ON_ONCE(cond) ((void)(cond))

/* page_to_phys — userspace, page IS the address. */
static inline uintptr_t page_to_phys(void *page) { return (uintptr_t)page; }

/* The kernel header references `struct folio` with .page member.
** Our folio.page must be a struct page* (not void*) for the inline
** sg_set_folio's &folio->page to type-check. */
/* Need struct page to be a complete type so &folio->page yields a
** valid struct page *.  The test never instantiates one. */
struct page { unsigned long _opaque; };
struct folio { struct page page; };

/* Used by scatterlist.c later half (pages_are_mergeable, iov_iter
** funcs) — funcs we don't exercise.  Stub the types/symbols just
** enough to compile; link will succeed because we never call them. */
#include <stdbool.h>
typedef int iov_iter_extraction_t;
struct iov_iter;

static inline unsigned long page_to_pfn(void *p)
{ return (unsigned long)p >> PAGE_SHIFT; }

static inline bool zone_device_pages_have_same_pgmap(void *a, void *b)
{ (void)a; (void)b; return true; }

static inline int page_range_contiguous(void *page, unsigned long n)
{ (void)page; (void)n; return 1; }

/* Used inside scatterlist.c sg_kmalloc for SG_MAX_SINGLE_ALLOC paths.
** Our test stays well below SG_MAX_SINGLE_ALLOC so this branch is
** unused — but the symbol must resolve. */
static inline unsigned long __get_free_page(int gfp) {
	(void)gfp;
	void *p = NULL;
	if (posix_memalign(&p, PAGE_SIZE, PAGE_SIZE) != 0) return 0;
	return (unsigned long)p;
}
static inline void free_page(unsigned long addr) { free((void *)addr); }
static inline void kmemleak_alloc(const void *p, unsigned long sz, int n, int gfp)
{ (void)p; (void)sz; (void)n; (void)gfp; }
static inline void kmemleak_free(const void *p) { (void)p; }

/* kmalloc_objs(T, n, gfp) -> kmalloc(n * sizeof(T), gfp). */
#define kmalloc_objs(T, n, gfp) ((T *)kmalloc(sizeof(T) * (n), (gfp)))

/* More stubs for late-half functions (extract_*_to_sg, etc.) — we
** don't call them, just need them to typecheck. */
static inline int is_vmalloc_or_module_addr(const void *p) { (void)p; return 0; }
static inline struct page *vmalloc_to_page(const void *p) { (void)p; return NULL; }
#define umin(a, b) ((a) < (b) ? (a) : (b))
static inline struct page *folio_page(struct folio *f, unsigned long n)
{ (void)n; return f ? &f->page : NULL; }

/* The pinned kernel's own header, copied in by the Makefile (PIN_FILES);
** quoted-relative so it cannot be shadowed by this stub. */
#include "../../pinned-include/scatterlist.h"
#endif
