<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Manifest & background — sg_split submission

## What ships upstream (sent via `git send-email`)

- `0000-cover-letter.patch` — `[PATCH 0/2]` cover letter / intro mail.
- `0001-lib-scatterlist-fix-sg_calculate_split-nb_splits-ove.patch` —
  the one-line fix (`if (nb_splits <= 0) break;`). `Fixes:`
  f8bcbe62acd0, `Reported-by:` Alexander Egorenkov.
- `0002-lib-scatterlist-add-KUnit-tests-for-sg_split.patch` —
  `lib/test_sg_split_kunit.c` (16 cases) + `lib/Kconfig` +
  `lib/Makefile`. GPL-2.0.

## What does NOT ship (local only)

- `INSTRUCTIONS.md` — how to apply, verify, and send.
- `files.md` — this file.

## Key facts baked into the series

- **Bug:** `sg_calculate_split()` decrements `nb_splits` twice per outer
  iteration but the loop-termination check only tests `!nb_splits`. On
  partial coverage ending mid-entry of a non-last input entry, the
  counter overshoots `1 -> 0 -> -1`, the loop runs one iteration too
  many, and a trailing input entry is folded into the last split.
- **Fix:** widen the check to `nb_splits <= 0`. One character of
  behavioural change; only fires on the already-negative overshoot.
- **Prior art:** Alexander Egorenkov reported this in 2021
  (`20210418143425.22944-1-egorenar-dev@posteo.net`), with the same
  diagnosis. It got **zero review** and was never merged. His proposed
  fix (move the check above the second decrement) **regresses
  edge-aligned partial coverage** — test `sg_split_t10` guards exactly
  that, which is the concrete argument for the `<= 0` form over his.
- **Impact:** no in-tree caller triggers it today (4 of 5 use full
  coverage / DMA-mapped re-split; DTHEv2 does partial coverage but
  re-clamps downstream by `cryptlen`). It's a latent correctness bug for
  legitimate partial-coverage callers.
- **Verification:** 9 pass / 7 fail on mainline, 16/16 with the fix. The
  same matrix runs as a userspace harness in `tests/irl/sg_split/` of
  the hegel-c repo.

## Relationship to `../message_paul_2/`

`message_paul_2/` is the earlier, reviewer-oriented writeup (a "please
sanity-check this" letter to a kernel-savvy human). It predates the
discovery of Egorenkov's 2021 patch, so it does **not** mention the
prior art or the `t10` regression argument — both of which are now
folded into the cover letter and patch 1 changelog here. The technical
analysis below is reproduced for background; where it and this series
disagree, **this series wins**.

The full background letter from `message_paul_2/MESSAGE_CLAUDE.txt`
follows verbatim.

---

## Appendix: `message_paul_2/MESSAGE_CLAUDE.txt` (background, not for upstream)

```text
Hey,

I'm Claude (Anthropic's coding assistant). c4ffein had me chew on lib/sg_split.c and we found something. Before they post a
patch I'd love a sanity check from someone who occasionally touches the kernel.

TL;DR. sg_calculate_split has a dual-decrement / single-zero-check bug that corrupts the output SG geometry whenever a
caller asks for partial coverage ending mid-entry of a non-last input entry. Live on torvalds master (e7ae89a0c97c,
2026-05-24). Five in-tree callers; none seem to actually trigger it. Real correctness bug, probably not exploitable in any
current path.

The bug. nb_splits gets decremented in two places per outer-loop iteration:

while (!size && (skip + len < sglen) && (--nb_splits > 0)) { ... }   // (1)
...
if    (!size && --nb_splits > 0) { ... }                              // (2)
...
if (!nb_splits) break;                                                 // (3)

Both (1) and (2) are decrement-and-guard. When the iteration just finished the last requested split AND the current input
entry has leftover bytes AND there's another input entry after — (1) takes nb_splits 1→0 with body skipped, (2) takes it
0→-1 with body skipped, (3) checks !(-1) == false, loop doesn't break, next iteration corrupts the last split's nents /
length_last_sg.

Minimal repro: in=[{len=2}, {len=1}], skip=0, nb_splits=1, sizes=[1] → split[0] has total length 2, not 1.

Why nobody's hit it. All five callers (drivers/crypto/hisilicon/sec/sec_algs.c, drivers/crypto/sa2ul.c,
drivers/crypto/ti/dthev2-aes.c, drivers/media/platform/intel/pxa_camera.c, drivers/spi/spi-omap2-mcspi.c):

- Three of them (SEC, pxa_camera, McSPI) build sizes so sum == total. Bug impossible by construction.
- SA2UL passes in_mapped_nents != 0, which forces a two-pass through sg_calculate_split; the second pass walks only the
mapped subset, hits exact coverage, and overwrites the buggy first-pass state.
- DTHEv2 AEAD does partial coverage (skip=AAD, take cryptlen, leave TAG) — but its consumer (dthe_copy_sg) bounds reads by
cryptlen, so even bad SG geometry doesn't propagate. Plus its typical layout puts the cipher boundary at an SG-entry edge,
which doesn't even fire the bug (condition 1 short-circuits before the first decrement).

Real bug, but the trigger ∩ caller blast-radius ∩ unmasked-downstream set is currently empty.

The contract. Kernel-doc above sg_split documents per-split byte ranges and says nothing about whether sum(sizes) has to
equal total. The original commit message (f8bcbe62acd0, Robert Jarzmik, 2015) lists the design guarantees, including: "the
union of spans of all resulting scatter lists is a subrange of the span of the original scatter list." Word choice is
"subrange," not "equal" — partial coverage was an intentional feature from day one, and DTHEv2's AAD/cryptlen use is the
canonical legitimate case. The bug breaks the per-split size guarantee exactly when callers exercise that clause.

The fix.

 if (!size && --nb_splits > 0) {
     curr++;
     size = *(++sizes);
 }

-if (!nb_splits)
+if (nb_splits <= 0)
     break;

One character. The existing break check missed the overshoot to -1 caused by the dual decrement; widening it to <= 0
catches the overshoot. Touches no other path, cannot change behaviour for any input that succeeded before — the modified
condition only triggers when nb_splits is already negative, which is reachable exclusively through the bug.

How we got here. First attempt was a guard at the top of the body: `if (!size) continue;`. It passed the mid-entry partial
cases but regressed a sizes-containing-zero edge: sizes=[10, 0] with in=[{len=10}, {len=5}]. The outer-if transition sets
size = sizes[1] = 0 at end of iter 1; iter 2 then enters with size=0 and the proposed guard short-circuits before the body
initializes splitter[1] (the existing code relies on the next-iteration body to populate `in_sg0`/`nents` via the
`!curr->in_sg0` branch). splitter[1] stays {in_sg0=NULL, nents=0}, kmalloc_objs(0) returns ZERO_SIZE_PTR, sg_split_phys
runs `out_sg[-1].length = ...` against the un-iterated pointer → abort / OOB. Pre-patch code handles sizes-with-zero
degenerately (zero-length split, return 0); the guard-at-top fix turns it into a crash. The break-condition fix doesn't
touch that path.

Verified locally — 16 deterministic test cases covering the (skip, sizes, input-shape) matrix: full coverage, edge-aligned
partial, mid-entry partial (3 boundary positions × 1-3 input entries), skip-position variants (skip=0, at-exact-entry-edge,
mid-entry-fastforward), and multi-split with mid-entry transitions.  Plus a sizes-containing-zero regression check.
Current upstream: 7/16 fail (the mid-entry partial cases — exactly the ones where `skip + sum(sizes)` lands strictly
inside a non-last input entry).  With `if (!size) continue;`: 15/16 pass and the 16th aborts on the sizes-containing-zero
case.  With `if (nb_splits <= 0) break;`: 16/16 pass.

KUnit version is also included for the upstream submission — same 16 cases expressed as `KUNIT_CASE`s in
test_sg_split_kunit.c.  Drops into lib/test_sg_split_kunit.c with a Kconfig + Makefile entry (see KUNIT_INTEGRATION.txt).
Runs via `tools/testing/kunit/kunit.py run sg_split`.

Where I'd love you to push back:

1. Is "subrange (not equal)" the consensus reading of the contract? If you'd read the kernel-doc cold and assumed "must sum
to total," that's a signal the doc needs to grow the guarantee too, not just the code.
2. Did I miss a caller? `git grep -nw '\bsg_split\b' -- '*.c' '*.h'` gave me 5 hits, all in drivers. The symbol is
EXPORT_SYMBOL (not _GPL), so closed-source modules can use it and won't appear in any grep — vendor GPU/storage drivers
plausibly call this for chunked DMA, but I can't see their source. The in-tree analysis is therefore a lower bound on
real-world impact.
3. Is the sizes-containing-zero degenerate case (which the break-condition fix preserves) worth defining explicitly in the
kernel-doc, or worth rejecting with -EINVAL? Pre-patch behaviour is to produce a zero-length split for sizes[k]=0 and
return success. That's undocumented but observable. Reasonable to leave as-is in this patch and address contract clarity
separately.
4. Posting strategy: file is orphan, no MAINTAINERS entry. Planning Cc to T Pratham (latest contributor, DTHEv2 author),
Robert Jarzmik (original author), Marek Szyprowski (DMA mapping), Herbert Xu + linux-crypto (3 of 5 callers are crypto),
linux-kernel. Anyone obvious missing?

Caveats: I'm an LLM, c4ffein is the one who'll actually post. The trace and reproducer I'm rock-solid on. The "is this
exploitable" assessment is the squishiest part — it's an enumeration over 5 drivers and that's exactly where a human pair
of eyes beats another round of me looking. Also, fwiw, c4ffein caught the proposed-fix regression by asking the right
question after I'd already committed to the wrong fix — appreciate that the value of this kind of review is exactly that.

Attachments:
- sg_split_current.c — current upstream lib/sg_split.c, B2 (offset-reset, 8b46fdaea819) already applied
- sg_split_fixed.c — current upstream + one-character break-condition fix
- test_sg_split_cases.c — 16 hand-crafted cases (userspace harness; fastest to iterate on)
- test_sg_split_kunit.c — same 16 cases as a KUnit suite, for upstream submission
- KUNIT_INTEGRATION.txt — Kconfig + Makefile snippets for landing the KUnit file in lib/

Thanks!
— Claude
```
