/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
**
** Deterministic test cases for lib/sg_split.c.  Each case sets up a
** specific (input SG, skip, sizes) tuple and checks the two documented
** post-conditions, in BOTH the CPU view (->length / page bytes) and,
** when run mapped, the DMA view (sg_dma_len / sg_dma_address):
**
**   1. sum of out[k] entry lengths equals split_sizes[k]   (per-split size)
**   2. the bytes/addresses covered by out[k] are the i-th
**      [skip + sum(prev sizes) ..] window of the input  (per-split content)
**
** COVERAGE MATRIX
** ---------------
** Every base case (T1..T8b) runs twice via RUN():
**   - /unmapped : in_mapped_nents = 0      -> pass 1 + sg_split_phys only
**   - /mapped   : identity DMA mapping     -> pass 1 + phys + pass 2 + mapped
**
** The identity mapping sets dma_len[i] == length[i] and lays the DMA
** address space out contiguously from DMA_BASE, so pass 2 computes the
** SAME geometry as pass 1 and the expected values are reused; we just
** also assert the DMA addresses are right.  This is the cheap "run all
** existing cases through sg_split_mapped too" coverage (idea 1).
**
** Group G (idea 2) only builds when CONFIG_NEED_SG_DMA_LENGTH is set
** (so dma_length is a real separate field that can diverge from
** ->length).  There the DMA segmentation is COARSER than the CPU one
** (coalescing), so pass 2 computes a different per-split nents than
** pass 1 — the only path that genuinely exercises divergent geometry.
** On !NEED arches sg_dma_len aliases ->length, so divergence cannot be
** represented and these cases are skipped.
**
** Build/run: see the `cases` target in the Makefile (ASAN + UBSAN, no
** hegel — sg_split is called directly). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <linux/scatterlist.h>

extern int sg_split (struct scatterlist *in, const int in_mapped_nents,
                     const off_t skip, const int nb_splits,
                     const size_t *split_sizes,
                     struct scatterlist **out, int *out_mapped_nents,
                     gfp_t gfp_mask);

#define MAX_IN_ENTRIES   8
#define MAX_SPLITS       4
#define PAGE_BYTES       4096
/* Synthetic IOVA base, well clear of any real userspace page address so a
** mistaken CPU-vs-DMA mixup is obvious. */
#define DMA_BASE         ((dma_addr_t) 0x100000000ULL)

static uint8_t backing[MAX_IN_ENTRIES * PAGE_BYTES];

/* Position-unique byte so we can verify "out[k][i] == in[skip + offset + i]". */
static uint8_t pattern (int page, unsigned off)
{
	return (uint8_t)(((page * 31u) + off * 17u + 0x5a) & 0xffu);
}

static void backing_init (void)
{
	for (int p = 0; p < MAX_IN_ENTRIES; p++)
		for (unsigned o = 0; o < PAGE_BYTES; o++)
			backing[p * PAGE_BYTES + o] = pattern (p, o);
}

struct in_entry { int page; unsigned off; unsigned len; };

/* ---- shared verification helpers ------------------------------------ */

/* CPU view: property 1 (length sum == want) + property 2 (page bytes match
** the linear oracle window [cursor .. cursor+want)). */
static int verify_cpu (const char *name, int k,
                       struct scatterlist *out, int nents, size_t want,
                       const uint8_t *oracle, size_t oracle_total,
                       unsigned long cursor)
{
	int fails = 0;
	struct scatterlist *sg;
	int j;

	unsigned long sum = 0;
	for_each_sg (out, sg, nents, j)
		sum += sg->length;
	if (sum != want) {
		printf ("FAIL %s: split[%d] CPU sum=%lu, requested=%zu  (P1 size)\n",
		        name, k, sum, want);
		fails++;
	}

	uint8_t got[PAGE_BYTES * MAX_IN_ENTRIES];
	size_t copied = 0;
	for_each_sg (out, sg, nents, j) {
		if (copied + sg->length > sizeof got) break;
		memcpy (got + copied,
		        (uint8_t *) page_address (sg_page (sg)) + sg->offset,
		        sg->length);
		copied += sg->length;
	}
	size_t check_len = sum < want ? sum : want;
	if (cursor + check_len > oracle_total) {
		printf ("FAIL %s: split[%d] CPU oracle window OOR "
		        "(cursor=%lu len=%zu total=%zu)\n",
		        name, k, cursor, check_len, oracle_total);
		fails++;
	} else if (memcmp (got, oracle + cursor, check_len) != 0) {
		printf ("FAIL %s: split[%d] CPU bytes mismatch  (P2 content)\n",
		        name, k);
		fails++;
	}
	return fails;
}

/* DMA view: property 1 (sg_dma_len sum == want) + property 2 (the DMA
** addresses tile the contiguous IOVA window [DMA_BASE+cursor ..]).  Works
** for both identity and coalesced mappings because we always lay the DMA
** address space out contiguously, so logical byte b lives at DMA_BASE+b. */
static int verify_dma (const char *name, int k,
                       struct scatterlist *out, int nents, size_t want,
                       unsigned long cursor)
{
	int fails = 0;
	struct scatterlist *sg;
	int j;

	unsigned long sum = 0;
	unsigned long pos = cursor;
	for_each_sg (out, sg, nents, j) {
		dma_addr_t want_addr = DMA_BASE + pos;
		if (sg_dma_address (sg) != want_addr) {
			printf ("FAIL %s: split[%d] DMA addr[%d]=0x%llx want 0x%llx\n",
			        name, k, j,
			        (unsigned long long) sg_dma_address (sg),
			        (unsigned long long) want_addr);
			fails++;
		}
		sum += sg_dma_len (sg);
		pos += sg_dma_len (sg);
	}
	if (sum != want) {
		printf ("FAIL %s: split[%d] DMA sum=%lu, requested=%zu  (P1 size)\n",
		        name, k, sum, want);
		fails++;
	}
	return fails;
}

/* Build the linear CPU oracle (concatenation of input entry bytes). */
static size_t build_oracle (const struct in_entry *in_spec, int n_in,
                            uint8_t *oracle)
{
	size_t op = 0;
	for (int i = 0; i < n_in; i++) {
		memcpy (oracle + op,
		        backing + in_spec[i].page * PAGE_BYTES + in_spec[i].off,
		        in_spec[i].len);
		op += in_spec[i].len;
	}
	return op;
}

/* ---- idea 1: run one case unmapped or mapped (identity DMA) ---------- */

static int run_case (const char *name,
                     const struct in_entry *in_spec, int n_in,
                     off_t skip,
                     const size_t *sizes, int nb_splits,
                     bool dma_mapped)
{
	struct scatterlist in[MAX_IN_ENTRIES + 1];
	struct scatterlist *out[MAX_SPLITS] = {0};
	int out_nents[MAX_SPLITS] = {0};

	sg_init_table (in, n_in);
	for (int i = 0; i < n_in; i++) {
		uint8_t *buf = backing + in_spec[i].page * PAGE_BYTES + in_spec[i].off;
		sg_set_buf (&in[i], buf, in_spec[i].len);
	}

	int in_mapped_nents = 0;
	if (dma_mapped) {
		/* Identity mapping: dma_len == length, addresses contiguous. */
		dma_addr_t a = DMA_BASE;
		for (int i = 0; i < n_in; i++) {
			sg_dma_address (&in[i]) = a;
			sg_dma_len (&in[i]) = in_spec[i].len;
			a += in_spec[i].len;
		}
		in_mapped_nents = n_in;
	}

	int rc = sg_split (in, in_mapped_nents, skip, nb_splits, sizes,
	                   out, out_nents, 0);
	if (rc != 0) {
		printf ("FAIL %s: sg_split rc=%d\n", name, rc);
		return 1;
	}

	uint8_t oracle[PAGE_BYTES * MAX_IN_ENTRIES];
	size_t oracle_total = build_oracle (in_spec, n_in, oracle);

	int fails = 0;
	unsigned long cursor = (unsigned long) skip;
	for (int k = 0; k < nb_splits; k++) {
		fails += verify_cpu (name, k, out[k], out_nents[k], sizes[k],
		                     oracle, oracle_total, cursor);
		if (dma_mapped)
			fails += verify_dma (name, k, out[k], out_nents[k],
			                     sizes[k], cursor);
		cursor += sizes[k];
	}

	for (int k = 0; k < nb_splits; k++)
		free (out[k]);

	if (!fails) {
		printf ("PASS %s\n", name);
		return 0;
	}
	return 1;
}

#define RUN(nm, in, n, skip, sizes, nb)                                  \
	do {                                                             \
		total_fails += run_case (nm "/unmapped", in, n, skip,    \
		                         sizes, nb, false);             \
		total_fails += run_case (nm "/mapped",   in, n, skip,    \
		                         sizes, nb, true);              \
	} while (0)

#ifdef CONFIG_NEED_SG_DMA_LENGTH
/* ---- idea 2: divergent mapping (NEED arch only) --------------------- *
** The DMA segmentation differs from the CPU one (coalescing): dma_seg[]
** gives the DMA entry lengths laid out contiguously from DMA_BASE, on
** the first n_dma input entries.  CPU lengths stay as in_spec.  pass 2
** thus computes a different per-split nents than pass 1. */
static int run_case_divergent (const char *name,
                               const struct in_entry *in_spec, int n_in,
                               off_t skip,
                               const size_t *sizes, int nb_splits,
                               const unsigned *dma_seg, int n_dma)
{
	struct scatterlist in[MAX_IN_ENTRIES + 1];
	struct scatterlist *out[MAX_SPLITS] = {0};
	int out_nents[MAX_SPLITS] = {0};

	sg_init_table (in, n_in);
	for (int i = 0; i < n_in; i++) {
		uint8_t *buf = backing + in_spec[i].page * PAGE_BYTES + in_spec[i].off;
		sg_set_buf (&in[i], buf, in_spec[i].len);
	}
	/* Coalesced DMA view on the first n_dma entries, contiguous IOVA. */
	dma_addr_t a = DMA_BASE;
	for (int i = 0; i < n_dma; i++) {
		sg_dma_address (&in[i]) = a;
		sg_dma_len (&in[i]) = dma_seg[i];
		a += dma_seg[i];
	}

	int rc = sg_split (in, n_dma, skip, nb_splits, sizes,
	                   out, out_nents, 0);
	if (rc != 0) {
		printf ("FAIL %s: sg_split rc=%d\n", name, rc);
		return 1;
	}

	uint8_t oracle[PAGE_BYTES * MAX_IN_ENTRIES];
	size_t oracle_total = build_oracle (in_spec, n_in, oracle);

	int fails = 0;
	unsigned long cursor = (unsigned long) skip;
	for (int k = 0; k < nb_splits; k++) {
		/* CPU view still describes the scattered pages (pass 1).  Its
		** entry count is the end-marker count sg_nents(), NOT
		** out_nents[k] — under coalescing the latter is the (smaller)
		** DMA count.  This is the orig_nents vs dma_nents overlay: the
		** output array carries the full CPU view AND a shorter DMA view. */
		fails += verify_cpu (name, k, out[k], sg_nents (out[k]), sizes[k],
		                     oracle, oracle_total, cursor);
		/* DMA view describes the coalesced contiguous IOVA (pass 2),
		** walked with the DMA count out_nents[k]. */
		fails += verify_dma (name, k, out[k], out_nents[k], sizes[k],
		                     cursor);
		cursor += sizes[k];
	}

	for (int k = 0; k < nb_splits; k++)
		free (out[k]);

	if (!fails) {
		printf ("PASS %s\n", name);
		return 0;
	}
	return 1;
}
#endif /* CONFIG_NEED_SG_DMA_LENGTH */

int main (void)
{
	backing_init ();

	int total_fails = 0;

#ifdef CONFIG_NEED_SG_DMA_LENGTH
	const char *arch = "NEED_SG_DMA_LENGTH (x86/arm64 class: dma_length is a separate field)";
#else
	const char *arch = "!NEED_SG_DMA_LENGTH (riscv/m68k class: sg_dma_len aliases ->length)";
#endif
	printf ("=== sg_split case suite — arch model: %s ===\n\n", arch);

	/* ===== Group A — full / edge-aligned coverage ===== */
	{ struct in_entry in[] = {{0,0,100}};            size_t s[] = {100};
	  RUN ("T1_single_entry_full",            in, 1, 0,  s, 1); }
	{ struct in_entry in[] = {{0,0,60},{1,0,40}};    size_t s[] = {100};
	  RUN ("T2_two_entries_full_coverage",    in, 2, 0,  s, 1); }
	{ struct in_entry in[] = {{0,0,60},{1,0,40}};    size_t s[] = {50,50};
	  RUN ("T3_two_entries_two_splits_full",  in, 2, 0,  s, 2); }
	{ struct in_entry in[] = {{0,0,60},{1,0,40}};    size_t s[] = {60};
	  RUN ("T4_edge_aligned_partial",         in, 2, 0,  s, 1); }

	/* ===== Group B — partial coverage, mid-entry boundary (bug 1 / bug C) ===== */
	{ struct in_entry in[] = {{0,0,2},{1,0,1}};      size_t s[] = {1};
	  RUN ("T5_minimal_repro",                in, 2, 0,  s, 1); }
	{ struct in_entry in[] = {{0,0,64},{1,0,100},{2,0,16}}; size_t s[] = {100};
	  RUN ("T6_dthev2_aead_shape",            in, 3, 64, s, 1); }
	{ struct in_entry in[] = {{0,0,100},{1,0,50}};   size_t s[] = {30};
	  RUN ("T7_skip_plus_partial_midentry",   in, 2, 20, s, 1); }

	/* ===== Group C — trailing zero-size split ===== */
	{ struct in_entry in[] = {{0,0,10},{1,0,5}};     size_t s[] = {10,0};
	  RUN ("T8_sizes_trailing_zero",          in, 2, 0,  s, 2); }
	/* t8b: first split consumes ALL input; trailing zero split gets
	** nents==0 -> patch-4 OOB guard territory (out_sg is ZERO_SIZE_PTR). */
	{ struct in_entry in[] = {{0,0,10}};             size_t s[] = {10,0};
	  RUN ("T8b_trailing_zero_no_input_left", in, 1, 0,  s, 2); }

	/* ===== Group D — boundary-position matrix ===== */
	{ struct in_entry in[] = {{0,0,10},{1,0,10},{2,0,10}}; size_t s[] = {5};
	  RUN ("T9_mid_first_two_trailing",       in, 3, 0,  s, 1); }
	{ struct in_entry in[] = {{0,0,10},{1,0,10},{2,0,10}}; size_t s[] = {10};
	  RUN ("T10_edge_first_two_trailing",     in, 3, 0,  s, 1); }
	{ struct in_entry in[] = {{0,0,10},{1,0,10},{2,0,10}}; size_t s[] = {15};
	  RUN ("T11_mid_middle_one_trailing",     in, 3, 0,  s, 1); }
	{ struct in_entry in[] = {{0,0,10},{1,0,10},{2,0,10}}; size_t s[] = {25};
	  RUN ("T12_mid_last_no_trailing",        in, 3, 0,  s, 1); }

	/* ===== Group E — skip-position variations ===== */
	{ struct in_entry in[] = {{0,0,10},{1,0,10},{2,0,10}}; size_t s[] = {5};
	  RUN ("T13_skip_at_entry_boundary",      in, 3, 10, s, 1); }
	{ struct in_entry in[] = {{0,0,10},{1,0,10},{2,0,10}}; size_t s[] = {5};
	  RUN ("T14_skip_fastforward_partial",    in, 3, 12, s, 1); }

	/* ===== Group F — multi-split, mid-entry internal transition ===== */
	{ struct in_entry in[] = {{0,0,10},{1,0,10},{2,0,10}}; size_t s[] = {5,10};
	  RUN ("T15_two_splits_last_mid_trailing", in, 3, 0, s, 2); }
	{ struct in_entry in[] = {{0,0,10},{1,0,10}};    size_t s[] = {3,17};
	  RUN ("T16_two_splits_full_coverage_mid_first", in, 2, 0, s, 2); }

#ifdef CONFIG_NEED_SG_DMA_LENGTH
	/* ===== Group G — divergent DMA mapping (idea 2, NEED only) =====
	** CPU view is 3 scattered 4096-byte entries; DMA view coalesces them
	** into fewer, larger contiguous segments.  Each split's DMA nents is
	** therefore < its CPU nents, exercising sg_split_mapped over geometry
	** pass 1 never sees. */
	printf ("\n--- Group G: divergent CPU/DMA geometry (coalescing) ---\n");
	{ /* D1: CPU [4096,4096,4096] -> DMA [12288] (3->1). */
	  struct in_entry in[] = {{0,0,4096},{1,0,4096},{2,0,4096}};
	  size_t s[]        = {5000, 7288};
	  unsigned dma[]    = {12288};
	  total_fails += run_case_divergent ("D1_coalesce_3to1", in, 3, 0, s, 2, dma, 1); }
	{ /* D2: CPU [4096,4096,4096] -> DMA [8192,4096] (3->2). */
	  struct in_entry in[] = {{0,0,4096},{1,0,4096},{2,0,4096}};
	  size_t s[]        = {6000, 6288};
	  unsigned dma[]    = {8192, 4096};
	  total_fails += run_case_divergent ("D2_coalesce_3to2", in, 3, 0, s, 2, dma, 2); }
	{ /* D3: 3->1 with skip landing mid first DMA segment. */
	  struct in_entry in[] = {{0,0,4096},{1,0,4096},{2,0,4096}};
	  size_t s[]        = {5000};
	  unsigned dma[]    = {12288};
	  total_fails += run_case_divergent ("D3_coalesce_3to1_skip", in, 3, 1000, s, 1, dma, 1); }
#endif

	printf ("\n");
	if (total_fails == 0) {
		printf ("ALL TESTS PASS\n");
		return 0;
	}
	printf ("%d test(s) failed\n", total_fails);
	return 1;
}
