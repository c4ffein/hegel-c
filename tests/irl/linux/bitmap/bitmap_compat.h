/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Compat shim for compiling lib/bitmap-str.c against the radix-tree
** harness.  bitmap-str.c pulls in <linux/hex.h>, <linux/mm.h> uses
** offset_in_page(), and references kasprintf / memdup_user_nul /
** memory_read_from_buffer that don't exist in our userspace harness.
**
** We only care about bitmap_parselist() — the list-format parser.  All
** other entry points (bitmap_parse, bitmap_parse_user, bitmap_parselist_user,
** bitmap_print_*) reference symbols we deliberately don't link.  We'd take
** a link error if they were invoked at runtime, but the test never calls them. */

#ifndef HEGEL_BITMAP_COMPAT_H
#define HEGEL_BITMAP_COMPAT_H

#include <stddef.h>
#include <stdint.h>

/* hex_to_bin lives in our local linux/hex.h shim (search path -I./include).
** offset_in_page used by bitmap_print_to_pagebuf only; never called. */

/* Declarations for kernel-only symbols used by paths we DO NOT EXERCISE
** (bitmap_parse, bitmap_parse_user, bitmap_parselist_user, bitmap_print_*).
** We only need declarations for the .c file to compile cleanly; the
** corresponding code is never invoked, so the linker is happy as long as
** dead-code-elimination drops them OR we provide stubs. We provide stubs
** below as static inlines that abort — if any unintended code path runs,
** the stub aborts loudly. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HEGEL_BITMAP_COMPAT_DECLS
#define HEGEL_BITMAP_COMPAT_DECLS

/* kfree provided by harness's _stubs.c. */
extern void kfree(const void *p);

/* memdup_user_nul: only called by *_user wrappers we don't exercise. */
static inline void *memdup_user_nul(const void *src, size_t len) {
    (void)src; (void)len;
    fprintf(stderr, "memdup_user_nul stub hit unexpectedly\n"); abort();
}

/* bitmap_parse: hex parser; declared by linux/bitmap.h normally but the
** harness's tools/include/ shim doesn't declare it. */
extern int bitmap_parse(const char *buf, unsigned int buflen,
                        unsigned long *maskp, int nmaskbits);

/* kasprintf used only by bitmap_print_to_buf — never called. */
static inline char *kasprintf_stub(int gfp, const char *fmt, ...) {
    (void)gfp; (void)fmt;
    fprintf(stderr, "kasprintf stub hit unexpectedly\n"); abort();
}
#define kasprintf kasprintf_stub

/* memory_read_from_buffer used only by bitmap_print_to_buf. */
static inline long memory_read_from_buffer(void *to, size_t count, long *ppos,
                                           const void *from, size_t available) {
    (void)to; (void)count; (void)ppos; (void)from; (void)available;
    fprintf(stderr, "memory_read_from_buffer stub hit unexpectedly\n"); abort();
}

/* strnchrnul: used by bitmap_print_to_buf — declared in real kernel
** linux/string.h.  Provide a real impl in case any DCE leftover keeps it. */
static inline char *strnchrnul(const char *s, size_t count, int c) {
    while (count-- && *s && *s != (char)c) s++;
    return (char *)s;
}

#endif /* HEGEL_BITMAP_COMPAT_DECLS */
#ifndef offset_in_page
#define offset_in_page(p)  ((unsigned long)(p) & 4095UL)
#endif

/* These are referenced by paths we don't exercise. Provide weak stubs that
** abort if ever entered; the linker still needs the symbols. */
struct page;

#endif /* HEGEL_BITMAP_COMPAT_H */
