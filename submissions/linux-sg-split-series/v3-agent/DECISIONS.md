# v3 decisions that change the contract of sg_split(): needs human decision

Each item below changes what a caller of `sg_split()` sees, or how the series is presented.
I made a choice for each so that v3 is complete and tested. You may overrule any of them.
Where a choice can be reverted on its own, the item says so and lists what else would have to change.

---

## D1. A zero-size split that starts at the very end of the input (F2, Sashiko #5/#7) — needs human decision

Inputs: `in=[10], sizes={10,0}` (KUnit T8b), and `skip == total` with `sizes={0,0}` (T8c).

| Option | What callers get | Cost to callers |
|---|---|---|
| (a) v2: skip it in the copiers | rc 0. `out[k]` is `ZERO_SIZE_PTR`, with no entry and no end marker. `out_mapped_nents[k] == 0`. | Any caller that walks `out[k]` (`sg_nents`, `sg_next`, `sg_copy_*`, `sg_miter`, `dma_map_sg(…, sg_nents())`) dereferences 0x10. `out_mapped_nents` may be NULL for unmapped input, so the caller can't always tell. The result also depends on the input's shape: T8 (`in=[10,5]`) returns a real 1-entry list for the same request. |
| (b) `-EINVAL` for any split that gets no entry | The call fails. | A request the kernel-doc defines as valid gets rejected, but only when the split falls at the very end of the input. A zero-size split anywhere else keeps working. |
| **(c) chosen: one zero-length, end-marked entry** | rc 0. `out[k]` has 1 entry with `length == 0` (and `dma_len == 0` if mapped). `out_mapped_nents[k] == 1`. | None that I can see. It is the shape a zero-size split already gets everywhere else (T8, and mid-entry). `out_mapped_nents[k]` goes from 0 to 1 for this case. |
| (d) document `ZERO_SIZE_PTR` | Same as (a), plus a documented rule. | Every caller must special-case it. |

**Why (c):**
- It makes "every `out[k]` of a successful call is a scatterlist" true without exceptions.
- It does not reject a request the kernel-doc allows.
- The fix lives in `sg_calculate_split()`, so the copiers need no guard.

**Detail to check.** The synthesized entry is a copy of the *last input entry*, with `offset += length` and `length = 0`. Its CPU address is one past that entry's last byte, and `offset` can reach or exceed `PAGE_SIZE`. Mid-entry splits already produce such offsets, both upstream and in v2, for entries larger than a page. P-ZEROSPLIT checks that both the CPU address and the DMA address equal the stream position.

**If you prefer (b):**
- In 3/6, return `-EINVAL` instead of synthesizing the entry.
- T8b and T8c must then move to `sg_split_run_einval()`.
- P-ZEROSPLIT would need its "valid" filter changed.

---

## D2. Past-the-end requests are rejected (F1) — needs human decision (low risk)

Inputs: `skip + Σ sizes > total` in the shape where a zero-size split is reached as the input runs out (`sizes={10,0,5}` on 10 bytes).
- v2 returned 0. Before v2, it wrote out of bounds.
- v3 returns `-EINVAL`, as every other past-the-end request already did.

There is no real alternative: the kernel-doc defines `out[k]` by a byte range that does not exist. This is listed only because rc changes from 0 to -EINVAL for these inputs.

---

## D3. No zero-length first entry (F3, new patch 4/6) — needs human decision

Inputs:
- a split with `size > 0` that starts exactly at the end of an input entry (`skip` on a boundary: T13, T6), or
- a split that starts at an empty input entry (T17).

v2 and upstream return a leading `{length 0}` entry, with `dma_len 0` when mapped. v3 drops it, so `out_mapped_nents[k]` is one less and the list one entry shorter. The bytes are identical.

| Option | Cost |
|---|---|
| keep the leading entry and document it | A DMA consumer that stops at the first `sg_dma_len() == 0` (allowed by `scatterlist.h`) sees the split as empty. Callers that count entries get one wasted entry. |
| **drop it (chosen)** | A caller that hard-coded the old entry count for such a request would see one entry fewer. No in-tree caller is known to (UNCHECKED: no tree here). |

**Why:** `scatterlist.h` documents the stop-at-zero DMA convention. The fix is one condition and touches only those inputs: the step differential s3→s4 changed exactly the E3 inputs.

**Patch 4/6 can be dropped on its own:**
- 5/6 does not touch its lines.
- 6/6's T6, T13 and T17 would fail, and the "non-empty split starts with an entry that has bytes" check in `sg_split_check_shape()` would have to go.
- This is pre-existing and not something the maintainer asked about. You may prefer to send it separately after this series.

**Fixes: tag.** 4/6 carries `Fixes: f8bcbe62acd0`. Whether this counts as a bug for stable purposes is your call. I did not add `Cc: stable` anywhere.

---

## D4. `nb_splits <= 0` and `skip < 0` → `-EINVAL` (F5, new patch 5/6) — needs human decision

| Argument | Before | Options | Chosen |
|---|---|---|---|
| `nb_splits == 0` | out-of-bounds read/write on a 0-byte array | return 0 (nothing to do), or `-EINVAL` | **`-EINVAL`** |
| `nb_splits < 0` | `-ENOMEM` (the allocation size overflows) | `-EINVAL` | **`-EINVAL`** |
| `skip < 0` | rc 0 and `out[0]` starting before the input (probe: offset 97 for an entry at offset 100, `skip=-3`) | `-EINVAL` | **`-EINVAL`** |

**Why `-EINVAL` for 0:**
- No working caller can exist today: any call with 0 faults or corrupts memory.
- "Split into zero lists" is a caller bug rather than a request worth serving.
- If you prefer "0 is a no-op success", move the `nb_splits == 0` case to `return 0` before the allocation and change T18 and P-ARGS accordingly.

---

## D5. Series layout: KUnit suite last (6/6) — needs human decision

v2 put the suite at 3/5, between fixes, and added the zero-nents test at 5/5. v3 puts the suite, with all its cases, after every fix.

- **Benefit:** no commit has a red test, so the series stays bisectable for the suite too. All new cases need 3/6–5/6.
- **Cost:** the fixes do not carry their regression test in the same commit. 6/6's message lists which cases fail with each fix reverted, as checked by the replay.

The alternative is to put the suite back at 3/6 and add each new case with its fix. That gives more patches and more churn.

---

## D6. Tags and placeholders — needs human decision

**`Assisted-by:` tags.**
- 1/6, 2/6, 3/6 and 6/6 keep v2's `Assisted-by: Claude:claude-opus-4-8 hegel-c` and add `Assisted-by: Claude:claude-opus-5-5 hegel-c` for this respin.
- 4/6 and 5/6 are new, so they only carry the second tag.
- Adjust as you see fit.

**No `Signed-off-by:` anywhere.** You have to add yours (`git am` then `git commit --amend -s`, or `format-patch -s`).

**Cover letter placeholders:**
- The "Testing" paragraph has a `[HUMAN: …]` block. The KUnit-in-kernel runs (UML and x86_64+KASAN) were not done in this respin: the suite was only compiled and run in userspace through a shim.
- The v2 lore link needs the v2 cover letter's message-id.

**3/6 `Link:`.** It keeps v2 4/5's `Link:` to Andrew's mail. Check that it still applies.

**Generated headers.** Index lines are real git blob hashes, computed here. s1/s2 match v2's `28fe7fa6c9ac` and `ab574dd26b03`, so 1/6 and 2/6 are byte-identical in code to v2 1/5 and 2/5. The `From <sha>` lines are zeros and there is no `Date:`. Regenerate the patches with `git format-patch` after `git am` if you want real ones.

---

## D7. Claims kept from v2 that could not be checked here

These need a human eye, since there are no callers, no Kconfig and no network in this workspace:
- "no in-tree caller triggers it", and the DTHEv2 description (1/6, 2/6, 3/6);
- "any config with an IOMMU" selects `NEED_SG_DMA_LENGTH` (2/6);
- Egorenkov's 2021 report and link (1/6).

I dropped the "no in-tree caller" sentence from the new 5/6 because I could not check it.
