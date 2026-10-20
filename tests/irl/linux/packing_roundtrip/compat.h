/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Minimal userspace compat shim so the pinned lib/packing.c compiles
** standalone (force-included with -include). Of the kernel headers, only
** <linux/packing.h> is really parsed: the guards below pre-empt the generic
** headers it and packing.c pull in, and this file supplies what they would
** have provided. */
#ifndef PACKING_COMPAT_H
#define PACKING_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Kernel headers whose content this file replaces: mark them as already
** included so that the copies under $(LINUX)/include are skipped. */
#define _LINUX_MODULE_H
#define _LINUX_BITOPS_H
#define __LINUX_BITS_H
#define _LINUX_ERRNO_H
#define _LINUX_TYPES_H
#define _LINUX_BITREV_H
#define _LINUX_ARRAY_SIZE_H
#define _LINUX_BUILD_BUG_H
#define _LINUX_MINMAX_H
#define _LINUX_STDDEF_H

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define BITS_PER_BYTE       8
#define BITS_PER_LONG       64
#define BITS_PER_LONG_LONG  64

#define BIT(n)          (1UL << (n))
#define GENMASK(h, l)     (((~0UL)  << (l)) & (~0UL  >> (BITS_PER_LONG      - 1 - (h))))
#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))

#define min(a, b) ((a) < (b) ? (a) : (b))

#define likely(x)   (x)
#define unlikely(x) (x)

/* Kernel WARN() returns the condition; we only need that side, silently. */
#define WARN(cond, ...) (!!(cond))

#define EXPORT_SYMBOL(x)
#define MODULE_DESCRIPTION(x)

#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef ERANGE
#define ERANGE 34
#endif

static inline u8 bitrev8(u8 x)
{
	x = (u8)((x >> 4) | (x << 4));
	x = (u8)(((x & 0xCC) >> 2) | ((x & 0x33) << 2));
	x = (u8)(((x & 0xAA) >> 1) | ((x & 0x55) << 1));
	return x;
}

#endif /* PACKING_COMPAT_H */
