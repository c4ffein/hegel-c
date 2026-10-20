/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Adapter for testing real kernel lib/glob.c in userspace.  The kernel
** source's only dependencies are linux/module.h, linux/glob.h,
** linux/export.h — all macros that we replace via glob_compat.h
** (-included by the Makefile).  Single-TU compile: we #include the
** kernel source so glob_match has external linkage in this object. */

#include <stdbool.h>

/* The kernel source's three #includes — linux/module.h, linux/glob.h,
** linux/export.h — don't all exist as plain headers under
** tools/include/linux (some are deep dependency trees that drag in
** asm/, slab.h, etc.).  We're after just glob.c's matcher logic, so
** we bypass the includes entirely: define their guards BEFORE the
** preprocessor sees `#include <linux/glob.h>`, so the include line
** does nothing.  glob_compat.h (force-included by the Makefile)
** provides bool, __pure, fallthrough, MODULE_*, EXPORT_SYMBOL. */

/* Bypass the includes by pre-defining their guards.  None of them
** define types that glob.c needs beyond what stdbool.h + glob_compat.h
** already give us. */
#define _LINUX_MODULE_H
#define _LINUX_GLOB_H
#define _LINUX_EXPORT_H

/* Forward decl that linux/glob.h would have given us. */
extern bool glob_match (char const *pat, char const *str);

#include "glob.c"
