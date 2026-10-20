/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/dynamic_queue_limits.c against the
** radix-tree harness's tools/include tree.  BQL needs:
**   - jiffies (we expose g_fake_jiffies, set by the test harness)
**   - time_after / time_before / time_after_eq / time_before_eq
**   - smp_rmb / smp_wmb (memory barriers — no-ops in single-thread)
**   - ____cacheline_aligned_in_smp
**   - trace_dql_stall_detected stub
**   - <linux/dynamic_queue_limits.h> compiles with __KERNEL__ defined
**
** Approach: provide everything as small helpers/macros so the kernel
** source compiles unchanged.  The shim is loaded via -include before
** dql.c is parsed. */

#ifndef HEGEL_BQL_COMPAT_H
#define HEGEL_BQL_COMPAT_H

#define __KERNEL__
#define _LGPL_SOURCE
/* So <linux/types.h> brings in the kernel-style definitions. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Provide a writable jiffies the test harness can advance. */
extern volatile unsigned long g_fake_jiffies;
#define jiffies (g_fake_jiffies)

/* Memory barriers — single-threaded test. */
#ifndef smp_rmb
# define smp_rmb() __asm__ __volatile__("" ::: "memory")
#endif
#ifndef smp_wmb
# define smp_wmb() __asm__ __volatile__("" ::: "memory")
#endif

/* Time comparison helpers (kernel uses signed delta to handle wrap). */
#define time_after(a,b)         ((long)((b) - (a)) < 0)
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      ((long)((a) - (b)) >= 0)
#define time_before_eq(a,b)     time_after_eq(b,a)

/* The kernel's __cacheline_aligned attributes are layout hints; in
** userspace we don't care.  Define them as no-ops so the struct
** definition in dynamic_queue_limits.h compiles.
**
** Defining ____cacheline_aligned_in_smp BEFORE including the header
** means we must provide a placeholder before <linux/cache.h> would be
** pulled in — the dql header includes <linux/bitops.h> and <asm/bug.h>
** but NOT cache.h directly.  cache.h is not in tools/include — so the
** symbol must just exist as a macro. */
#ifndef ____cacheline_aligned_in_smp
# define ____cacheline_aligned_in_smp
#endif
#ifndef ____cacheline_aligned
# define ____cacheline_aligned
#endif

/* trace event hook used inside dql_check_stall — no-op for tests. */
static inline void
trace_dql_stall_detected (unsigned short stall_thrs,
                          unsigned long stall_dur,
                          unsigned long last_reap,
                          unsigned long history_head,
                          unsigned long now,
                          unsigned long *history)
{
  (void) stall_thrs; (void) stall_dur; (void) last_reap;
  (void) history_head; (void) now; (void) history;
}

#endif /* HEGEL_BQL_COMPAT_H */
