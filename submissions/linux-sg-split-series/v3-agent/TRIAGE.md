# v3 triage: every item from the v2 review

**Sources**
- `work/REPORT.md` and `work/CLAIMS.md` (property review: F1–F5 and the commit-message claims)
- `work/SASHIKO_REVIEW_v2.txt` (S1–S10)

**Patch numbers are v3:**

| Patch | Subject |
|---|---|
| 1/6 | overshoot |
| 2/6 | `->length` clobber |
| 3/6 | end-of-input splits |
| 4/6 | zero-length first entry |
| 5/6 | argument checks |
| 6/6 | KUnit |

All evidence logs are in `work/v3/logs/`. Commands are in `EVIDENCE.md`.

## Property-review failures

| Item | Status | Where / evidence |
|---|---|---|
| **F1** past-the-end request accepted (`[1]`, `sizes=[1,0,1]`) | **fixed** | 3/6: `sg_calculate_split()` checks every split not yet placed when the input runs out.<br>Original `P-INVALID` passes in all 4 configurations (`orig_props_v3_*`).<br>`test_diff_v3` asserts `-EINVAL` for every invalid request.<br>KUnit T8d and T8e.<br>Contract note in DECISIONS D2. |
| **F2** zero-size split at the end returns a 0-entry "list" (`ZERO_SIZE_PTR`) | **fixed** | 3/6: such splits get one zero-length, end-marked entry.<br>Original `P-WALK` passes.<br>New `P-ZEROSPLIT` (`test_v3_new.c`) passes.<br>KUnit T8b and T8c.<br>Contract change in DECISIONS D1.<br>This intentionally changes `P-GEOM`'s expectation (EVIDENCE §2). |
| **F3** skip on an entry boundary gives a leading zero-length / `dma_len 0` entry | **fixed** | New 4/6.<br>Original `P-NOZERO` and `P-NOZERO-MAPPED` pass.<br>New `P-NOLEAD` also covers empty input entries.<br>KUnit T6, T13 and T17 assert it.<br>Contract change in DECISIONS D3.<br>Intentional `P-GEOM` expectation change (EVIDENCE §2). |
| **F4** old code: mapped-pass overshoot is a heap overflow | **fixed** (by 1/6, code unchanged since v1); **message fixed** | 1/6's message now describes the mapped-pass heap overflow with the [5,5,5] / DMA [10,5] example.<br>`claims_v3_need_san p1_coalesced_mapped_overshoot`: "every split matches kdoc", no ASan report (`logs/claims_v3.log`).<br>KUnit D3 fails with 1/6 reverted on NEED (`logs/kunit_replay_v3_minus_each_patch.log`). |
| **F5** `nb_splits == 0` reads/writes out of bounds | **fixed** | New 5/6: `-EINVAL` for `nb_splits <= 0` and `skip < 0`.<br>New `P-ARGS` passes, including under ASan.<br>KUnit T18.<br>`work/v3/probe_args.c`: v2 gives an ASan heap-buffer-overflow for `nb=0`, `-ENOMEM` for `nb=-1`, and rc 0 with `out[0]` 3 bytes before the input for `skip=-3`. v3 gives `-EINVAL` for all three.<br>Contract change in DECISIONS D4. |

## Commit-message / cover-letter inaccuracies (REPORT "Commit-message inaccuracies" + CLAIMS)

| Item | Status | Where / evidence |
|---|---|---|
| 1/5 (CLAIMS 1.3 / C0.4): "the next input entry is folded" | **fixed** | 1/6 now says every remaining entry is appended, with the `{2,1,0}` example. The cover letter says "folds every remaining input entry". |
| 1/5 (1.3): `length_last_sg` "inflated" | **fixed** | 1/6: it drops to 0, and the over-coverage comes from entries that keep their full length. Also Sashiko S8(2). |
| 1/5 (1.4): `[2,1]` → 2 holds only on NEED (0 on !NEED) | **fixed** | 1/6 says "With CONFIG_NEED_SG_DMA_LENGTH …", and adds the `{0,0}` result on !NEED. |
| 1/5 (1.7, F4): only consequence stated is over-coverage | **fixed** | 1/6 adds the mapped-pass heap overflow paragraph. |
| 1/5 (1.11): kernel-doc quote about partial coverage is not in the tree | **fixed** | 1/6 replaces it with what the kernel-doc does say (per-split byte ranges). |
| 2/5 (2.2 / C0.5): on !NEED the clobber shows only with `in_mapped_nents == 0` | **fixed** | 2/6 has a new paragraph on the mapped pass hiding it. |
| 4/5 (4.1 / C0.6): trigger "trailing 0 after an exact consume" is too narrow | **fixed** | 3/6 states the real condition: splits reached after the input runs out, with the `{10,0,5}` and skip-at-end examples. The cover letter is also updated. |
| 4/5 (4.7 / C0.12): "-EINVAL is a trivial respin" ignores valid `[…,0]`-at-end requests | **fixed** | v3 rejects only past-the-end requests and keeps valid zero-size splits (D1, D2). The remark is gone. |
| 3/5 (3.3): T13 accepts the leading zero-length entry | **fixed** | 6/6: `sg_split_check_shape()` asserts that no non-empty split starts with a zero-length entry. |
| Cover C0.10 / C0.11: bisect layout and "isolable tail" | **superseded** | v3 has a new layout. Each step is checked against its parent (EVIDENCE §4). The cover letter explains the new layout. The tail is no longer droppable as a whole; 4/6 alone is (D3). |
| Cover C0.14: KUnit results (UML 17/3, x86_64+KASAN 20/20) | **out of scope here; cover letter says so** | No kernel build in this workspace. The cover letter has a `[HUMAN: …]` block with the commands and expected counts (22 pass / 3 skip; 25/25). The userspace shim replay passes 25/25 (EVIDENCE §5). |
| C0.3, 1.8, 1.10, 2.4, 2.7, 4.6, C0.17: no in-tree caller / DTHEv2 / Kconfig selects / Egorenkov 2021 / Kconfig.debug | **out of scope (UNCHECKED, no tree or network)** | Kept as in v2 except where Sashiko corrected them (S2). Listed in DECISIONS D7 for human verification. The cover letter now says "as far as I can tell". |
| 1/5, 2/5 "confirmed" items (1.2, 1.5, 1.6, 1.9, 2.1, 2.3, 2.5, 2.6, C0.8, C0.15, …) | **no change needed** | Still true. 1/6 and 2/6 are byte-identical in code to v2 (blob hashes `28fe7fa6c9ac` / `ab574dd26b03`). C0.15 is re-confirmed: v3 minus 2/6 on !NEED fails T2, T3, T11, T12, T15 and T16. |

## Sashiko review of v2 (S1–S10)

| Item | Status | Where / evidence |
|---|---|---|
| **S1** [High] patch 2 still writes `out_sg[-1]` for nents 0 | **fixed (3/6); partly refuted as a bisect concern** | The write is pre-existing and identical upstream; 2/6 does not introduce or worsen it. 3/6 removes its cause: no split has nents 0 on success.<br>The step differential s2→s3 is identical wherever s2 is memory-safe, and s3 rejects every invalid request (`logs/diff_steps_s2_s3.log`). |
| **S2** s390 selects `NEED_SG_DMA_LENGTH` only `if PCI` | **fixed** | 2/6 message: "or s390 with PCI". Taken from the review; Kconfig is not in this workspace. |
| **S3** KUnit comments describe T8b / the zero-nents guard before they exist | **fixed** | The suite now lands after all fixes (6/6) with T8b included. The `ZERO_SIZE_PTR` "walks zero times" comments are removed, because no split has 0 entries any more. |
| **S4** comment mentions a nonexistent `*backing_out` | **fixed** | 6/6: `sg_split_build_input()` comment rewritten. |
| **S5** `ZERO_SIZE_PTR` returned as a successful `out[k]`, kernel-doc silent | **fixed** | 3/6, option "always emit one end-marked zero-length entry". The kernel-doc now documents the zero-size split result and the `-EINVAL` cases (D1). |
| **S6** divergent cases don't check the end marker, yet the message claims they do | **fixed** | 6/6: `sg_split_run_divergent()` takes the expected CPU entry count per split, asserts the end marker is on exactly that entry, and checks DMA entries ≤ CPU entries. The message now describes this. |
| **S7** [Medium] same as S5, with T8 vs T8b inconsistency | **fixed** | As S5. T8 and T8b now give the same shape: one zero-length, end-marked entry. |
| **S8** (1) T6 is not a mid-entry case; (2) `length_last_sg` is not inflated | **fixed** | (1) T6 moved to group A with a comment. With 4/6 it now also asserts no leading zero piece, and it fails with 4/6 reverted. 6/6's message no longer lists it among the mid-entry cases.<br>(2) 1/6 message corrected. |
| **S9** `out[k]` leaks if an assertion aborts the case | **fixed** | 6/6: `KUNIT_DEFINE_ACTION_WRAPPER(sg_split_kfree, kfree, const void *)` plus `kunit_add_action_or_reset()` right after a successful `sg_split()`, in `sg_split_run_mode()` and `sg_split_run_divergent()`. `sg_split_run_einval()` frees manually only on a regression, with no assert in between. |
| **S10** duplicate of S4 | **fixed** | As S4. |

## Residual (not fixed, flagged)

**Mutation survivors** — 2 of 33: `L73 --nb_splits >= 0` and `L88 nb_splits >= 0` (EVIDENCE §6). Listed, not explained away.

**Assumption in 3/6 and 4/6: DMA entries ≤ CPU entries per split.** It holds when DMA segment boundaries are a subset of CPU entry boundaries, i.e. `dma_map_sg` only merges whole entries. sg_split() still sizes `out[k]` from the CPU pass and has no check that the mapped pass fits. This is out of scope for v3 and not mentioned in the cover letter.
- P-CORE asserts DMA ≤ CPU on generated coalesced mappings.
- KUnit D1–D3 assert it.
- A defensive `-EINVAL` when the mapped nents exceeds the allocation could be a follow-up. Needs human decision whether to add it.
