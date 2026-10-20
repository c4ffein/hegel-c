/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Userspace stub for <linux/sort.h>.  objagg uses sort() with a
** comparator and a NULL swap fn — qsort matches semantically. */

#ifndef HEGEL_OBJAGG_SORT_SHIM_H
#define HEGEL_OBJAGG_SORT_SHIM_H

#include <stdlib.h>

static inline void sort(void *base, size_t num, size_t size,
			int (*cmp)(const void *, const void *),
			void *swap)
{
	(void)swap;
	qsort(base, num, size, cmp);
}

#endif
