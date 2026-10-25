# v2 commit messages (draft — for review, not the patches themselves)

Tags convention: code fixes (1, 2, 4) carry Fixes:; test patches (3, 5) do
not. All carry Assisted-by + Signed-off-by.

================================================================================
[PATCH v2 1/5] lib: scatterlist: fix sg_calculate_split() nb_splits overshoot on partial coverage
================================================================================
(code UNCHANGED from v1 1/2 — only the cross-reference to the test patch
is reworded for the new ordering)

sg_calculate_split() decrements nb_splits in two places per outer-loop
iteration: once in the inner while() that advances to the next output
split when a split boundary falls mid-entry, and once in the trailing

    if (!size && --nb_splits > 0)

The loop-termination check only tests for nb_splits == 0:

    if (!nb_splits)
        break;

When the final requested split ends mid-entry of a non-last input entry,
both decrements run with their bodies skipped: the inner while() takes
nb_splits 1 -> 0, then the trailing if() takes it 0 -> -1. The
termination check then sees -1, not 0, so the loop does not break and the
next input entry is folded into the last split, inflating its nents and
length_last_sg. The resulting split covers more bytes than requested.
For in=[2, 1], skip=0, nb_splits=1, sizes=[1] the single output split
reports a total length of 2 instead of 1.

Widen the termination check to also catch the overshoot to -1:

    if (nb_splits <= 0)
        break;

The new condition only fires when nb_splits is already negative, which is
reachable solely through the double decrement above, so it cannot change
behaviour for any input that splits correctly today.

This was first reported by Alexander Egorenkov in 2021, with an identical
root-cause analysis; that patch was never reviewed or merged. Its proposed
fix moved the existing termination check above the second decrement rather
than widening it. That handles the mid-entry case but regresses
edge-aligned partial coverage -- a split ending exactly on an input-entry
boundary with further entries trailing -- which the current code handles
correctly. A KUnit test added later in this series
(sg_split_t10_edge_first_two_trailing) covers that regression.

No in-tree caller triggers the bug today: four request full coverage or
re-split over a DMA-mapped subset, and the one caller that requests
partial coverage (the DTHEv2 AEAD driver) bounds its consumer by cryptlen,
masking the wrong geometry. The fix matters for callers that legitimately
request partial coverage, which the API has documented as supported since
the original commit ("the union of spans of all resulting scatter lists is
a subrange of the span of the original scatter list").

Fixes: f8bcbe62acd0 ("lib: scatterlist: add sg splitting function")
Reported-by: Alexander Egorenkov <egorenar-dev@posteo.net>
Closes: https://lore.kernel.org/all/20210418143425.22944-1-egorenar-dev@posteo.net/
Assisted-by: Claude:claude-opus-4-8 hegel-c
Signed-off-by: Charles Pellegrini <c4ffein.work@gmail.com>

================================================================================
[PATCH v2 2/5] lib: scatterlist: fix sg_split_phys() ->length clobber on !NEED_SG_DMA_LENGTH
================================================================================

sg_split_phys() builds each output entry by copying the input entry and
then scrubbing the output's DMA view:

    *out_sg = *in_sg;
    sg_dma_address(out_sg) = 0;
    sg_dma_len(out_sg) = 0;

On configurations where CONFIG_NEED_SG_DMA_LENGTH is not selected,
sg_dma_len() is defined as ((sg)->length): there is no separate
dma_length member. The sg_dma_len(out_sg) = 0 line therefore zeroes the
CPU ->length that the function computes for the split. The trailing

    out_sg[-1].length = split->length_last_sg;

restores only the last entry, so for a multi-entry split every non-last
entry is left with length 0 and the split silently exposes fewer bytes
than requested. Where CONFIG_NEED_SG_DMA_LENGTH is selected (any config
with an IOMMU, and arches such as x86, arm64, powerpc, s390 or sparc)
sg_dma_len() touches the distinct dma_length field and ->length is
untouched, so the bug does not manifest.

Compute the length into a local and assign ->length after the DMA scrub,
so the value survives on both configurations:

    unsigned int len = in_sg->length;

    *out_sg = *in_sg;
    sg_dma_address(out_sg) = 0;
    sg_dma_len(out_sg) = 0;
    if (!j) {
        out_sg->offset += split->skip_sg0;
        len -= split->skip_sg0;
    }
    out_sg->length = len;

The sg_dma_len() = 0 scrub is kept deliberately: on NEED configurations it
clears the dma_length copied by *out_sg = *in_sg, which a consumer would
otherwise read as stale. Deleting it would fix the !NEED case but leak a
stale DMA length on NEED; reordering is correct on both.

No in-tree caller triggers this: the only caller that hands sg_split() an
unmapped list and requests a multi-entry partial split (the DTHEv2 AEAD
driver) builds for an arm64 platform, which selects CONFIG_NEED_SG_DMA_LENGTH
and is therefore immune. It was found while correcting an in_mapped_nents
argument in the KUnit suite (next patch), which is what first exercised the
unmapped multi-entry path on a !NEED build.

Fixes: f8bcbe62acd0 ("lib: scatterlist: add sg splitting function")
Assisted-by: Claude:claude-opus-4-8 hegel-c
Signed-off-by: Charles Pellegrini <c4ffein.work@gmail.com>

================================================================================
[PATCH v2 3/5] lib: scatterlist: add KUnit tests for sg_split()
================================================================================

Add a KUnit suite for lib/sg_split.c covering the (input shape, skip,
split sizes) matrix: full coverage, edge-aligned partial coverage,
mid-entry partial coverage at several boundary positions across one to
three input entries, skip-position variants, and multi-split runs.

Each case checks the two documented post-conditions of sg_split(): the sum
of each output split's entry lengths equals the requested split size, and
the bytes exposed by each split match the corresponding region of the
input. Backing memory uses a position-unique byte pattern so a content
mismatch pinpoints which input region was mis-exposed. Each case also
asserts that exactly one entry per split carries the end-of-list marker.

Every case is run twice: unmapped (in_mapped_nents = 0, exercising
sg_split_phys()) and with an identity DMA mapping (dma_len == length,
contiguous synthetic IOVA, exercising sg_split_mapped() and asserting the
DMA addresses and lengths). A further group exercises divergent CPU/DMA
geometry (coalescing, where the DMA segmentation is coarser than the CPU
one); those need a distinct dma_length field and are skipped where
CONFIG_NEED_SG_DMA_LENGTH is not selected.

The mid-entry partial cases fail without the sg_calculate_split() overshoot
fix in patch 1; sg_split_t10_edge_first_two_trailing additionally guards
the edge-aligned case that the 2021 fix proposal regressed. The unmapped
multi-entry cases fail without the sg_split_phys() ->length fix in patch 2.

Run with:

    tools/testing/kunit/kunit.py run sg_split

Assisted-by: Claude:claude-opus-4-8 hegel-c
Signed-off-by: Charles Pellegrini <c4ffein.work@gmail.com>

================================================================================
[PATCH v2 4/5] lib: scatterlist: guard sg_split_phys()/sg_split_mapped() against zero-nents splits
================================================================================

If a caller passes a split_sizes[] array whose trailing entry is 0 and the
preceding splits exactly consume the input scatterlist, sg_calculate_split()
returns success with the trailing split's nents left at 0. sg_split() then
allocates that split's output with

    kmalloc_objs(struct scatterlist, 0, gfp_mask)

which returns ZERO_SIZE_PTR. Both copiers finish each split with a fixup of
the last written output entry -- sg_split_phys() does

    out_sg[-1].length = split->length_last_sg;
    sg_mark_end(out_sg - 1);

and sg_split_mapped() does

    sg_dma_len(--out_sg) = split->length_last_sg;

For a zero-nents split out_sg has not advanced, so that fixup dereferences
ZERO_SIZE_PTR: an out-of-bounds write. KASAN reports it as a slab
out-of-bounds write; a userspace ASAN harness reproduces it as a heap
overflow.

Skip empty splits at the top of the per-split loop in both copiers:

    if (!split->nents)
        continue;

No in-tree caller passes such a split_sizes[] array, so this is latent; it
was surfaced by an automated review of the v1 posting. The fix tolerates
and skips the degenerate split rather than rejecting it with -EINVAL in
sg_calculate_split(); if a stricter contract is preferred, the rejecting
variant is a trivial respin.

Fixes: f8bcbe62acd0 ("lib: scatterlist: add sg splitting function")
Link: https://lore.kernel.org/all/20260601175549.b4a10c07dd9e3b867f66da0c@linux-foundation.org/
Assisted-by: Claude:claude-opus-4-8 hegel-c
Signed-off-by: Charles Pellegrini <c4ffein.work@gmail.com>

================================================================================
[PATCH v2 5/5] lib: scatterlist: add zero-nents regression test for sg_split()
================================================================================

Add sg_split_t8b_trailing_zero_no_input_left: a split_sizes[] array whose
first split consumes the entire input, so the trailing zero-size split
receives no input entry (nents == 0). Without the guard in the previous
patch, the last-entry fixup in sg_split_phys()/sg_split_mapped() writes
through a ZERO_SIZE_PTR; with it, the empty split is skipped.

Run under KASAN to catch a regression as a clean out-of-bounds report,
e.g.:

    tools/testing/kunit/kunit.py run sg_split \
        --arch=x86_64 --kconfig_add=CONFIG_KASAN=y

Without KASAN the write lands at a wild address and faults, so the case
still fails -- KASAN just turns it into a precise report rather than an
oops.

Assisted-by: Claude:claude-opus-4-8 hegel-c
Signed-off-by: Charles Pellegrini <c4ffein.work@gmail.com>
