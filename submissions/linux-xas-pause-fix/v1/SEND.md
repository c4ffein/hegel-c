<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# xas_pause() sibling-slot fix — send notes

Patch: `0001-xarray-fix-xas_pause-skipping-entries-from-a-sibling.patch`
(against v7.3, c03c1a14507d; applies cleanly to linux-next as of 2026-10-22 —
xas_pause() is identical there).

**Related fix already in akpm's tree (seen 2026-10-22):** f0f80e85841d
"xarray: fix index jumping backwards in xas_find()" (Krystian Kaniewski,
lore 20260904121301.200049-1-krystianmkaniewski@gmail.com, syzbot, Cc stable).
Same root cause in a different function: xa_offset left at the multi-index
entry's canonical slot while xa_index sits in a sibling slot — and the same
`Fixes: b803b42823d0`. Our patch applies on top of it (test hunks don't
collide). Worth one sentence in the changelog ("xas_find() had the same
offset/index mismatch, fixed by f0f80e85841d; xas_pause() still has it") and
a Cc to Krystian.

## Before sending (yours to do)

1. **Search lore** (browser — lore is behind a bot check):
   https://lore.kernel.org/all/?q=xas_pause — make sure nobody posted this
   fix since 2026-10-19.
2. **Read the whole patch and changelog** — under the kernel's
   AI-assistance rules (Documentation/process/coding-assistants.rst and
   generated-content.rst) you certify it and must be able to answer review.
3. **Add your Signed-off-by** after the `Assisted-by:` line:
   `Signed-off-by: Charles Pellegrini <c4ffein.work@gmail.com>`
   (the assistant must not add it). Adjust the `Assisted-by:` and the
   tool-disclosure paragraph if you want them worded differently.
4. Optional: `Cc: stable@vger.kernel.org` — the bug dates from 2017 (v4.20
   era), so it is in every maintained stable tree. akpm often adds it
   himself; leaving it out is fine.

## Recipients

From MAINTAINERS (XARRAY) and c9ba5249's history — get_maintainer.pl was
not run (the sparse checkout has no scripts/):

    To: Matthew Wilcox <willy@infradead.org>
    To: Andrew Morton <akpm@linux-foundation.org>    (c9ba5249 went in via mm)
    Cc: Kemeng Shi <shikemeng@huaweicloud.com>        (author of c9ba5249)
    Cc: Krystian Kaniewski <krystianmkaniewski@gmail.com>  (author of f0f80e85841d, same mechanism)
    Cc: linux-mm@kvack.org
    Cc: linux-fsdevel@vger.kernel.org
    Cc: linux-kernel@vger.kernel.org

## What was verified

- Bug present in mainline v7.3 and linux-next (2026-10-21).
- New check in check_pause() (lib/test_xarray.c): RED without the fix
  (`check_pause: Assertion` at the `count != 2` line — index 2 skipped),
  GREEN with it.
- tools/testing/radix-tree at v7.3, ASan + UBSan, with the fix:
  xarray (159585935 checks), idr-test (23564609), multiorder, maple, main —
  all pass.
- checkpatch.pl --strict: only the missing Signed-off-by (yours), the
  unverifiable Fixes id (--no-tree; verified b803b42823d0 added xas_pause),
  XA_BUG_ON "do not crash" warnings (the file's own assert macro, 425 uses)
  and a `!= NULL` check (same form 12 times in the file).
- Threat model: a regular bug (iterator skips an entry, no boundary
  crossed shown) — normal submission, not security@.

## What was NOT done (say so if asked)

- No in-kernel run (KUnit / booted test_xarray module) — userspace harness only.
- No analysis of which in-tree callers can hit it (needs a walk that starts
  inside a multi-index entry of a leaf node and pauses on that first entry;
  large page-cache folios are multi-index entries, so filemap/shmem walkers
  are the place to look).
- get_maintainer.pl not run.

## Provenance

hegel-c pin `tests/irl/linux/pins/xarray_pause.conf` (buggy / ours-fixed /
upstream-c9ba5249 states); the harness is `tests/irl/linux/xarray_pause/`.
