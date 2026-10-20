/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/lzo source files against the radix-tree harness in
** userspace.  Three issues to bridge:
**
**   1. glibc's <endian.h> defines BOTH __BIG_ENDIAN and __LITTLE_ENDIAN as
**      numeric values; lzodefs.h treats that as "conflicting endian
**      definitions" and #errors.  Undef them before lzodefs is parsed and
**      provide the kernel-style markers via CONFIG_X86_64.
**   2. lzodefs.h uses CONFIG_X86_64 / CONFIG_ARM64 / CONFIG_X86 / CONFIG_PPC
**      / CONFIG_ARM to select fast-path codegen.  We define CONFIG_X86_64
**      since this is x86_64 userspace.
**   3. lzo1x_compress.c uses ALIGN / IS_ALIGNED (from <linux/align.h>) and
**      EXPORT_SYMBOL_GPL (a no-op in userspace).  Pull those in / stub them.
*/

#ifndef HEGEL_LZO_COMPAT_H
#define HEGEL_LZO_COMPAT_H

/* Glibc's <endian.h> defines BOTH __BIG_ENDIAN and __LITTLE_ENDIAN as numeric
** ids regardless of host endianness, which trips lzodefs.h's
**   #if defined(__BIG_ENDIAN) && defined(__LITTLE_ENDIAN)
** check.  But lzo1x_compress.c then *needs* __LITTLE_ENDIAN defined to
** enable the ctz/clz fast path.  So: drop __BIG_ENDIAN only.  This is x86_64
** userspace, host is little-endian. */
#ifdef __BIG_ENDIAN
#undef __BIG_ENDIAN
#endif
#if !defined(__LITTLE_ENDIAN)
#define __LITTLE_ENDIAN 1234
#endif

/* Tell lzodefs.h we're on x86_64 so it picks the 64-bit fast path. */
#ifndef CONFIG_X86_64
#define CONFIG_X86_64 1
#endif

/* lzo1x_compress.c reaches for ALIGN / IS_ALIGNED but the kernel C file
** doesn't include linux/align.h directly (linux/kernel.h pulls it in the
** real tree, but the tools/ shim doesn't).  Pull it explicitly. */
#include <linux/align.h>

/* lzo source files invoke EXPORT_SYMBOL_GPL — no-op outside the kernel. */
#ifndef EXPORT_SYMBOL_GPL
#define EXPORT_SYMBOL_GPL(x)
#endif
#ifndef EXPORT_SYMBOL
#define EXPORT_SYMBOL(x)
#endif

/* ARRAY_SIZE — used in compress paths via macros that may need it.
** kernel.h shim already defines this; safe-guard anyway. */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#endif /* HEGEL_LZO_COMPAT_H */
