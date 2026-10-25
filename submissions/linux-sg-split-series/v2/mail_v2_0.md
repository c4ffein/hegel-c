To: Andrew Morton <akpm@linux-foundation.org>
Cc: Alexander Egorenkov <egorenar-dev@posteo.net>,
    Robert Jarzmik <robert.jarzmik@free.fr>, t-pratham@ti.com,
    linux-kernel@vger.kernel.org
Subject: Re: [PATCH 0/2] lib: scatterlist: fix sg_split() partial-coverage geometry + add KUnit tests
In-Reply-To: <20260601175549.b4a10c07dd9e3b867f66da0c@linux-foundation.org>
References: <178027099087.72481.1976843064458686851@gmail.com>
 <20260601175549.b4a10c07dd9e3b867f66da0c@linux-foundation.org>

On Mon, 1 Jun 2026 17:55:49 -0700 Andrew Morton <akpm@linux-foundation.org> wrote:

> Thanks.  Has this been observed in real life, or was the patch motivated by
> code review?

Not observed in real life.
I was crafting a pure C client for the Hegel PBT library (and related
bug-hunting Claude skills). I used known kernel bugs to benchmark various
interfaces and skills. Crafting Hegel tests for sg_split.c led Claude to
report this one to me. I only realized it was actually already reported
later.

> AI review
> (https://sashiko.dev/#/patchset/178027099087.72481.1976843064458686851@gmail.com)
> might have found a pre-existing issue, which you may choose to reject,
> fix, add to todo list or ignore.  Also a possible issue in the kunit
> test changes.
>
> Please take a look, let us know?

I took a look; it raised more questions and led to more changes:

---

1: The first comment noted that the existing code has no guard against a
split_sizes array ending with a 0 size
This is arguably the caller's responsibility, but since it's an
out-of-bounds write in an exported function, I lean toward guarding it
here.
A trailing zero-size split that receives no input entry leaves
out_sg == ZERO_SIZE_PTR, and sg_split_phys()/sg_split_mapped() then write
out_sg[-1].length: an out-of-bounds write.
Confirmed with KASAN, and with a userspace ASAN harness.
Fix: skip empty splits in both copiers.
(It needs a caller to request a trailing zero-size split with no input
left; no in-tree caller does, so it's latent.)

2: The second comment flagged an issue in the kunit suite
The test passes in_mapped_nents = n_in for an unmapped list, where it must
be 0.
On NEED_SG_DMA_LENGTH arches, the mapped pass then reads a zeroed
dma_length and returns -EINVAL.
It passed for me only because UML aliases dma_len onto ->length.
Fix: pass 0.

3: The test finding also turned up a separate pre-existing bug,
also latent.
On !NEED_SG_DMA_LENGTH arches sg_dma_len() aliases ->length, so
sg_split_phys() zeroes, via sg_dma_len(out_sg) = 0, the length it has just
computed.
The trailing out_sg[-1].length assignment restores only the last entry,
so for the non-last entries of a multi-entry split the zero sticks ->
silent short data.
Fix: write ->length after the sg_dma_len() = 0. Verified in-kernel (UML
and x86_64+KASAN) and under ASAN.
(No in-tree caller hits it: the sole unmapped caller, DTHEv2, runs on
arm64 = NEED, which is immune.)

---

v2 grows to 5 patches.
1. the overshoot fix: the exact same initial patch
2. the additional size computation fix
3. the base test suite, improved
  - fixed the second comment from sashiko
  - each case now run both unmapped and identity-mapped
  - added DMA-address + end-marker assertions and NEED-gated
    divergent-geometry cases
4. the zero-size OOB guard
5. its associated regression test

That way, patches 4 and 5 can be skipped if you think we should actually
keep the responsibility on the caller.
The automated review prompted patches 4 and 5 and the test-arg fix in 3;
chasing that fix is what surfaced 2. I'll note that in the commit messages.

I went with tolerate-and-skip for the empty trailing split rather than
rejecting it with -EINVAL, reasoning it's the droppable tail of the
request.
Happy to respin that one patch to -EINVAL if you'd prefer the stricter
contract -- trivial either way.

I'll send v2 threaded under this series shortly.

Thanks,
Charles
