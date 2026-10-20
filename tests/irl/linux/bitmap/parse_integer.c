/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Userspace impl of _parse_integer — the only kstrtox symbol bitmap-str.c
** references in the bitmap_parselist code path.  Compiling lib/kstrtox.c
** directly drags in linux/uaccess.h and friends; rolling our own avoids
** the rabbit hole. */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define KSTRTOX_OVERFLOW (1U << 31)

/* Copied verbatim from lib/ctype.c. _ctype is referenced by isspace() etc.
** via the linux/ctype.h header.  Tiny — keep it inline rather than build
** the whole ctype.c through the harness. */
#define _U  0x01
#define _L  0x02
#define _D  0x04
#define _C  0x08
#define _P  0x10
#define _S  0x20
#define _X  0x40
#define _SP 0x80
const unsigned char _ctype[] = {
_C,_C,_C,_C,_C,_C,_C,_C,
_C,_C|_S,_C|_S,_C|_S,_C|_S,_C|_S,_C,_C,
_C,_C,_C,_C,_C,_C,_C,_C,
_C,_C,_C,_C,_C,_C,_C,_C,
_S|_SP,_P,_P,_P,_P,_P,_P,_P,
_P,_P,_P,_P,_P,_P,_P,_P,
_D,_D,_D,_D,_D,_D,_D,_D,
_D,_D,_P,_P,_P,_P,_P,_P,
_P,_U|_X,_U|_X,_U|_X,_U|_X,_U|_X,_U|_X,_U,
_U,_U,_U,_U,_U,_U,_U,_U,
_U,_U,_U,_U,_U,_U,_U,_U,
_U,_U,_U,_P,_P,_P,_P,_P,
_P,_L|_X,_L|_X,_L|_X,_L|_X,_L|_X,_L|_X,_L,
_L,_L,_L,_L,_L,_L,_L,_L,
_L,_L,_L,_L,_L,_L,_L,_L,
_L,_L,_L,_P,_P,_P,_P,_C,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
_S|_SP,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,
_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,
_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,
_U,_U,_U,_U,_U,_U,_U,_P,_U,_U,_U,_U,_U,_U,_U,_L,
_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,
_L,_L,_L,_L,_L,_L,_L,_P,_L,_L,_L,_L,_L,_L,_L,_L};

/* scnprintf only referenced by bitmap_print_to_pagebuf which we don't call.
** Provide a libc-vsnprintf-backed stub that errors loudly if used (since
** the kernel's "%*pbl" format extension isn't supported by libc anyway). */
int scnprintf(char *buf, size_t size, const char *fmt, ...) {
    (void)buf; (void)size; (void)fmt;
    fprintf(stderr, "scnprintf stub hit unexpectedly — this build only "
                    "exercises bitmap_parselist\n");
    abort();
}

unsigned int _parse_integer(const char *s, unsigned int base,
                            unsigned long long *p) {
    unsigned long long res = 0;
    unsigned int rv = 0;
    unsigned int n = 0;

    while (n < INT_MAX) {
        unsigned char c = (unsigned char)s[n];
        unsigned int val;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else break;
        if (val >= base) break;
        if (res > (unsigned long long)-1 / base ||
            res * base > (unsigned long long)-1 - val) {
            rv |= KSTRTOX_OVERFLOW;
        }
        res = res * base + val;
        n++;
    }
    *p = res;
    return rv | n;
}
