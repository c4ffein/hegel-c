/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Userspace shim around the pinned kernel's xarray.c.  Mirrors
** tools/testing/shared/xarray-shared.c but points at our local
** copy (xarray_pinned.c) instead of the in-tree lib/xarray.c. */

#include "xarray-shared.h"

#include "xarray_pinned.c"
