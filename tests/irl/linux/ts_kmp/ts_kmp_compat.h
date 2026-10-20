/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/ts_kmp.c against the radix-tree harness.
** Provides what tools/include/ doesn't: THIS_MODULE, ERR_PTR family
** (already in linux/err.h), and tweaks ts_kmp's static kmp_ops symbol
** so we can reach it externally. */

#ifndef HEGEL_TS_KMP_COMPAT_H
#define HEGEL_TS_KMP_COMPAT_H

/* THIS_MODULE: ts_kmp uses it as the .owner field of struct ts_ops.
** Userspace doesn't have modules — define as NULL. */
#ifndef THIS_MODULE
#define THIS_MODULE ((struct module *)0)
#endif

/* __init / __exit / __read_mostly: these section attributes have no
** meaning in userspace — define them away. */
#ifndef __init
#define __init
#endif
#ifndef __exit
#define __exit
#endif
#ifndef __read_mostly
#define __read_mostly
#endif

/* textsearch_register / textsearch_unregister are declared extern in
** linux/textsearch.h but live in lib/textsearch.c, which we don't
** compile.  ts_kmp.c calls them from init_kmp/exit_kmp.  Provide
** trivial stubs. */
struct ts_ops;
static inline int textsearch_register_stub(struct ts_ops *o)   { (void)o; return 0; }
static inline int textsearch_unregister_stub(struct ts_ops *o) { (void)o; return 0; }
#define textsearch_register   textsearch_register_stub
#define textsearch_unregister textsearch_unregister_stub

/* tools/include/linux/list.h has LIST_HEAD_INIT but ts_kmp.c also pulls
** linux/textsearch.h which references list_for_each_entry / list_add_tail
** — already in tools/include/linux/list.h. */

/* Drop "static" on kmp_ops so we can call its methods from outside the
** translation unit (the file under test is GPL'd kernel code we don't
** want to patch — preprocessor bypass instead). */

#endif /* HEGEL_TS_KMP_COMPAT_H */
