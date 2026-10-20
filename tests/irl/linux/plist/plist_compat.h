/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/plist.c against the radix-tree harness.
**
** plist.c does `#include <linux/bug.h>` to get WARN_ON / BUG_ON.  The
** tools/include version of <linux/bug.h> only defines BUILD_BUG_ON_ZERO;
** the real WARN_ON/WARN macros live in <asm/bug.h>.  Real kernel builds
** chain include/linux/bug.h -> asm-generic/bug.h -> arch/asm/bug.h.
** Pull <asm/bug.h> in here so WARN_ON is visible, and add likely/unlikely
** for the WARN macros to use. */

#ifndef HEGEL_PLIST_COMPAT_H
#define HEGEL_PLIST_COMPAT_H

#include <linux/compiler.h>   /* for unlikely(), needed by asm/bug.h's WARN */
#include <asm/bug.h>          /* WARN, WARN_ON */

#endif /* HEGEL_PLIST_COMPAT_H */
