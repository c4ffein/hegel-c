To: Andrew Morton <akpm@linux-foundation.org>
Cc: Alexander Egorenkov <egorenar-dev@posteo.net>,
    Robert Jarzmik <robert.jarzmik@free.fr>, t-pratham@ti.com,
    David Gow <david@davidgow.net>,
    linux-kernel@vger.kernel.org
Subject: Re: [PATCH v2 3/5] lib: scatterlist: add KUnit tests for sg_split()
In-Reply-To: <GET-FROM-RAW-MAIL: akpm's 2026-06-28 message-id>

> I guess this is all forgotten about.

Not forgotten -- sorry for the gap. v3 follows, rebased on v7.2-rc1.

> Now it's -rc1, please incorporate any feedback, check the Sashiko
> feedback
> (https://sashiko.dev/#/patchset/178113124324.90620.15726328110868523164@gmail.com),
> refresh, retest and resend?

Checked. The review's one finding is real, pre-existing, and a fourth
member of the same zero-size family as the out-of-bounds write fixed in
v2: when a zero split size lands exactly at input exhaustion with
non-zero sizes still pending, sg_calculate_split() exits its loop with
size == 0 and returns success, silently leaving the trailing splits
unfulfilled. Minimal reproducer: a 1-byte input with
split_sizes = {1, 0, 1} returns 0 with split[2] empty. On kernels
without v2's guard the same input hits the ZERO_SIZE_PTR out-of-bounds
write instead, so the bogus success was never observable -- it would
have become observable precisely with v2 applied as-is.

That settled the tolerate-vs-reject question v2 patch 4 left open, in
favor of rejecting: there is no good empty result for a zero-size
split to return (the CPU view of a scatterlist has no empty
representation, and the DMA view's documented sg_dma_len() == 0
terminator would need an allocated entry whose CPU view is then a
zero-length element); no in-tree caller can pass a zero size (the
two whose sizes could arithmetically reach zero already branch around
the call); and the shapes tolerating would hand back are not accepted
downstream. v3 patch 4 is that rejection. I implemented and tested the
tolerate variant too -- its diff is in the cover letter if you would
rather keep the looser contract.

Changes are listed in the cover. Series re-tested on UML and
x86_64+KASAN, checkpatch --strict clean.
