# sg_split replay — answer key (written 2026-10-23 BEFORE any agent result)

Start state: v2 as sent 2026-06-10 (scratch-v2, 5 patches). What really happened after:

| id | what | min example | pre-existing? | found in reality by |
|---|---|---|---|---|
| B4 | zero size loaded exactly at input exhaustion, then later non-zero sizes: rc 0, later splits silently unfulfilled (nents 0 / ZERO_SIZE_PTR) | in=[1], skip=0, sizes={1,0,1} | yes (crash-masked before v2's guard; v2's guard makes it silent) | Sashiko (v2 review), then hegel-c contract PBT |
| B5 | skip landing exactly on an entry boundary (skip == sglen) leaves a zero-length entry at the head of the output | in=[10,10], skip=10, sizes={5} | yes | cold-context Claude session by hand, 2026-07-06 |
| Z | zero-size semantics inconsistent: mid-list zero with input remaining -> nents 1 with a 0-length entry; at exhaustion -> nents 0 ZERO_SIZE_PTR | sizes containing 0 | yes | our trace 2026-07-05; resolved by -EINVAL (v3) |
| TRAP | naive B5 fix `skip >= sglen` silently corrupts data on a mid-list zero-length input entry; correct fix `skip && skip >= sglen` | in=[1][0][1], skip=0, sizes={2} -> 5a 5a instead of 5a 49 | n/a (fix-time) | comprehensive PBT "acid" suite |

Out of scope by our own decision: zero-length INPUT entries passing through to the output.

Scoring:
- process finds B4 / B5 / Z without hints: hit per item, and WHICH step found it
  (families/contract, differential, claims, mutation survivor, Sashiko).
- if a fix for B5 is written: does it fall into TRAP, and does the process catch it?
- false positives: claimed bugs that aren't (judge against kdoc + callers).
- anything real not in this key = bonus (verify).

## Result: Sashiko (pinned a434adc, claude-cli provider, model "opus"), 2026-10-23, ~20 min, 10 findings

- Z  HIT: #5 (Low) and #7 (Medium): trailing zero-size split after the input is used up
  returns rc 0 with out[k] == ZERO_SIZE_PTR (no entry, no end marker); T8 vs T8b shapes
  differ; recommends -EINVAL (what v3 did).
- B4 MISS (adjacent): it reasons only about a TRAILING zero; never tries zero followed by
  a non-zero size (the silent-unfulfilled case). The real July Gemini run had B4 as High.
- B5 NEAR-MISS: #8 describes T6 with "entry 0 records a zero-length leading piece
  (skip == sglen)", the exact bug-5 mechanism, but treats it as a test-grouping remark,
  not as a malformed output.
- Bonus, plausible, NOT yet verified by us: #2 commit message wrong about s390 (selects NEED_SG_DMA_LENGTH
  only `if PCI`); #6 commit message claims an end-marker check the divergent cases don't
  do; #8(2) "inflates length_last_sg" claim wrong for in=[2,1], sizes=[1]; #3 #4 #10 stale
  test comments; #9 KUnit assert path leaks out[k]; #1 bisectability (patch 2..3 carry
  the zero-nents OOB until patch 4).
- False positives: none obvious on first read.

## Result: property-writer agent (blind, steps 3+4+5), ~64 min, 57 tool calls

- B4 HIT (F1, P-INVALID, contract-boundary family): minimal in=[1], skip=0, sizes={1,0,1}
  -> rc 0 — the SAME minimum as our July hegel run. Also: patch 4's description too narrow.
  Verified by me with an independent program (both before and after the series: rc 0).
- Z  HIT (F2, P-WALK, well-formedness family): trailing zero after exhaustion -> 0-entry
  "list"; inconsistent with a zero elsewhere. Same as Sashiko #5/#7.
- B5 HIT (F3, P-NOZERO, well-formedness family): in=[1,1], skip=1, sizes={1} -> {len0,len1}.
  Verified by me. It also saw that a plain `>=` is not a valid fix (for a different
  reason than TRAP: skip==total with a zero split -> -EINVAL). TRAP itself not exercised yet.
- Bonus, verified: F5 nb_splits == 0 -> heap OOB read in sg_calculate_split (ASan, line 43),
  before and after the series; kdoc doesn't forbid 0. Latent (in-tree callers pass >= 1?).
- Bonus, not yet verified: F4 old code overshoot = heap overflow WRITE in sg_split_mapped
  (NEED model, coalesced DMA) — patch 1 fixes more than its message says.
- Claims (step 5): 1/5 length_last_sg "inflated" is wrong (drops to 0) — same as Sashiko
  #8(2); 1/5 "[2,1] gives 2" only on NEED; 2/5 clobber only shows with in_mapped_nents==0;
  4/5 trigger narrower than reality. Confirmed: 6 cases fail with bug 2 reverted, Egorenkov
  variant regresses t10, bisect order safe.
- Differential (step 3): no disagreement outside preconditions C1/C2/C3 (1039 / 1212
  inputs per 3000, both models, plus ASan).
- Mutation (step 1) on its passing subset: 0 of 23 survive.

Comparison: PBT agent 3/3 known + 1 verified bonus; Sashiko (Opus) 1/3 + near-miss on B5
+ several commit-message/test-hygiene finds the agent also partly made.

## Result: patch-author agent (blind, step 6), ~61 min, 107 tool calls -> work/v3/ (6 patches)

- Fixes B4 (3/6: past-the-end -> -EINVAL), Z (3/6: trailing zero split -> one zero-length end-marked
  entry, option (c); OUR real v3 chose -EINVAL for every zero size: different, both defensible,
  flagged as D1), B5 (4/6: `skip > sglen || (skip == sglen && size && !curr->in_sg0)`), F5 (5/6:
  nb_splits <= 0, skip < 0 -> -EINVAL). KUnit last (D5).
- TRAP: AVOIDED by construction (the `!curr->in_sg0` guard = only drop an entry before the split
  starts, the same reasoning as our `skip &&`). Process check (me): removing that guard
  (`skip == sglen && size`) is caught by test_diff_v3, which shrinks to EXACTLY the key's TRAP input
  in=[1][0][1] skip=0 sizes={2}; P-CORE catches it too (in=[1,0,1,1], 2 bytes != 3).
  Our own v4a fix (`skip && skip >= sglen`) fails the agent's suite, but only on its D1 contract
  (skip==total with sizes={0} must succeed) - a contract difference, not a bug.
- Rules: no SOB; Assisted-by present; [HUMAN: ...] placeholders for the in-kernel KUnit run and the
  v2 message-id; one stray `git status` (reported by the agent itself).
- Mutation (agent, plain): 2/33 survive. Me, under ASan, sg_calculate_split: still 2/19.
  - L73 `--nb_splits > 0` -> `>= 0`: SETUP GAP. Reads sizes[nb]; the harness passes a fixed
    sizes[MAXS] array, so ASan can't see it. With an exactly-sized heap copy (work/judge/v3fix):
    ASan heap-buffer-overflow at m73.c:75 -> killed. Unmutated v3 stays green with that harness.
  - L88 `if (nb_splits > 0)` -> `>= 0`: EQUIVALENT (nb_splits only reaches 0 with size == 0).
  Same lesson as v4a: the second survivor class is "run under ASan with exact-sized buffers".

## Result: Sashiko on the agent's v3 (same pin/provider/model), ~25 min: 2 findings, both Low
- 5/6 commit message: the negative-skip "starts 3 bytes before the input" story is LP64-only; on
  ILP32 off_t vs unsigned int compares unsigned, every entry is skipped and the old code already
  returned -EINVAL. Correct (usual arithmetic conversions). Message fix only.
- 6/6 KUnit divergent cases walk out[k] with the EXPECTED cpu_nents[k], not the actual count: a
  regression would give a KASAN report / oops instead of a clean KUnit failure. Valid test-robustness
  point; fix = KUNIT_ASSERT_EQ(sg_nents(out[k]), cpu_nents[k]) before the walks.
- No code-correctness finding. v2 had 10 findings; all v2 items are either fixed or triaged in TRIAGE.md.

## Overall
Blind pipeline (properties -> author -> mutation -> Sashiko) on v2 reproduced everything we learned
June-July (B4, Z, B5) plus F5, avoided TRAP, and its tests catch TRAP if reintroduced. Remaining
gate items before a human would sign: the 2 Sashiko Lows, the exact-size sizes[] harness fix,
D1-D7 decisions, and an in-kernel KUnit run (UML/KASAN) which nothing here did.

## Extended testing of agent v3 (items 1/2/3/4/6; 5 and 7 deferred) — 2026-10-23

Suite in sgreplay/work/v3x/. All properties pass in plain / NEED / ASan / NEED+ASan.
- 1 bigger shapes: n up to 48, up to 16 splits, up to 3 chain points.
- 6 exact-size heap arrays for sizes/out/out_mapped_nents (ASan sees overruns; kills the old L73 setup-gap survivor).
- 3 DMA model: research says every in-tree map_sg groups whole consecutive entries; model is now any consecutive grouping with a contiguous IOVA buffer; output DMA addresses checked against it.
- 2 fake pages (P-HUGE): lengths to UINT_MAX, totals past 4 GiB, extreme skip/sizes. Found one out-of-contract wrap (in-page offset + length > UINT_MAX); no allocator builds that, now a generator precondition.
- 4 32-bit differential (P-ABI32): static i386 build of the same sg_split.c must print identical canonical output. Reproduces Sashiko's ILP32 point on steps/s4.c (in=[1], skip=-1, sizes={0}).

Mutation (4 drivers, ASan, 4 functions): 32/33 killed in both NEED and !NEED.
Only survivor: L88 `nb_splits > 0` -> `>= 0`, equivalent (nb_splits <= 0 already rejected at entry).

Not covered: no 32-bit ASan; fake-page tests don't check bytes; no real-kernel run (5); D7 claims unchecked (7).
Series still needs Sashiko's 2 Lows (5/6 message LP64-only; 6/6 KUnit should assert sg_nents first).
