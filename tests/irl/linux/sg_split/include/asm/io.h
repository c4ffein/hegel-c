/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** <linux/scatterlist.h> includes <asm/io.h>.  None of its MMIO accessors
** are used here, and older kernels' tools/include has no userspace version
** of it; what the scatterlist code does get through it (transitively in
** newer trees) is limits, errno values and the kernel.h helpers. */
#ifndef _HEGEL_STUB_ASM_IO_H
#define _HEGEL_STUB_ASM_IO_H
#include <limits.h>
#include <errno.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/kernel.h>
#endif
