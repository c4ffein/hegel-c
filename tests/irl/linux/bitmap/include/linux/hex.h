/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Tiny shim for kernel <linux/hex.h>.  bitmap-str.c includes this header,
** but the userspace tools/include/ path doesn't have it.  Only hex_to_bin
** is referenced (by bitmap_parse(), which our test never calls). */

#ifndef _LINUX_HEX_H
#define _LINUX_HEX_H

static inline int hex_to_bin(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

#endif
