/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
#ifndef _LINUX_UIO_LOCAL_STUB_H
#define _LINUX_UIO_LOCAL_STUB_H

/* Userspace stubs for the iov_iter / bio_vec / kvec types.  The
** scatterlist.c functions extract_user_to_sg / extract_bvec_to_sg /
** extract_kvec_to_sg / extract_folioq_to_sg / extract_iter_to_sg
** dereference these.  We never call those funcs from our PBT, so
** the bodies just need to typecheck; the linker drops them or our
** unused-fn warnings are silenced. */

#include <stddef.h>
#include <linux/types.h>

struct page;
struct folio_queue;

struct bio_vec {
	struct page *bv_page;
	unsigned int bv_len;
	unsigned int bv_offset;
};

struct kvec {
	void *iov_base;
	size_t iov_len;
};

struct iov_iter {
	int iter_type;
	unsigned int data_source;
	size_t iov_offset;
	size_t count;
	union {
		const struct bio_vec *bvec;
		const struct kvec *kvec;
		const struct folio_queue *folioq;
	};
	unsigned long nr_segs;
	loff_t folioq_slot;
};

typedef int iov_iter_extraction_t;

/* Predeclare external helpers — not called from our test path. */
extern ssize_t iov_iter_extract_pages(struct iov_iter *, struct page ***,
				      size_t, unsigned, iov_iter_extraction_t,
				      size_t *);
extern void iov_iter_advance(struct iov_iter *, size_t);
extern void iov_iter_revert(struct iov_iter *, size_t);

extern void unpin_user_page(struct page *);
extern void unpin_user_pages(struct page **, unsigned long);

#endif
