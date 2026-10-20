/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Round-trip PBT pilot for lib/packing.c: unpack(pack(v)) == v.
** pack() and unpack() are independent code paths, so a bug in either is
** caught by the other — a real oracle, not a symmetric-bug trap. Buffer is
** heap-exact so any out-of-[0,pbuflen) access is ASAN-flagged. */
#include "hegel_c.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

extern int pack(void *pbuf, uint64_t uval, size_t startbit, size_t endbit,
                size_t pbuflen, uint8_t quirks);
extern int unpack(const void *pbuf, uint64_t *uval, size_t startbit, size_t endbit,
                  size_t pbuflen, uint8_t quirks);

/* Property: for any valid (buffer length, field position within [1,64] bits,
** quirks) and any value that fits the field, packing then unpacking is the
** identity. */
static void prop_roundtrip(hegel_testcase *tc)
{
	int pbuflen = hegel_draw_int(tc, 1, 32);
	int total_bits = 8 * pbuflen;
	int startbit = hegel_draw_int(tc, 0, total_bits - 1);

	/* Keep field width in [1,64]: endbit in [max(0,startbit-63), startbit]. */
	int lo_end = startbit - 63;
	if (lo_end < 0)
		lo_end = 0;
	int endbit = hegel_draw_int(tc, lo_end, startbit);
	int width = startbit - endbit + 1;              /* 1..64 */
	int quirks = hegel_draw_int(tc, 0, 7);          /* all 8 quirk combos */

	uint64_t fmask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
	uint64_t v = hegel_draw_u64(tc, 0, UINT64_MAX) & fmask;

	uint8_t *buf = calloc((size_t)pbuflen, 1);      /* heap-exact for ASAN */
	HEGEL_ASSERT(buf != NULL, "oom");

	int rp = pack(buf, v, (size_t)startbit, (size_t)endbit, (size_t)pbuflen,
	              (uint8_t)quirks);
	HEGEL_ASSERT(rp == 0, "pack rc=%d (start=%d end=%d len=%d quirks=%d)",
	             rp, startbit, endbit, pbuflen, quirks);

	uint64_t out = 0;
	int ru = unpack(buf, &out, (size_t)startbit, (size_t)endbit, (size_t)pbuflen,
	                (uint8_t)quirks);
	HEGEL_ASSERT(ru == 0, "unpack rc=%d (start=%d end=%d len=%d quirks=%d)",
	             ru, startbit, endbit, pbuflen, quirks);

	HEGEL_ASSERT(out == v,
	    "roundtrip mismatch: v=0x%llx out=0x%llx start=%d end=%d width=%d len=%d quirks=%d",
	    (unsigned long long)v, (unsigned long long)out,
	    startbit, endbit, width, pbuflen, quirks);

	free(buf);
}

int main(void)
{
	printf("packing pack/unpack round-trip PBT...\n");
	int rc = hegel_run_test_result_n(prop_roundtrip, 20000);
	if (rc == 0)
		printf("PASSED\n");
	return rc;
}
