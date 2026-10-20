/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
#ifndef _LINUX_FOLIO_QUEUE_LOCAL_STUB_H
#define _LINUX_FOLIO_QUEUE_LOCAL_STUB_H

#include <linux/types.h>

struct folio;
struct folio_queue {
	struct folio_queue *next;
	struct folio_queue *prev;
};

static inline struct folio *folioq_folio(const struct folio_queue *fq, unsigned int s)
{ (void)fq; (void)s; return NULL; }
static inline size_t folioq_folio_size(const struct folio_queue *fq, unsigned int s)
{ (void)fq; (void)s; return 0; }
static inline unsigned int folioq_nr_slots(const struct folio_queue *fq)
{ (void)fq; return 0; }

#endif
