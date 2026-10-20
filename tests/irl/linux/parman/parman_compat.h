/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/parman.c against the radix-tree harness.
** parman.c uses list_for_each_entry_from_reverse, which exists in the
** real kernel include/linux/list.h but NOT in tools/include/linux/list.h
** (the userspace harness shim).  Define it here so the harness include
** path can be reused unchanged. */

#ifndef HEGEL_PARMAN_COMPAT_H
#define HEGEL_PARMAN_COMPAT_H

#include <linux/list.h>

/* Bring list_is_head / list_entry_is_head from the real kernel include —
** the tools/include/linux/list.h shim predates these macros. */
#ifndef list_is_head
static inline int list_is_head(const struct list_head *list,
                               const struct list_head *head) {
        return list == head;
}
#endif

#ifndef list_entry_is_head
#define list_entry_is_head(pos, head, member)                           \
        list_is_head(&pos->member, (head))
#endif

#ifndef list_for_each_entry_from_reverse
#define list_for_each_entry_from_reverse(pos, head, member)             \
        for (; !list_entry_is_head(pos, head, member);                  \
             pos = list_prev_entry(pos, member))
#endif

#endif /* HEGEL_PARMAN_COMPAT_H */
