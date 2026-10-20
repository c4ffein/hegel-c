/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** BUG 5 repro.  `skip` landing exactly on an input entry boundary is not
** consumed by sg_calculate_split()'s strict `if (skip > sglen)` skip path,
** so the entry is counted with a zero-byte contribution and the first split
** gains a leading zero-length entry.  Well-formed input.
**   current/v3: nents=2, entry[0] length 0   <- bug
**   v4a:        nents=1                       <- fixed */

#include <stdio.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

int sg_split(struct scatterlist *in, const int in_mapped_nents,
	     const off_t skip, const int nb_splits, const size_t *split_sizes,
	     struct scatterlist **out, int *out_mapped_nents, gfp_t gfp_mask);

int main(void)
{
	struct scatterlist in[2];
	struct scatterlist *out[1] = { 0 };
	int out_nents[1] = { 0 };
	static char a[10], b[10];
	const size_t sizes[1] = { 5 };
	int ret;

	sg_init_table(in, 2);
	sg_set_buf(&in[0], a, 10);
	sg_set_buf(&in[1], b, 10);

	ret = sg_split(in, 0, 10, 1, sizes, out, out_nents, GFP_KERNEL);
	printf("rc=%d nents=%d\n", ret, out_nents[0]);
	for (int j = 0; j < out_nents[0]; j++)
		printf("  out[0][%d]: offset=%u length=%u%s\n", j,
		       out[0][j].offset, out[0][j].length,
		       out[0][j].length == 0 ? "   <-- ZERO-LENGTH ENTRY" : "");
	return 0;
}
