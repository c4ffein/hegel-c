/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/bch.c against the radix-tree harness.
** bch.c uses EXPORT_SYMBOL_GPL (which the harness's tools/include
** module.h doesn't pull in) and the `swap` macro from
** include/linux/minmax.h (also missing from the userspace shim).
** Provide them here so the harness include path stays unchanged. */

#ifndef HEGEL_BCH_COMPAT_H
#define HEGEL_BCH_COMPAT_H

#include <linux/export.h>     /* EXPORT_SYMBOL{,_GPL} = no-op */

#ifndef swap
#define swap(a, b) \
        do { typeof(a) __hegel_tmp = (a); (a) = (b); (b) = __hegel_tmp; } while (0)
#endif

#endif /* HEGEL_BCH_COMPAT_H */
