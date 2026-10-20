/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** PBT for lib/scatterlist.c — iteration / total-length property.
**
** Property: build an N-entry scatterlist over a single backing
** buffer, with each entry pointing at a (offset, length) region.
** Then iterate via for_each_sg AND sg_nents_for_len, summing
** lengths.  Assert: total length = sum of drawn lengths, sg_nents
** returns N, sg_last returns the same as the manually-walked tail.
**
** Optional second property (round-trip): use sg_pcopy_from_buffer to
** populate, sg_pcopy_to_buffer to read back, assert byte-equality.
**
** This test exercises the DMA-agnostic core of scatterlist —
** sg_init_table, sg_set_buf, sg_chain, sg_next iteration, sg_nents,
** sg_nents_for_len, sg_last, sg_copy_buffer, sg_pcopy_*. */

#include <hegel_c.h>
#include <linux/scatterlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* In our userspace-page model, a "page" IS a page-aligned virtual
** address.  Allocate a page-aligned backing buffer so virt_to_page,
** offset_in_page, page_address all agree. */
#define BACKING_BYTES (16u * 4096u)        /* 16 pages */
#define MAX_ENTRIES   16
#define MAX_TABLES    3                    /* up to 2 chains */

static uint8_t *get_backing(void)
{
	static uint8_t *buf = NULL;
	if (!buf) {
		void *p = NULL;
		if (posix_memalign (&p, 4096, BACKING_BYTES) != 0) {
			fprintf (stderr, "posix_memalign failed\n");
			exit (1);
		}
		memset (p, 0xa5, BACKING_BYTES);
		buf = p;
	}
	return buf;
}

/* Property 1: total-length invariant under iteration. */
static void prop_iter_total_length (hegel_testcase * tc)
{
	uint8_t *backing = get_backing();
	int n = hegel_draw_int (tc, 1, MAX_ENTRIES);

	/* Allocate sg table inline (no chain).  +1 for sg_mark_end slot. */
	struct scatterlist sgl[MAX_ENTRIES];
	sg_init_table (sgl, n);

	/* For each entry draw (offset_in_backing, length) constrained so
	** offset+length <= BACKING_BYTES.  Note we use uint range. */
	unsigned long total_drawn = 0;
	for (int i = 0; i < n; i++) {
		int off = hegel_draw_int (tc, 0, BACKING_BYTES - 1);
		int max_len = (BACKING_BYTES - off) > 4096 ? 4096 : (BACKING_BYTES - off);
		int len = hegel_draw_int (tc, 0, max_len);
		sg_set_buf (&sgl[i], backing + off, (unsigned)len);
		total_drawn += (unsigned)len;
	}

	/* Iterate via for_each_sg, sum lengths. */
	struct scatterlist *sg;
	int i;
	unsigned long total_iter = 0;
	int seen = 0;
	for_each_sg (sgl, sg, n, i) {
		total_iter += sg->length;
		seen++;
	}

	HEGEL_ASSERT (seen == n,
	              "for_each_sg visited %d, expected %d", seen, n);
	HEGEL_ASSERT (total_iter == total_drawn,
	              "iter total %lu vs drawn %lu", total_iter, total_drawn);

	/* sg_nents must agree with n. */
	int nents = sg_nents (sgl);
	HEGEL_ASSERT (nents == n,
	              "sg_nents returned %d, expected %d", nents, n);

	/* sg_last must point at sgl[n-1]. */
	struct scatterlist *last = sg_last (sgl, n);
	HEGEL_ASSERT (last == &sgl[n - 1],
	              "sg_last returned %p, expected %p", (void *)last, (void *)&sgl[n - 1]);

	/* sg_nents_for_len(_, X): smallest k s.t. sum lengths[0..k-1] >= X.
	** Property: result is in [1, n] for any X in [1, total_drawn], and
	** result is -EINVAL when X > total_drawn. */
	if (total_drawn > 0) {
		int k = sg_nents_for_len (sgl, total_drawn);
		HEGEL_ASSERT (k >= 1 && k <= n,
		              "sg_nents_for_len(total) = %d, want [1,%d]", k, n);
	}
	if (total_drawn < (unsigned long)BACKING_BYTES * 2) {
		int k = sg_nents_for_len (sgl, total_drawn + 1);
		HEGEL_ASSERT (k == -22 /* -EINVAL */,
		              "sg_nents_for_len(total+1) = %d, want -EINVAL", k);
	}
}

/* Property 2: round-trip via sg_pcopy_from_buffer / sg_pcopy_to_buffer.
**
** We build an SG over a 16-page backing region with non-overlapping
** regions (so the SG has no aliasing).  Then write via pcopy_from
** then read back via pcopy_to and assert byte-equality on the drawn
** payload.  This exercises sg_miter_*, kmap_local stubs, and the
** offset-arithmetic in sg_copy_buffer. */
static void prop_pcopy_round_trip (hegel_testcase * tc)
{
	uint8_t *backing = get_backing();
	memset (backing, 0, BACKING_BYTES);

	int n = hegel_draw_int (tc, 1, 8);

	/* Carve non-overlapping regions: each entry gets a 1-page slot, but
	** keep per-entry length small so total payload <= 8192 (Hypothesis
	** hard limit on list size for hegel_draw_bytes). */
	struct scatterlist sgl[MAX_ENTRIES];
	sg_init_table (sgl, n);
	unsigned long total = 0;
	for (int i = 0; i < n; i++) {
		int len = hegel_draw_int (tc, 0, 1024);
		sg_set_buf (&sgl[i], backing + i * 4096, (unsigned)len);
		total += (unsigned)len;
	}

	if (total == 0) return;            /* trivial */
	if (total > 8192) total = 8192;    /* Hypothesis caps list size */

	/* Draw a payload of length `total`. */
	uint8_t payload[8192];
	int plen = (int)total;
	int got = hegel_draw_bytes (tc, plen, plen, payload, sizeof payload);
	HEGEL_ASSERT (got == plen, "draw_bytes returned %d not %d", got, plen);

	size_t written = sg_pcopy_from_buffer (sgl, n, payload, plen, 0);
	HEGEL_ASSERT (written == (size_t)plen,
	              "pcopy_from wrote %zu, expected %d", written, plen);

	uint8_t readback[8192];
	memset (readback, 0, sizeof readback);
	size_t read = sg_pcopy_to_buffer (sgl, n, readback, plen, 0);
	HEGEL_ASSERT (read == (size_t)plen,
	              "pcopy_to read %zu, expected %d", read, plen);

	HEGEL_ASSERT (memcmp (payload, readback, plen) == 0,
	              "pcopy round-trip mismatch (plen=%d)", plen);
}

int main (void)
{
	(void)get_backing();      /* prime the static buf in parent before fork */
	hegel_run_test_n (prop_iter_total_length, 200);
	hegel_run_test_n (prop_pcopy_round_trip, 200);
	return 0;
}
