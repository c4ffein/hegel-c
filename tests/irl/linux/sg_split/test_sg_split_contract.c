/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Contract-boundary property test for lib/sg_split.c (v2-fixed snapshot
** in sg_split_v2.c).
**
** Companion to test_sg_split.c, which constrains its generator to
** "polite caller" inputs: every split size >= 1 and
** skip + sum(sizes) <= total_len.  Those constraints hide the region
** where Sashiko's v2 review found bug 4, so here the generator is
** widened in exactly two ways:
**   - split sizes are drawn from [0, avail + slack]  (zeros allowed)
**   - the sum may overshoot the input             (infeasible allowed)
**
** The property is the EXPORT_SYMBOL contract itself, parameterized over
** the two candidate zero-size semantics:
**   default (tolerate):  valid = skip + sum(sizes) <= total_len
**   -DSG_SPLIT_SEMANTICS_EINVAL:  valid additionally requires every
**     sizes[k] != 0 (zero-size splits are rejected upfront)
** In both cases:  invalid request => rc != 0;  valid request => rc == 0
** and every split k satisfies sum(entry lengths) == sizes[k] plus
** byte-exactness against the linear oracle.
**
** Bug 4 (pre-existing, found by the Sashiko review of v2): a zero size
** loaded exactly at input exhaustion makes sg_calculate_split() return
** success, silently ignoring any later non-zero sizes.  Minimal shape:
** in=[1], sizes={1,0,1} -> rc==0 with split[2] unfulfilled. */

#include <hegel_c.h>
#include <linux/scatterlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PAGES     8
#define MAX_ENTRIES   6
#define MAX_SPLITS    4
#define SIZE_SLACK    2   /* how far past the remaining budget a size may go */
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
		for (unsigned page = 0; page < MAX_PAGES; page++) {
			for (unsigned off = 0; off < 4096u; off++) {
				b[page * 4096u + off] = pattern_byte((int)page, off);
			}
		}
		buf = b;
	}
	return buf;
}

/* Forward declaration — sg_split_v2.c provides the symbol. */
int sg_split (struct scatterlist *in, const int in_mapped_nents,
              const off_t skip, const int nb_splits,
              const size_t *split_sizes,
              struct scatterlist **out, int *out_mapped_nents,
              gfp_t gfp_mask);

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

static void prop_sg_split_contract (hegel_testcase * tc)
{
	uint8_t *backing = get_backing ();

	int n = hegel_draw_int (tc, 1, MAX_ENTRIES);

	struct scatterlist in[MAX_ENTRIES + 1];
	sg_init_table (in, n);

	/* Input entries: min length 1, same as test_sg_split.c (zero-length
	** INPUT entries are a separate degenerate axis; here the degenerate
	** axis under test is the SIZES array). */
	unsigned int entry_off[MAX_ENTRIES];
	unsigned int entry_len[MAX_ENTRIES];
	int entry_page[MAX_ENTRIES];
	unsigned long total_len = 0;

	for (int i = 0; i < n; i++) {
		int page_idx = hegel_draw_int (tc, 0, MAX_PAGES - 1);
		unsigned off = (unsigned) hegel_draw_int (tc, 0, 4095);
		unsigned max_len = 4096u - off;
		unsigned len = (unsigned) hegel_draw_int (tc, 1, (int) max_len);

		entry_page[i] = page_idx;
		entry_off[i] = off;
		entry_len[i] = len;
		total_len += len;

		uint8_t *buf = backing + (unsigned) page_idx * 4096u + off;
		sg_set_buf (&in[i], buf, len);
	}

	uint8_t *oracle = (uint8_t *) malloc (total_len);
	HEGEL_ASSERT (oracle != NULL, "oracle malloc failed");
	size_t cursor = 0;
	for (int i = 0; i < n; i++) {
		uint8_t *src = backing + (unsigned) entry_page[i] * 4096u + entry_off[i];
		memcpy (oracle + cursor, src, entry_len[i]);
		cursor += entry_len[i];
	}

	int skip = hegel_draw_int (tc, 0, (int) total_len - 1);
	int nb_splits = hegel_draw_int (tc, 1, MAX_SPLITS);

	/* Sizes: zeros allowed, and the running budget may be overshot.  A
	** uniform draw over [0, budget + slack] almost never lands on the
	** boundary shapes that matter (budgets run into the thousands), so
	** bias each size toward the two interesting degenerate values:
	** exactly-the-remaining-budget and zero. */
	size_t sizes[MAX_SPLITS];
	long budget_left = (long) total_len - skip;
	unsigned long sum_sizes = 0;
	int has_zero = 0;
	for (int k = 0; k < nb_splits; k++) {
		long hi = budget_left + SIZE_SLACK;
		if (hi < SIZE_SLACK)
			hi = SIZE_SLACK;
		int mode = hegel_draw_int (tc, 0, 5);
		int sz;
		if (mode == 0)
			sz = 0;
		else if (mode == 1 && budget_left > 0)
			sz = (int) budget_left;      /* exact exhaustion */
		else
			sz = hegel_draw_int (tc, 0, (int) hi);
		sizes[k] = (size_t) sz;
		sum_sizes += (unsigned long) sz;
		budget_left -= sz;
		if (!sz)
			has_zero = 1;
	}
	int feasible = (unsigned long) skip + sum_sizes <= total_len;
#ifdef SG_SPLIT_SEMANTICS_EINVAL
	feasible = feasible && !has_zero;
#else
	(void) has_zero;
#endif

	struct scatterlist *out_sg_arr[MAX_SPLITS];
	int out_mapped[MAX_SPLITS];
	for (int k = 0; k < nb_splits; k++) {
		out_sg_arr[k] = NULL;
		out_mapped[k] = 0;
	}

	int rc = sg_split (in, 0, (off_t) skip, nb_splits,
	                   sizes, out_sg_arr, out_mapped, 0);

	if (!feasible) {
		/* Contract: an invalid request (over-coverage, or a zero
		** size under -EINVAL semantics) must be rejected — a 0
		** return with unfulfillable splits hands the caller
		** success + garbage lists. */
		HEGEL_ASSERT (rc != 0,
		    "CONTRACT: sg_split accepted an invalid request: rc=0 "
		    "(skip=%d sum(sizes)=%lu total=%lu has_zero=%d "
		    "n=%d nb=%d sizes=[%zu,%zu,%zu,%zu])",
		    skip, sum_sizes, total_len, has_zero, n, nb_splits,
		    sizes[0], nb_splits > 1 ? sizes[1] : 0,
		    nb_splits > 2 ? sizes[2] : 0, nb_splits > 3 ? sizes[3] : 0);
		free (oracle);
		return;
	}

	HEGEL_ASSERT (rc == 0,
	              "sg_split rejected a valid request: rc=%d "
	              "(n=%d skip=%d nb=%d sizes=[%zu,%zu,%zu,%zu])",
	              rc, n, skip, nb_splits,
	              sizes[0], nb_splits > 1 ? sizes[1] : 0,
	              nb_splits > 2 ? sizes[2] : 0, nb_splits > 3 ? sizes[3] : 0);

	/* Property A: total length of each split equals sizes[k].  Holds for
	** zero-size splits under both current shapes (nents==0 ZERO_SIZE_PTR
	** list, and nents==1 zero-length-entry list). */
	unsigned long oracle_cursor = (unsigned long) skip;
	for (int k = 0; k < nb_splits; k++) {
		HEGEL_ASSERT (out_sg_arr[k] != NULL, "out[%d] is NULL", k);

		unsigned long split_total = 0;
		struct scatterlist *sg;
		int j;
		for_each_sg (out_sg_arr[k], sg, out_mapped[k], j)
			split_total += sg->length;
		HEGEL_ASSERT (split_total == sizes[k],
		              "split[%d] total length %lu != requested %zu "
		              "(skip=%d nb=%d)",
		              k, split_total, sizes[k], skip, nb_splits);

		/* Property B: byte-exactness against the oracle. */
		if (sizes[k]) {
			uint8_t *got = (uint8_t *) malloc (sizes[k]);
			HEGEL_ASSERT (got != NULL, "got buf malloc");
			size_t copied = copy_sg_bytes (out_sg_arr[k], out_mapped[k], got);
			HEGEL_ASSERT (copied == sizes[k],
			              "split[%d] copied %zu, want %zu", k, copied, sizes[k]);
			HEGEL_ASSERT (memcmp (got, oracle + oracle_cursor, sizes[k]) == 0,
			              "split[%d] byte mismatch vs oracle (skip=%d)", k, skip);
			free (got);
		}
		oracle_cursor += sizes[k];
	}

	for (int k = 0; k < nb_splits; k++)
		free (out_sg_arr[k]);
	free (oracle);
}

int main (void)
{
	(void) get_backing ();  /* prime in parent before fork */
	hegel_run_test_n (prop_sg_split_contract, 500);
	return 0;
}
