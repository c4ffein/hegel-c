/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Pre-included before lib/asn1_encoder.c is parsed.  The kernel's
** tools/include/linux/module.h is a stub that doesn't define
** EXPORT_SYMBOL, but lib/asn1_encoder.c uses EXPORT_SYMBOL_GPL on every
** function.  Pull in the real (no-op) macros from tools/include/linux/export.h
** explicitly, then let the rest of module.h be the stub. */

#ifndef _ASN1_COMPAT_H
#define _ASN1_COMPAT_H

#include <linux/export.h>

/* The shared kernel.h aliases pr_debug to printf, which floods stderr
** when running thousands of decoder cases.  Silence at the
** preprocessor level. */
#include <linux/kernel.h>
#undef  pr_debug
#define pr_debug(...) do {} while (0)

#endif /* _ASN1_COMPAT_H */
