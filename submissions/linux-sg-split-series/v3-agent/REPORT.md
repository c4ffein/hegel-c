<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# sg_split v3 (agent variant): final report

This is the series a blind agent wrote, then tested further and fixed by us.

Status: **ready for human review, not ready to send.** Six patches plus a cover letter, in this folder.
There is no Signed-off-by anywhere. Every place that needs you is marked `[HUMAN: …]`.

## 1. Decision you need to make first: D1

Case: a split of size 0 that starts at the very end of the input, e.g. `in=[10], sizes={10,0}`.

| | This series (agent's choice) | Our own v3 |
|---|---|---|
| Result | rc 0, `out[k]` = one entry with length 0, end-marked, `out_mapped_nents[k] = 1` | `-EINVAL` |
| For | Every successful call returns a real list. The kernel-doc allows the request. A zero-size split anywhere else already gets this same shape. | Simpler: no made-up entry. |
| Against | The made-up entry's `offset` can reach `PAGE_SIZE` or more. Mid-entry splits already produce such offsets, upstream and in v2. | Rejects a request the kernel-doc allows, but only at the end of the input. |

To switch to `-EINVAL`, change three things (from `DECISIONS.md` D1):
- the one branch in 3/6;
- move KUnit T8b and T8c to `sg_split_run_einval()`;
- the "valid" filter of P-ZEROSPLIT.

Other decisions to skim: D2–D6 in `DECISIONS.md`. D3 matters most: 4/6 drops the zero-length first entry. It can be dropped on its own or sent separately.

## 2. What changed from the agent's series

1. **5/6 commit message, Sashiko Low #1.** The negative-skip bug only exists on 64-bit. On 32-bit, `off_t` (a long) compared with `unsigned int` is an unsigned comparison, so the old code already returned `-EINVAL`. Both claims were checked on the pre-fix code with the 64-bit and the static i386 builds:
   - `skip=-3` on a 10-byte entry at offset 100: 64-bit gives rc 0, with the split at position 97; 32-bit gives rc -22.

   The cover letter's bug 5 now says this too.
2. **6/6 KUnit, Sashiko Low #2.** The divergent cases now assert `sg_nents(out[k]) == cpu_nents[k]` before walking the entries. Before, a wrong count could make the walk read past the split. Run against the original unpatched code, the new assert fires (T `sg_split_d3_coalesce_3to1_skip`: wrong number of CPU entries).
3. **Author.** Kept as `Charles Pellegrini <c4ffein.work@gmail.com>`, the From: of the v2 you sent.
4. **Cover letter Testing paragraph** now describes the extended testing in §3. The `[HUMAN: …]` block for the real KUnit runs is still there.
5. Patches regenerated with `git format-patch`, based on 08d0d3466 (net-7.1-rc2 merge). To rebuild the branch: `git am v3-000[1-6]*.patch` on that commit. The code in 1/6–5/6 is unchanged from the agent's: `lib/sg_split.c` is byte-identical.

## 3. Testing done (userspace, against this series)

| What | Configs | Result |
|---|---|---|
| Properties: core, walkable, nozero (+mapped), geometry, invalid, error paths, args, zero split, no leading zero entry | !NEED / NEED × plain / ASan+UBSan | all pass |
| Bigger shapes: up to 48 entries, 4 chained tables, 16 splits; exact-size heap arrays for `sizes`/`out` | same | all pass |
| DMA: any grouping an in-tree `map_sg` can produce, over a contiguous IOVA buffer; DMA addresses checked | NEED | all pass |
| Huge values on fake pages: lengths up to `UINT_MAX`, totals past 4 GiB, extreme `skip`/sizes | 5000 cases plain, 1500 under ASan | pass (entries with in-page offset + length > `UINT_MAX` are excluded; no kernel allocator builds them) |
| 64-bit vs i386 build of the same `sg_split.c`, identical output required | 5000 cases | pass |
| Differential v2 vs v3: identical output required outside the changed input classes | 6000 cases | pass |
| KUnit suite through the userspace shim | 4 configs | 25/25 |
| Mutation testing, 33 mutants of the 4 functions, whole suite | NEED and !NEED, ASan | 32/33 caught in both. The survivor (L88 `> 0` → `>= 0`) cannot change behaviour, since `nb_splits <= 0` is rejected at entry |
| checkpatch --strict | | only the missing SOB, plus 2 alignment CHECKs in 6/6 that were already in the agent's version |

## 4. Not done (before sending)

- **Item 5:** real-kernel KUnit run (UML !NEED, x86_64 + KASAN). Then replace the cover letter's `[HUMAN: …]` Testing block with the real numbers.
- **Item 7:** claims kept from v2 that nobody checked here (DECISIONS.md D7):
  - "no in-tree caller triggers it";
  - the IOMMU → `NEED_SG_DMA_LENGTH` claim;
  - the 2021 report link.
- No 32-bit ASan run. The fake-page tests check positions and lengths, not bytes. There is no real `dma_map_sg`/IOMMU.
- Fill in:
  - the v2 message-id in the cover letter;
  - your Signed-off-by (`git am` then `git commit --amend -s`, or `format-patch -s`);
  - optionally fix the 2 alignment CHECKs.
- Sashiko hasn't re-run on these final patches. Its 2 Lows were small, but a re-run is cheap.

## Files (this folder)

- `v3-0000..0006-*.patch`: the cover letter and the 6 patches.
- `TRIAGE.md`, `DECISIONS.md`, `EVIDENCE.md`: the blind agent's own notes.
- `evidence/`: the tests, mutation drivers and logs behind §3, plus the blind replay's answer key (see its README).
- `../v3-ours/`: our own v3 (5 patches, the `-EINVAL` side of D1), built 2026-07-06, unsent.
