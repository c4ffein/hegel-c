/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/glob.c against userspace.  glob.c is
** dependency-light: it only needs bool, __pure, fallthrough,
** MODULE_DESCRIPTION/LICENSE, and EXPORT_SYMBOL.  No kmalloc, no list,
** no rcu — so we can skip the whole radix-tree harness and just supply
** these directly. */

#ifndef HEGEL_GLOB_COMPAT_H
#define HEGEL_GLOB_COMPAT_H

#include <stdbool.h>
#include <stddef.h>     /* NULL */

/* glob.c declares: bool __pure glob_match(...).
** __pure on a forward decl + definition is just a hint — make it a no-op. */
#ifndef __pure
#define __pure
#endif

/* fallthrough: GCC ≥7 understands [[fallthrough]] / __attribute__((fallthrough)).
** Use the attribute form; it's a statement, matches kernel usage. */
#ifndef fallthrough
#define fallthrough __attribute__((fallthrough))
#endif

/* Module macros: nuked. */
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define EXPORT_SYMBOL(x)

#endif /* HEGEL_GLOB_COMPAT_H */
