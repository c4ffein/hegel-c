/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Counterexample shrunk by test_sg_split_contract_full.c against the NAIVE
** bug-5 fix (`skip >= sglen`, sg_split_v4.c): it makes the skip path fire for
** a mid-list zero-length entry (0 >= 0), dropping it from nents while the
** copiers still walk onto it via sg_next() -- out_sg[-1].length then stretches
** it over the wrong page.  SILENT DATA CORRUPTION.
**   v4  (skip >= sglen):        bytes 5a 5a  <- corruption
**   v3  (skip >  sglen):        bytes 5a 49  (zero-len entry emitted, bytes ok)
**   v4a (skip && skip >= sglen) bytes 5a 49  <- correct: entries may only be
**                                              dropped BEFORE in_sg0 is set */

#include <stdio.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
int sg_split(struct scatterlist *, const int, const off_t, const int,
	     const size_t *, struct scatterlist **, int *, gfp_t);
static unsigned char *backing;
int main(void)
{
	struct scatterlist in[3];
	struct scatterlist *out[1] = { 0 };
	int out_nents[1] = { 0 };
	const size_t sizes[1] = { 2 };
	backing = aligned_alloc(4096, 8192);
	for (unsigned i = 0; i < 8192; i++) backing[i] = (unsigned char)(i * 17u + 0x5a);

	sg_init_table(in, 3);
	sg_set_buf(&in[0], backing + 0,    1);   /* byte A */
	sg_set_buf(&in[1], backing + 0,    0);   /* zero-length */
	sg_set_buf(&in[2], backing + 4095, 1);   /* byte B */

	int rc = sg_split(in, 0, 0, 1, sizes, out, out_nents, GFP_KERNEL);
	printf("rc=%d nents=%d\n", rc, out_nents[0]);
	unsigned char want[2] = { backing[0], backing[4095] };
	unsigned char got[2]; int c = 0;
	for (int j = 0; j < out_nents[0]; j++) {
		struct scatterlist *e = &out[0][j];
		printf("  entry[%d]: offset=%u length=%u\n", j, e->offset, e->length);
		for (unsigned b = 0; b < e->length && c < 2; b++)
			got[c++] = ((unsigned char *)page_address(sg_page(e)))[e->offset + b];
	}
	printf("want bytes: %02x %02x\n", want[0], want[1]);
	printf("got  bytes: %02x %02x   -> %s\n", got[0], got[1],
	       (c == 2 && got[0] == want[0] && got[1] == want[1]) ? "OK" : "*** DATA CORRUPTION ***");
	return 0;
}
