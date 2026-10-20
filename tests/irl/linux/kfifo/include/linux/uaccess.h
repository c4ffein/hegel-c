/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Userspace shim for <linux/uaccess.h>.  kfifo.c uses copy_from_user /
** copy_to_user inside the *_from_user / *_to_user variants.  We don't
** exercise those from the hegel-c test, but kfifo.c is compiled as one
** TU so we need definitions — make them straight memcpy in userspace
** (they cannot fail here).
*/
#ifndef HEGEL_KFIFO_UACCESS_H
#define HEGEL_KFIFO_UACCESS_H

#include <string.h>

#ifndef __user
#define __user
#endif

static inline unsigned long
copy_from_user(void *to, const void __user *from, unsigned long n)
{
        memcpy(to, (const void *)from, n);
        return 0;
}

static inline unsigned long
copy_to_user(void __user *to, const void *from, unsigned long n)
{
        memcpy((void *)to, from, n);
        return 0;
}

#endif
