/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Tiny stub layer for bch.c.  bch.c references bitrev8 only on the
** swap_bits=true path, and bitrev8 expands to a lookup into
** byte_rev_table[].  We never set swap_bits=true, so the table is
** never read, but the linker still needs the symbol since bitrev.h
** declares it extern.  Provide an all-zero placeholder. */

#include <linux/types.h>

const u8 byte_rev_table[256] = { 0 };
