/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/llist.c against the radix-tree harness
** in single-threaded mode.
**
** llist.c uses three atomic primitives that aren't defined in
** tools/include: try_cmpxchg(ptr, old_p, new), xchg(ptr, new), and
** smp_load_acquire(p) (smp_load_acquire IS defined; we leave it).
**
** SINGLE-THREADED MODEL: we deliberately model llist sequentially.  The
** cmpxchg loops in the real code never spin (no concurrent updaters
** under test), so trivial pointer-store implementations are SOUND for
** what we test (LIFO order, list traversal, del_all snapshot, empty
** semantics).  We are NOT testing concurrency / memory ordering.
*/

#ifndef HEGEL_LLIST_COMPAT_H
#define HEGEL_LLIST_COMPAT_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/compiler.h>   /* READ_ONCE / WRITE_ONCE */
#include <asm/barrier.h>      /* smp_load_acquire */
#include <asm/bug.h>          /* WARN, WARN_ON */

/* Single-thread try_cmpxchg: compare *ptr to *old_p; if equal, store new
** and return true.  If different, write the current *ptr value back into
** *old_p (matches the real semantics callers depend on) and return false.
**
** In our single-threaded model the comparison always succeeds — but we
** keep the full semantics in case any future stub deviates. */
#ifndef try_cmpxchg
#define try_cmpxchg(ptr, old_p, new)                            \
	({                                                      \
		typeof(*(ptr)) __cur  = *(ptr);                 \
		typeof(*(ptr)) __want = *(old_p);               \
		bool __ok = (__cur == __want);                  \
		if (__ok)                                       \
			*(ptr) = (new);                         \
		else                                            \
			*(old_p) = __cur;                       \
		__ok;                                           \
	})
#endif

/* xchg: atomically swap *ptr with new, returning old value.
** Single-threaded: just a load-then-store. */
#ifndef xchg
#define xchg(ptr, new)                                          \
	({                                                      \
		typeof(*(ptr)) __old = *(ptr);                  \
		*(ptr) = (new);                                 \
		__old;                                          \
	})
#endif

#endif /* HEGEL_LLIST_COMPAT_H */
