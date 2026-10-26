/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* Just enough kernel environment to compile lib/group_cpus.c without
** CONFIG_SMP (the single-node entry point). The allocator follows kernel
** semantics, including kcalloc of zero elements returning ZERO_SIZE_PTR
** (non-NULL, not dereferenceable) rather than libc's NULL-or-unique. */
#ifndef GROUP_CPUS_COMPAT_H
#define GROUP_CPUS_COMPAT_H
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define EXPORT_SYMBOL_GPL(x)
#define EXPORT_SYMBOL(x)

#define NR_CPUS 64
struct cpumask { unsigned long bits[(NR_CPUS + 63) / 64]; };

#define ZERO_SIZE_PTR ((void *)16)
#define GFP_KERNEL 0
typedef unsigned gfp_t;

static inline void *kcalloc(size_t n, size_t sz, gfp_t f) {
  (void)f;
  if (n == 0 || sz == 0) return ZERO_SIZE_PTR;
  return calloc(n, sz);
}
static inline void kfree(const void *p) {
  if (p && p != ZERO_SIZE_PTR) free((void *)p);
}

/* one node, every CPU possible */
static struct cpumask _cpu_possible = { { ~0UL } };
#define cpu_possible_mask (&_cpu_possible)

static inline void cpumask_copy(struct cpumask *d, const struct cpumask *s) {
  memcpy(d, s, sizeof(*d));
}

#endif /* GROUP_CPUS_COMPAT_H */
