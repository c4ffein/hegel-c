/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Userspace shim for compiling the pinned kernel's decompress_bunzip2.c
** as a self-contained pre-boot decompressor.  We bypass the kernel's
** include/linux/decompress/mm.h (which defines its own static malloc/free
** that clash with stdlib.h's) by pre-defining its include guard, then
** providing stand-in INIT/STATIC/malloc/free macros that route to libc. */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Skip <linux/decompress/mm.h>'s body entirely. */
#define DECOMPR_MM_H

/* Symbols decompress_bunzip2.c expects after that include. */
#define INIT
/* Re-export the top-level entry point so test_smoke can call it. */
#define STATIC
#define large_malloc(sz)  malloc((size_t) (sz))
#define large_free(p)     free((p))

#include "decompress_bunzip2_pinned.c"
