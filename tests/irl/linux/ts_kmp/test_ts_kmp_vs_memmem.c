/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Differential property test:
**   For random (haystack, needle) byte pairs, the offset returned by
**   ts_kmp_search() (kernel KMP) must match the offset returned by
**   glibc's memmem() — or both must report "not found".
**
** Tests both random-byte input and biased-alphabet input to surface
** different KMP failure-table edge cases.  The biased-alphabet test
** uses a 2-symbol alphabet ('a','b'), which heavily exercises the
** prefix-table fallback (long internal repetitions). */

#define _GNU_SOURCE     /* memmem */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "hegel_c.h"

extern unsigned int ts_kmp_search(const void *pattern, unsigned int plen,
                                  const void *data, unsigned int dlen);

#define MAX_HAY 1024
#define MAX_NDL  32

/* Set to 1 to inject a deliberate bug (off-by-one) — sanity-check that
** the property fires and shrinks. */
#define HEGEL_TS_KMP_INJECT_BUG 0

static unsigned int oracle_memmem(const unsigned char *hay, size_t hlen,
                                  const unsigned char *ndl, size_t nlen)
{
	if (nlen == 0) return 0;            /* empty needle: convention 0 */
	if (nlen > hlen) return UINT_MAX;
	const unsigned char *p = memmem(hay, hlen, ndl, nlen);
	if (!p) return UINT_MAX;
	return (unsigned int)(p - hay);
}

static void run_one(hegel_testcase *tc, int alphabet_lo, int alphabet_hi)
{
	unsigned char hay[MAX_HAY];
	unsigned char ndl[MAX_NDL];

	int hlen = hegel_draw_int(tc, 1, MAX_HAY);
	int nlen = hegel_draw_int(tc, 1, MAX_NDL);

	/* Per-byte draw — see FRICTION NOTES.  This is the awkward part. */
	for (int i = 0; i < hlen; i++)
		hay[i] = (unsigned char)hegel_draw_int(tc, alphabet_lo, alphabet_hi);
	for (int i = 0; i < nlen; i++)
		ndl[i] = (unsigned char)hegel_draw_int(tc, alphabet_lo, alphabet_hi);

	unsigned int kmp_off = ts_kmp_search(ndl, nlen, hay, hlen);
	unsigned int mem_off = oracle_memmem(hay, hlen, ndl, nlen);

#if HEGEL_TS_KMP_INJECT_BUG
	/* Off-by-one: pretend kmp returned 1 less.  Should fire on the
	** very first matching case. */
	if (kmp_off != UINT_MAX) kmp_off -= 1;
#endif

	if (kmp_off != mem_off) {
		char buf[256];
		snprintf(buf, sizeof(buf),
		    "kmp_off=%u mem_off=%u hlen=%d nlen=%d "
		    "hay[0..min(8,h)]=%02x%02x%02x%02x ndl[0..min(8,n)]=%02x%02x%02x%02x",
		    kmp_off, mem_off, hlen, nlen,
		    hlen > 0 ? hay[0] : 0, hlen > 1 ? hay[1] : 0,
		    hlen > 2 ? hay[2] : 0, hlen > 3 ? hay[3] : 0,
		    nlen > 0 ? ndl[0] : 0, nlen > 1 ? ndl[1] : 0,
		    nlen > 2 ? ndl[2] : 0, nlen > 3 ? ndl[3] : 0);
		hegel_fail(buf);
	}
}

static void test_random_bytes(hegel_testcase *tc)
{
	run_one(tc, 0, 255);
}

static void test_biased_ab(hegel_testcase *tc)
{
	/* 'a' = 0x61, 'b' = 0x62 — 2-symbol alphabet stresses
	** KMP failure-table fallback. */
	run_one(tc, 0x61, 0x62);
}

static void test_biased_3sym(hegel_testcase *tc)
{
	/* 3-symbol alphabet, including 0x00 — KMP must handle binary. */
	int alpha[] = {0x00, 0x61, 0x62};
	int i = hegel_draw_int(tc, 0, 2);
	(void)alpha; (void)i;
	/* Use a different mechanism: draw 0..2 then map.  This exposes
	** more friction (no draw-from-set). */
	unsigned char hay[MAX_HAY];
	unsigned char ndl[MAX_NDL];
	int hlen = hegel_draw_int(tc, 1, 64);
	int nlen = hegel_draw_int(tc, 1, 8);
	for (int j = 0; j < hlen; j++)
		hay[j] = (unsigned char)alpha[hegel_draw_int(tc, 0, 2)];
	for (int j = 0; j < nlen; j++)
		ndl[j] = (unsigned char)alpha[hegel_draw_int(tc, 0, 2)];
	unsigned int kmp_off = ts_kmp_search(ndl, nlen, hay, hlen);
	unsigned int mem_off = oracle_memmem(hay, hlen, ndl, nlen);
	if (kmp_off != mem_off) {
		char buf[256];
		snprintf(buf, sizeof(buf),
		    "BIASED_3SYM mismatch: kmp=%u mem=%u hlen=%d nlen=%d",
		    kmp_off, mem_off, hlen, nlen);
		hegel_fail(buf);
	}
}

int main(void)
{
	hegel_suite *s = hegel_suite_new();
	hegel_suite_add(s, "ts_kmp_vs_memmem_random_bytes", test_random_bytes);
	hegel_suite_add(s, "ts_kmp_vs_memmem_biased_ab",    test_biased_ab);
	hegel_suite_add(s, "ts_kmp_vs_memmem_3sym_bin",     test_biased_3sym);
	int rc = hegel_suite_run(s);
	hegel_suite_free(s);
	return rc;
}
