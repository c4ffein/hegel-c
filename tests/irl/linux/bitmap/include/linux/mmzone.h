/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Empty shim for kernel <linux/mmzone.h>.  bitmap-str.c -> linux/mm.h ->
** mmzone.h.  Nothing in our exercised code path actually needs zones. */
#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H
#endif
