/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** COMPREHENSIVE contract property test for lib/sg_split.c.
**
** Successor to test_sg_split_contract.c, built from the campaign's
** accumulated generator/oracle lessons:
**   1. don't bake preconditions into generation (bug 4 hid in
**      excluded-by-construction inputs);
**   2. boundary values need explicit generator weight (uniform draws
**      never hit measure-thin sets);
**   3. RELATIONAL boundaries between axes need deliberate construction
**      (bug 5 = skip landing exactly on an entry edge — a coincidence
**      between two independently drawn values);
**   4. the oracle must assert SHAPE wellformedness, not just content
**      (T13 executed bug 5's pathology for three versions while every
**      content check passed).
**
** Generator axes and modes:
**   entries    n in [1,MAX]; per entry: offset {0, uniform, 4095},
**              length {0 (zero-len entity), 1, small, page-large}
**   skip       {0, uniform, EXACT prefix boundary of entry k, total-1}
**   nb_splits  {1, MAX, uniform}
**   sizes[k]   {1, exact-remaining, to-next-entry-boundary, uniform
**              incl. overshoot, zero (low weight)}
**   mode       unmapped / identity-mapped (in_mapped_nents = n)
**
** Contract (parameterized like test_sg_split_contract.c):
**   default (tolerate):           valid = skip + sum(sizes) <= total
**   -DSG_SPLIT_SEMANTICS_EINVAL:  valid additionally requires every
**                                 sizes[k] != 0
**   invalid => rc != 0 and out[] untouched;
**   valid   => rc == 0 and for every split k:
**     - out_nents[k] >= 1 and every entry length >= 1   [shape; under
**       tolerate semantics only asserted when sizes[k] > 0]
**     - exactly one end marker, on the last entry
**     - sum of entry lengths == sizes[k]
**     - bytes match the input oracle at the running cursor
**     - mapped mode: first entry dma_address == DMA_BASE + cursor
**   and the input scatterlist is not mutated. */

#include <hegel_c.h>
#include <linux/scatterlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PAGES     8
#define MAX_ENTRIES   6
#define MAX_SPLITS    4
#define SIZE_SLACK    2
#define DMA_BASE      0x40000000ul
#define BACKING_BYTES (MAX_PAGES * 4096u)

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
		for (unsigned page = 0; page < MAX_PAGES; page++)
			for (unsigned off = 0; off < 4096u; off++)
				b[page * 4096u + off] = pattern_byte((int)page, off);
		buf = b;
	}
	return buf;
}

int sg_split (struct scatterlist *in, const int in_mapped_nents,
              const off_t skip, const int nb_splits,
              const size_t *split_sizes,
              struct scatterlist **out, int *out_mapped_nents,
              gfp_t gfp_mask);

static void prop_sg_split_contract_full (hegel_testcase * tc)
{
	uint8_t *backing = get_backing ();

	/* ---- input entries: geometry with degenerate modes ---- */
	int n = hegel_draw_int (tc, 1, MAX_ENTRIES);

	struct scatterlist in[MAX_ENTRIES + 1];
	sg_init_table (in, n);

	unsigned int entry_off[MAX_ENTRIES], entry_len[MAX_ENTRIES];
	int entry_page[MAX_ENTRIES];
	unsigned long prefix[MAX_ENTRIES + 1];   /* prefix[i] = bytes before entry i */
	unsigned long total_len = 0;
	int in_has_zero_len = 0;

	for (int i = 0; i < n; i++) {
		int page_idx = hegel_draw_int (tc, 0, MAX_PAGES - 1);
		int off_mode = hegel_draw_int (tc, 0, 3);
		unsigned off = off_mode == 0 ? 0
		             : off_mode == 1 ? 4095u
		             : (unsigned) hegel_draw_int (tc, 0, 4095);
		unsigned max_len = 4096u - off;
		int len_mode = hegel_draw_int (tc, 0, 4);
		unsigned len = len_mode == 0 ? 0                    /* zero-len entity */
		             : len_mode == 1 ? 1
		             : len_mode == 2 ? (unsigned) hegel_draw_int (tc, 1,
		                                   (int)(max_len < 16 ? max_len : 16))
		             : (unsigned) hegel_draw_int (tc, 1, (int) max_len);

		entry_page[i] = page_idx;
		entry_off[i] = off;
		entry_len[i] = len;
		prefix[i] = total_len;
		total_len += len;
		if (!len)
			in_has_zero_len = 1;

		uint8_t *buf = backing + (unsigned) page_idx * 4096u + off;
		sg_set_buf (&in[i], buf, len);
	}
	prefix[n] = total_len;

	/* Linear oracle: concatenation of the input's view (zero-len
	** entries contribute nothing, naturally). */
	uint8_t *oracle = (uint8_t *) malloc (total_len ? total_len : 1);
	HEGEL_ASSERT (oracle != NULL, "oracle malloc failed");
	{
		size_t cursor = 0;
		for (int i = 0; i < n; i++) {
			memcpy (oracle + cursor,
			        backing + (unsigned) entry_page[i] * 4096u + entry_off[i],
			        entry_len[i]);
			cursor += entry_len[i];
		}
	}

	/* ---- skip: incl. the relational entry-boundary mode ---- */
	int skip_mode = hegel_draw_int (tc, 0, 3);
	long skip;
	if (skip_mode == 0 || total_len == 0)
		skip = 0;
	else if (skip_mode == 1)                 /* exact prefix boundary */
		skip = (long) prefix[hegel_draw_int (tc, 1, n)];
	else if (skip_mode == 2)
		skip = (long) total_len - 1;
	else
		skip = hegel_draw_int (tc, 0, (int) total_len - 1);

	/* ---- nb_splits ---- */
	int nb_mode = hegel_draw_int (tc, 0, 2);
	int nb_splits = nb_mode == 0 ? 1
	              : nb_mode == 1 ? MAX_SPLITS
	              : hegel_draw_int (tc, 1, MAX_SPLITS);

	/* ---- sizes: relational modes ---- */
	size_t sizes[MAX_SPLITS];
	long budget_left = (long) total_len - skip;
	unsigned long cursor_for_mode = (unsigned long) skip;
	unsigned long sum_sizes = 0;
	int has_zero = 0;

	for (int k = 0; k < nb_splits; k++) {
		long hi = budget_left + SIZE_SLACK;
		if (hi < SIZE_SLACK)
			hi = SIZE_SLACK;
		int mode = hegel_draw_int (tc, 0, 9);
		long sz;
		switch (mode) {
		case 0:                              /* minimal */
			sz = 1;
			break;
		case 1: case 2:                      /* exact exhaustion */
			sz = budget_left > 0 ? budget_left : 1;
			break;
		case 3: case 4: {                    /* to next entry boundary */
			unsigned long c = cursor_for_mode;
			int b = 0;
			while (b <= n && prefix[b] <= c)
				b++;
			sz = (b <= n && prefix[b] > c) ? (long)(prefix[b] - c) : 1;
			break;
		}
		case 5:                              /* zero (low weight) */
			sz = 0;
			break;
		default:                             /* uniform incl. overshoot */
			sz = hegel_draw_int (tc, 0, (int) hi);
			break;
		}
		sizes[k] = (size_t) sz;
		sum_sizes += (unsigned long) sz;
		budget_left -= sz;
		cursor_for_mode += (unsigned long) sz;
		if (!sz)
			has_zero = 1;
	}

	int valid = (unsigned long) skip + sum_sizes <= total_len;
#ifdef SG_SPLIT_SEMANTICS_EINVAL
	valid = valid && !has_zero;
#endif

	/* ---- mode: unmapped or identity-mapped ---- */
	int mapped = hegel_draw_int (tc, 0, 1);
	if (mapped) {
		dma_addr_t a = DMA_BASE;
		for (int i = 0; i < n; i++) {
			sg_dma_address (&in[i]) = a;
			sg_dma_len (&in[i]) = entry_len[i];
			a += entry_len[i];
		}
	}

	struct scatterlist in_snapshot[MAX_ENTRIES + 1];
	memcpy (in_snapshot, in, sizeof in_snapshot);

	struct scatterlist *out_sg_arr[MAX_SPLITS];
	int out_nents[MAX_SPLITS];
	for (int k = 0; k < nb_splits; k++) {
		out_sg_arr[k] = NULL;
		out_nents[k] = 0;
	}

	int rc = sg_split (in, mapped ? n : 0, (off_t) skip, nb_splits,
	                   sizes, out_sg_arr, out_nents, 0);

	hegel_note (tc, "sg_split returned");

	if (!valid) {
		HEGEL_ASSERT (rc != 0,
		    "CONTRACT: invalid request accepted: rc=0 (skip=%ld "
		    "sum=%lu total=%lu has_zero=%d n=%d nb=%d mapped=%d)",
		    skip, sum_sizes, total_len, has_zero, n, nb_splits, mapped);
		for (int k = 0; k < nb_splits; k++)
			HEGEL_ASSERT (out_sg_arr[k] == NULL,
			    "CONTRACT: out[%d] written on error path", k);
		free (oracle);
		return;
	}

	HEGEL_ASSERT (rc == 0,
	    "CONTRACT: valid request rejected: rc=%d (skip=%ld sum=%lu "
	    "total=%lu n=%d nb=%d mapped=%d sizes=[%zu,%zu,%zu,%zu])",
	    rc, skip, sum_sizes, total_len, n, nb_splits, mapped,
	    sizes[0], nb_splits > 1 ? sizes[1] : 0,
	    nb_splits > 2 ? sizes[2] : 0, nb_splits > 3 ? sizes[3] : 0);

	unsigned long cursor = (unsigned long) skip;
	for (int k = 0; k < nb_splits; k++) {
		int check_shape = 1;
#ifndef SG_SPLIT_SEMANTICS_EINVAL
		/* Under tolerate semantics zero-size splits legitimately
		** return degenerate shapes; shape checks apply to the
		** nonzero-size splits only. */
		check_shape = sizes[k] > 0;
#endif
		HEGEL_ASSERT (out_sg_arr[k] != NULL, "out[%d] is NULL", k);

		if (check_shape) {
			HEGEL_ASSERT (out_nents[k] >= 1,
			    "SHAPE: split[%d] has %d entries for %zu bytes",
			    k, out_nents[k], sizes[k]);
			if (!in_has_zero_len)
				HEGEL_ASSERT (out_nents[k] <= n,
				    "SHAPE: split[%d] nents %d exceeds input nents %d",
				    k, out_nents[k], n);
			for (int j = 0; j < out_nents[k]; j++) {
				/* sg_split passes zero-length INPUT entries through
				** (they stay in the copiers' sg_next() walk).  It must
				** never MANUFACTURE one: with a well-formed input, no
				** output entry may have length 0. */
				HEGEL_ASSERT (in_has_zero_len ||
				              out_sg_arr[k][j].length >= 1,
				    "SHAPE: split[%d] entry[%d] length 0 from a "
				    "well-formed input (offset=%u, skip=%ld, n=%d, mapped=%d)",
				    k, j, out_sg_arr[k][j].offset, skip, n, mapped);
				HEGEL_ASSERT (!!sg_is_last (&out_sg_arr[k][j]) ==
				              (j == out_nents[k] - 1),
				    "SHAPE: split[%d] entry[%d] end-marker wrong",
				    k, j);
			}
		}

		unsigned long split_total = 0;
		for (int j = 0; j < out_nents[k]; j++)
			split_total += out_sg_arr[k][j].length;
		HEGEL_ASSERT (split_total == sizes[k],
		    "split[%d] total %lu != requested %zu (skip=%ld, mapped=%d)",
		    k, split_total, sizes[k], skip, mapped);

		if (sizes[k]) {
			uint8_t *got = (uint8_t *) malloc (sizes[k]);
			HEGEL_ASSERT (got != NULL, "got malloc");
			size_t copied = 0;
			for (int j = 0; j < out_nents[k]; j++) {
				struct scatterlist *e = &out_sg_arr[k][j];
				memcpy (got + copied,
				        (uint8_t *) page_address (sg_page (e)) + e->offset,
				        e->length);
				copied += e->length;
			}
			HEGEL_ASSERT (copied == sizes[k],
			    "split[%d] copied %zu want %zu", k, copied, sizes[k]);
			HEGEL_ASSERT (memcmp (got, oracle + cursor, sizes[k]) == 0,
			    "split[%d] BYTES differ from oracle (skip=%ld n=%d "
			    "nb=%d mapped=%d)", k, skip, n, nb_splits, mapped);
			free (got);
		}

		if (mapped && check_shape && out_nents[k] >= 1)
			HEGEL_ASSERT (sg_dma_address (&out_sg_arr[k][0]) ==
			              DMA_BASE + cursor,
			    "DMA: split[%d] first dma_address %#lx != %#lx",
			    k, (unsigned long) sg_dma_address (&out_sg_arr[k][0]),
			    DMA_BASE + cursor);

		cursor += sizes[k];
	}

	HEGEL_ASSERT (memcmp (in, in_snapshot, sizeof in) == 0,
	              "input scatterlist mutated by sg_split");

	for (int k = 0; k < nb_splits; k++)
		free (out_sg_arr[k]);
	free (oracle);
}

int main (void)
{
	(void) get_backing ();
	hegel_run_test_n (prop_sg_split_contract_full, 800);
	return 0;
}
