/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/objagg.c against the radix-tree harness.
** Provides:
**   - kzalloc_objs / kzalloc_flex (newer kernel macros not in tools shim)
**   - sort() declaration (no tools sort.h shim exists)
**   - bitmap_zalloc / bitmap_free wrappers (objagg uses these for the
**     edges bitmap inside opt_simple_greedy)
*/

#ifndef HEGEL_OBJAGG_COMPAT_H
#define HEGEL_OBJAGG_COMPAT_H

#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/export.h>

#ifndef kzalloc_objs
#define kzalloc_objs(P, COUNT, ...) \
	((typeof(P) *)kzalloc(sizeof(P) * (COUNT), GFP_KERNEL))
#endif

#ifndef kzalloc_flex
/* kzalloc for a struct with a flexible-array member named FAM, sized for
** COUNT trailing elements. */
#define kzalloc_flex(P, FAM, COUNT, ...) \
	((typeof(P) *)kzalloc(sizeof(P) + (COUNT) * sizeof((P).FAM[0]), \
			      GFP_KERNEL))
#endif

/* tools/include/linux/bitmap.h provides single-arg bitmap_zalloc /
** bitmap_free.  objagg calls them with two args (GFP_KERNEL) — wrap. */
#define bitmap_zalloc(nbits, gfp) bitmap_zalloc((nbits))
/* bitmap_free already takes the right shape. */

/* sort() — kernel-style heapsort.  Provided by ./include/linux/sort.h. */

#endif
