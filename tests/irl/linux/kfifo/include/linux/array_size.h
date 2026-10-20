/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Local copy of <linux/array_size.h> (1:1 with the kernel header).
** kfifo.h includes it but it's absent from tools/include/linux/.
*/
#ifndef HEGEL_KFIFO_ARRAY_SIZE_H
#define HEGEL_KFIFO_ARRAY_SIZE_H

#include <linux/compiler.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#endif

#endif
