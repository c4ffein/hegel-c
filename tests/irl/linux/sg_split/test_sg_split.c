/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Property-based test for lib/sg_split.c (snapshot in sg_split_buggy.c).
**
** Strategy:
**   1. Allocate a page-aligned backing region of MAX_PAGES * 4096 bytes.
**      Fill it with a deterministic, position-unique byte pattern so
**      every backing byte is identifiable.
**   2. Build an N-entry input scatterlist over slices of the backing
**      region.  Each entry has a chosen (offset_in_page, length) and
**      points at a chosen page.  Build a flat oracle buffer that is
**      the byte-by-byte concatenation of the input SG's view.
**   3. Pick `skip` and `nb_splits`, then sizes such that
**      skip + sum(sizes) <= total_len.
**   4. Call sg_split(...).
**   5. For each output split k, walk every byte of every output SG
**      entry (page+offset+length) and compare against
**      oracle[skip + sum(sizes[0..k-1]) + within_split_byte_index].
**      This is the ONLY oracle: "the i-th byte of the output equals
**      the (skip+i)-th byte of the input."
**   6. Verify input scatterlist entries unchanged after the call.
**
** We bias generators toward inputs where skip lands mid-entry and where
** individual sizes[k] cross multiple input entries — the cases where
** offset and partial-entry handling diverge. */

#include <hegel_c.h>
#include <linux/scatterlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each "page" in our model is a page-aligned virtual address.
** Allocate MAX_PAGES distinct pages for the backing region. */
#define MAX_PAGES     8
#define MAX_ENTRIES   6
#define MAX_SPLITS    4
#define BACKING_BYTES (MAX_PAGES * 4096u)

/* Position-unique byte: encode (page_idx, off_in_page) into one byte
** by combining the two with a hash.  Since both ranges are 0..255 and
** 0..4095, we use ((page << 4) ^ off) & 0xff which gives a reasonably
** mixed but deterministic byte. */
static inline uint8_t pattern_byte(int page_idx, unsigned off)
{
	return (uint8_t)(((page_idx * 31u) + off * 17u + 0x5a) & 0xffu);
}

static uint8_t *get_backing(void)
{
	static uint8_t *buf = NULL;
	if (!buf) {
		void *p = NULL;
		if (posix_memalign (&p, 4096, BACKING_BYTES) != 0) {
			fprintf (stderr, "posix_memalign failed\n");
			exit (1);
		}
		uint8_t *b = (uint8_t *)p;
		for (unsigned page = 0; page < MAX_PAGES; page++) {
			for (unsigned off = 0; off < 4096u; off++) {
				b[page * 4096u + off] = pattern_byte((int)page, off);
			}
		}
		buf = b;
	}
	return buf;
}

/* Forward declaration — sg_split_buggy.c provides the symbol. */
int sg_split (struct scatterlist *in, const int in_mapped_nents,
              const off_t skip, const int nb_splits,
              const size_t *split_sizes,
              struct scatterlist **out, int *out_mapped_nents,
              gfp_t gfp_mask);

/* Walk every byte of every output SG entry in `out_sg` (an array of
** `nents` scatterlist entries) and append to `dst`.  Returns total bytes
** written. */
static size_t copy_sg_bytes (struct scatterlist *out_sg, int nents, uint8_t *dst)
{
	size_t total = 0;
	for (int j = 0; j < nents; j++) {
		struct scatterlist *e = &out_sg[j];
		struct page *pg = sg_page (e);
		uint8_t *base = (uint8_t *)page_address (pg) + e->offset;
		memcpy (dst + total, base, e->length);
		total += e->length;
	}
	return total;
}

/* Property: round-trip via oracle. */
static void prop_sg_split_oracle (hegel_testcase * tc)
{
	uint8_t *backing = get_backing ();

	/* Draw N input entries. */
	int n = hegel_draw_int (tc, 1, MAX_ENTRIES);

	struct scatterlist in[MAX_ENTRIES + 1];
	sg_init_table (in, n);

	/* For each input entry: pick a page (0..MAX_PAGES-1), an offset
	** within the page, and a length such that off + len <= 4096.  We
	** allow len = 0 too — sg_split treats that as a zero-length entry. */
	unsigned int entry_off[MAX_ENTRIES];
	unsigned int entry_len[MAX_ENTRIES];
	int entry_page[MAX_ENTRIES];
	unsigned long total_len = 0;

	for (int i = 0; i < n; i++) {
		int page_idx = hegel_draw_int (tc, 0, MAX_PAGES - 1);
		unsigned off = (unsigned) hegel_draw_int (tc, 0, 4095);
		unsigned max_len = 4096u - off;
		/* Bias toward small but varied lengths so we more often
		** have splits crossing entries.  Min length 1 — zero-length
		** entries trigger a separate (simpler) sg_calculate_split bug
		** that masks the more interesting offset-reset bug under
		** shrinking. */
		unsigned len = (unsigned) hegel_draw_int (tc, 1, (int) max_len);

		entry_page[i] = page_idx;
		entry_off[i] = off;
		entry_len[i] = len;
		total_len += len;

		uint8_t *buf = backing + (unsigned) page_idx * 4096u + off;
		sg_set_buf (&in[i], buf, len);
	}

	/* Sanity: input geometry round-trips via for_each_sg. */
	{
		struct scatterlist *sg;
		int i;
		unsigned long sum = 0;
		for_each_sg (in, sg, n, i) {
			sum += sg->length;
		}
		HEGEL_ASSERT (sum == total_len,
		              "input length mismatch: %lu vs %lu", sum, total_len);
	}

	/* Build oracle: linear concatenation of every input byte. */
	uint8_t *oracle = (uint8_t *) malloc (total_len ? total_len : 1);
	HEGEL_ASSERT (oracle != NULL || total_len == 0,
	              "oracle malloc failed");
	size_t cursor = 0;
	for (int i = 0; i < n; i++) {
		uint8_t *src = backing + (unsigned) entry_page[i] * 4096u + entry_off[i];
		memcpy (oracle + cursor, src, entry_len[i]);
		cursor += entry_len[i];
	}

	/* Save input snapshot to verify no mutation later. */
	struct scatterlist in_snapshot[MAX_ENTRIES + 1];
	memcpy (in_snapshot, in, sizeof in_snapshot);

	if (total_len == 0) {
		/* Skip degenerate empty inputs — sg_split returns -EINVAL. */
		free (oracle);
		return;
	}

	/* Draw skip and nb_splits.  Bias skip toward mid-entry positions
	** by picking a random offset into the total. */
	int skip = hegel_draw_int (tc, 0, (int) total_len - 1);
	unsigned long remaining = total_len - (unsigned long) skip;
	if (remaining == 0) { free (oracle); return; }

	/* Pick nb_splits in [1, min(MAX_SPLITS, remaining)] so we can
	** always allocate >=1 byte per split. */
	int max_splits = (int) (remaining < (unsigned long) MAX_SPLITS
	                        ? remaining : (unsigned long) MAX_SPLITS);
	int nb_splits = hegel_draw_int (tc, 1, max_splits);

	/* Draw sizes such that sum(sizes) <= total_len - skip and each
	** size >= 1.  Reserve at least 1 byte for every remaining split. */
	size_t sizes[MAX_SPLITS];
	unsigned long used = 0;
	for (int k = 0; k < nb_splits; k++) {
		int splits_left = nb_splits - k;
		long avail = (long) (remaining - used) - (long) (splits_left - 1);
		HEGEL_ASSERT (avail >= 1, "avail=%ld remaining=%lu used=%lu splits_left=%d",
		              avail, remaining, used, splits_left);
		int sz = hegel_draw_int (tc, 1, (int) avail);
		sizes[k] = (size_t) sz;
		used += (unsigned long) sz;
	}

	unsigned long sum_sizes = used;
	HEGEL_ASSERT (sum_sizes + (unsigned long) skip <= total_len,
	              "size budget overrun: skip=%d sum=%lu total=%lu",
	              skip, sum_sizes, total_len);

	/* Allocate one pointer per split for sg_split to populate. */
	struct scatterlist *out_sg_arr[MAX_SPLITS];
	int out_mapped[MAX_SPLITS];
	for (int k = 0; k < nb_splits; k++) {
		out_sg_arr[k] = NULL;
		out_mapped[k] = 0;
	}

	int rc = sg_split (in, n, (off_t) skip, nb_splits,
	                   sizes, out_sg_arr, out_mapped, 0);

	hegel_note (tc, "after sg_split");
	HEGEL_ASSERT (rc == 0,
	              "sg_split returned rc=%d (n=%d skip=%d nb=%d)",
	              rc, n, skip, nb_splits);

	/* Property A: total length of each split equals sizes[k]. */
	for (int k = 0; k < nb_splits; k++) {
		HEGEL_ASSERT (out_sg_arr[k] != NULL,
		              "out[%d] is NULL", k);

		unsigned long split_total = 0;
		struct scatterlist *sg;
		int j;
		int seen = 0;
		for_each_sg (out_sg_arr[k], sg, out_mapped[k], j) {
			split_total += sg->length;
			seen++;
		}
		HEGEL_ASSERT (seen == out_mapped[k],
		              "split[%d] seen=%d nents=%d",
		              k, seen, out_mapped[k]);
		HEGEL_ASSERT (split_total == sizes[k],
		              "split[%d] total length %lu != requested %zu (skip=%d, sizes[0]=%zu)",
		              k, split_total, sizes[k], skip, sizes[0]);
	}

	/* Property B: walking every byte of each split recovers
	** oracle[skip + offset_within_concat]. */
	unsigned long oracle_cursor = (unsigned long) skip;
	for (int k = 0; k < nb_splits; k++) {
		uint8_t *got = (uint8_t *) malloc (sizes[k]);
		HEGEL_ASSERT (got != NULL, "got buf malloc");

		size_t copied = copy_sg_bytes (out_sg_arr[k], out_mapped[k], got);
		HEGEL_ASSERT (copied == sizes[k],
		              "split[%d] copy_sg_bytes copied %zu, want %zu",
		              k, copied, sizes[k]);

		if (memcmp (got, oracle + oracle_cursor, sizes[k]) != 0) {
			/* Find first mismatch for actionable error. */
			size_t bad = 0;
			while (bad < sizes[k] && got[bad] == oracle[oracle_cursor + bad]) bad++;
			uint8_t got_byte = got[bad];
			uint8_t want_byte = oracle[oracle_cursor + bad];
			fprintf (stderr,
			    "DATA MISMATCH split[%d] byte %zu: got=0x%02x oracle=0x%02x "
			    "(skip=%d, n=%d, sizes[0]=%zu, nb=%d)\n",
			    k, bad, got_byte, want_byte,
			    skip, n, sizes[0], nb_splits);
			for (int i = 0; i < n; i++) {
				fprintf (stderr,
				    "  in[%d]: page=%d off=%u len=%u\n",
				    i, entry_page[i], entry_off[i], entry_len[i]);
			}
			for (int j = 0; j < out_mapped[k]; j++) {
				fprintf (stderr,
				    "  out[%d][%d]: page=%p off=%u len=%u\n",
				    k, j, (void *) sg_page (&out_sg_arr[k][j]),
				    out_sg_arr[k][j].offset, out_sg_arr[k][j].length);
			}
			free (got);
			HEGEL_ASSERT (0,
			    "byte mismatch at split[%d] byte %zu (got=0x%02x want=0x%02x)",
			    k, bad, got_byte, want_byte);
		}
		free (got);
		oracle_cursor += sizes[k];
	}

	/* Property C: input unchanged. */
	HEGEL_ASSERT (memcmp (in, in_snapshot, sizeof in) == 0,
	              "input scatterlist mutated by sg_split");

	/* Cleanup. */
	for (int k = 0; k < nb_splits; k++) {
		free (out_sg_arr[k]);
	}
	free (oracle);
}

int main (void)
{
	(void) get_backing ();  /* prime in parent before fork */
	hegel_run_test_n (prop_sg_split_oracle, 300);
	return 0;
}
