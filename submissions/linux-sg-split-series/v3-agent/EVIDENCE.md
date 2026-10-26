# Evidence that v3 (work/v3/lib/sg_split.c) is correct

Run every command from the workspace root.

**Prefixes**
- Plain runs: `ulimit -v 4000000; ulimit -c 0;`
- ASan runs: `ulimit -c 0; timeout 300` (or `timeout 400`)

**Build flags**
- NEED builds add `CFLAGS="-O1 -g -w -D_LGPL_SOURCE -DCONFIG_NEED_SG_DMA_LENGTH"`.
- NEED+ASan builds append `-fsanitize=address,undefined -fno-omit-frame-pointer` to those CFLAGS.

Logs are in `work/v3/logs/`.

**Files under `work/v3/`**

| File | What it is |
|---|---|
| `lib/sg_split.c` | v3 code |
| `steps/s0.c … s5.c` | `lib/sg_split.c` after each patch (s0 = upstream) |
| `lib/tests/sg_split_kunit.c` | v3 KUnit suite |
| `series/` | patches; `make_series.py` builds them from `steps/` and `msgs/` |
| `sgcase_v3.h`, `test_props_v3.c` | copies with one expectation changed (§2) |
| `test_v3_new.c` | new properties (§3) |
| `test_diff_v3.c` | differential test (§4) |
| `test_kunit_v3.c` + `kunit_shim/` | userspace KUnit replay (§6) |
| `mut_*.c`, `mut_guard.h` | mutation drivers (§7) |
| `probe_args.c` | argument probe |
| `steps/rev1..5.c` | v3 with one patch reverted |

## 1. Existing property tests on v3 (`work/test_props.c`, unchanged)

```
make bin T=work/test_props.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_props_v3
./work/v3/test_props_v3 2000                                   -> logs/orig_props_v3_noneed_2000.log
make bin T=work/test_props.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_props_v3_need CFLAGS="... -DCONFIG_NEED_SG_DMA_LENGTH"
./work/v3/test_props_v3_need 2000                              -> logs/orig_props_v3_need_2000.log
make bin T=work/test_props.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_props_v3_san SAN=1
timeout 300 ./work/v3/test_props_v3_san 1000                   -> logs/orig_props_v3_noneed_san_1000.log
make bin T=work/test_props.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_props_v3_need_san CFLAGS="... -DCONFIG_NEED_SG_DMA_LENGTH -fsanitize=address,undefined -fno-omit-frame-pointer"
timeout 400 ./work/v3/test_props_v3_need_san 1000              -> logs/orig_props_v3_need_san_1000.log
```

| property | !NEED | NEED | !NEED ASan | NEED ASan | on v2 (REPORT) |
|---|---|---|---|---|---|
| core | pass | pass | pass | pass | pass |
| walkable | pass | pass | pass | pass | FAIL (F2) |
| nozero | pass | pass | pass | pass | FAIL (F3) |
| nozero_mapped | pass | pass | pass | pass | FAIL (F3) |
| geometry | **FAIL** (intended, §2) | **FAIL** (§2) | **FAIL** (§2) | **FAIL** (§2) | pass |
| invalid | pass | pass | pass | pass | FAIL (F1) |
| invalid_other | pass | pass | pass | pass | pass |
| errpaths | pass | pass | pass | pass | pass |

- No ASan or UBSan report appears in any run.
- `geometry` fails in all four runs with the same shrunk counterexample: `n=1 lens=[1] skip=0 sizes=[1,0]`, "split 1: 1 entries, model 0". That is the F2 shape, which v3 changes on purpose.

## 2. Expectations changed on purpose (contract changes)

The originals stay untouched. The copies are:
- `work/v3/sgcase_v3.h`, a copy of `work/sgcase.h`;
- `work/v3/test_props_v3.c`, a copy of `work/test_props.c` whose only change is `#include "sgcase_v3.h"`.

`diff work/sgcase.h work/v3/sgcase_v3.h` shows exactly one changed expectation. **CHK_GEOMETRY (P-GEOM)** now compares the entry count with `model_nents_v3()` instead of `m.s[k].nents`. `model_nents_v3()` is new, uses the same model, and adds two rules:

| # | Rule | Why | Decision |
|---|---|---|---|
| (a) | A split that starts at the very end of the input (model `start == N`, nents 0) now has **1** entry. | Intended contract change of 3/6: one zero-length, end-marked entry instead of a 0-entry `ZERO_SIZE_PTR`. | DECISIONS D1 |
| (b) | A split with `size > 0` does not count the entries at its start from which it takes 0 bytes. These are the entry that ends exactly at the split's start, and empty entries. | Intended contract change of 4/6: no zero-length first entry. | DECISIONS D3 |

No other expectation was changed.

```
make bin T=work/v3/test_props_v3.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_propsv3copy            (+ _need, _san, _need_san)
./work/v3/test_propsv3copy 2000 ; ./work/v3/test_propsv3copy_need 2000                -> logs/copy_props_v3_{noneed,need}_2000.log
timeout 400 ./work/v3/test_propsv3copy_san 500 ; ... _need_san 500                    -> logs/copy_props_v3_{noneed,need}_san_500.log
```

All 8 properties pass in all 4 configurations, including `geometry`. `./work/v3/test_propsv3copy 3000 geometry` also passes in both models.

The changed expectation still discriminates. In `logs/props_per_step.log`:
- `geometry` (v3 rules) fails on v2 and on step s3, which lacks 4/6.
- It passes from s4 on.

## 3. New properties (`work/v3/test_v3_new.c`)

These cover what v3 fixes and no existing property checks.

| Property | What it checks | Patch |
|---|---|---|
| **P-ARGS** | `nb_splits <= 0` (0, or `-[1, INT_MAX]`) and/or `skip < 0` (−1, or `-[1, LONG_MAX]`) give `-EINVAL`. `out[]` and `out_mapped_nents` are untouched. Nothing is allocated (tracker; plain build). ASan stays clean. | 5/6 |
| **P-ZEROSPLIT** | Every zero-size split of a valid request has exactly 1 entry, `out_mapped_nents == 1`, is end-marked with no chain bit, and has `length 0` and `dma_len 0`. Its CPU address, and its DMA address when mapped, is the split's stream position. When unmapped, `dma_address` is 0. Every split has `out_mapped_nents >= 1`. | 3/6 |
| **P-NOLEAD** | Every split with `size > 0` starts with an entry of `length > 0`, and when mapped `dma_len > 0`. This includes inputs with empty entries, which P-NOZERO excludes. | 4/6 |

```
make bin T=work/v3/test_v3_new.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_v3_new       (+ _need / _san / _need_san)
./work/v3/test_v3_new 3000 ; ./work/v3/test_v3_new_need 3000        -> logs/new_v3_{noneed,need}_3000.log      all pass
timeout 300 ./work/v3/test_v3_new_san 1000 ; ... _need_san 1000     -> logs/new_test_v3_new_{san,need_san}_1000.log   all pass
```

**They discriminate:**
- On v2 (`logs/new_props_on_v2.log`):
  - `args` fails with "nb_splits=0 accepted: rc=0" (plain build).
  - `zerosplit` and `nolead` fail on `[1]`, `sizes=[1,0]`.
- On step s3 (no 4/6), `nolead` fails on `lens=[1,1] skip=1 sizes=[1]`.
- On a variant of s4 that only handles the boundary case (`&& sglen` added), `nolead` fails on `lens=[0,1] skip=0 sizes=[1]`, the empty-entry case.

## 4. Differential v2 → v3, and every step against its parent

**Precondition** (also in the header of `work/v3/test_diff_v3.c`). v3 may differ from v2 only on these inputs, evaluated with the `sgcase.h` model on the CPU geometry and, if mapped, the DMA geometry:

| | Input class | v2 | v3 |
|---|---|---|---|
| **E1** | Invalid request (`skip + Σsizes > total`) in which some split is reached with no input left | may return 0 | `-EINVAL` |
| **E2** | Valid request in which some split starts at the very end of the input | 0 entries | 1 zero-length entry |
| **E3** | Valid request in which some split with `size > 0` starts in an entry from which it takes 0 bytes | leading zero-length entry | no leading zero-length entry |

`nb_splits <= 0` and `skip < 0` are never generated.

- **Everywhere else**, v2 and v3 must agree exactly: rc, `out_mapped_nents[]`, and each entry's `page_link`, `offset`, `length`, `dma_address` and `dma_len`.
- **Additionally**, v3 must return `-EINVAL` for every invalid request.

```
make bin T=work/v3/test_diff_v3.c SRC=work/v3/lib/sg_split.c OUT=work/v3/test_diff_v3        (+ _need, _san, _need_san)
./work/v3/test_diff_v3 20000 ; ./work/v3/test_diff_v3_need 20000 ; timeout 300 ./work/v3/test_diff_v3_san 5000 ; ... _need_san 5000
```
Results are in `logs/diff_v2_v3.log`.

| config | cases | identical-required | changed-allowed [E1/E2/E3] | changed but still identical (valid among them) | result |
|---|---|---|---|---|---|
| !NEED | 20000 | 9457 | 10543 [1199/8485/1703] | 501 (0) | pass |
| NEED | 20000 | 9489 | 10511 [1289/8435/1591] | 502 (0) | pass |
| !NEED ASan | 5000 | 2371 | 2629 [345/2088/370] | 117 (0) | pass |
| NEED ASan | 5000 | 2344 | 2656 [340/2111/418] | 138 (0) | pass |

- "Changed but still identical" only happens for invalid E1 inputs that v2 already rejected. That is the case where the current split is the one with bytes left.
- Every valid E2/E3 input did change.

**Bisect, step against parent.** These are the same test with `-DOLDSRC='"steps/sN.c"' -DALLOW=bits` and `SRC=work/v3/steps/sN+1.c`, for both models, plain 20000 and ASan 5000 cases.

| Step | Allowed to change | Result |
|---|---|---|
| s3 vs s2 | E1, E2 only (`-DOLD_UNSAFE_EMPTY`) | identical on every input where s2 is memory-safe. On the E1/E2 inputs, where s2 writes out of bounds exactly as upstream does, s2 is not called; s3 must and does return `-EINVAL` for every invalid one. `logs/diff_steps_s2_s3.log` |
| s4 vs s3 | E3 only | pass, and every E3 input changed. `logs/diff_steps.log` |
| s5 vs s4 | nothing | 20000/20000 identical. `logs/diff_steps.log` |

The first block of `logs/diff_steps.log` is the s3-vs-s2 attempt *without* `OLD_UNSAFE_EMPTY`. There s2's own pre-existing zero-nents out-of-bounds write aborted the run, which is the reason for that flag.

**s0→s1 and s1→s2:** `steps/s1.c` and `steps/s2.c` are byte-identical to v2 after 1/5 and after 2/5. The git blob hashes match v2's index lines: `28fe7fa6c9ac` and `ab574dd26b03`. The original review already checked them with `work/test_diff.c`.

**Per-step property sets** (`logs/props_per_step.log`): `test_props_v3` plus `test_v3_new`, 300 cases each, both models. The failing set never grows from one step to the next. It shrinks to empty at s5.

## 5. Per-patch revert check (KUnit replay, §6)

`steps/revN.c` is v3 with patch N reverted. Results are in `logs/kunit_replay_v3_minus_each_patch.log`.

| Reverted | Failing cases | Builds |
|---|---|---|
| 1/6 | T5, T7, T9, T11, T13, T14, T15 | both |
| 1/6 | D3 | NEED only |
| 2/6 | T2, T3, T11, T12, T15, T16 | !NEED only |
| 3/6 | T8b, T8c, T8d (ASan heap-buffer-overflow in `sg_split_phys` for T8b/T8c) | both |
| 4/6 | T6, T13, T17 | both |
| 5/6 | T18 | both |

The 6/6 commit message quotes these lists.

## 6. KUnit suite (userspace replay through a shim)

`work/v3/kunit_shim/kunit/{test,resource}.h` provides the KUnit API the suite uses: EXPECT/ASSERT, `kunit_kzalloc`, deferred actions and `kunit_skip`. An ASSERT aborts the case via longjmp. `work/v3/test_kunit_v3.c` `#include`s `work/v3/lib/tests/sg_split_kunit.c` verbatim and runs each case in a forked child.

```
make bin T=work/v3/test_kunit_v3.c SRC=work/v3/lib/sg_split.c OUT=work/v3/kunit_v3 CFLAGS="-O1 -g -w -D_LGPL_SOURCE -I$PWD/work/v3/kunit_shim"   (+ NEED, + ASan)
./work/v3/kunit_v3
```

| Build | Result |
|---|---|
| !NEED, plain and ASan | 22 ok, 3 skip (D1–D3) |
| NEED, plain and ASan | 25/25 ok |

Log: `logs/kunit_replay_v3.log`.

Against earlier code (`logs/kunit_replay_v3_suite_vs_steps.log`, ASan), failing cases (!NEED / NEED):

| Code | !NEED | NEED |
|---|---|---|
| upstream | 17 | 14 |
| v2 | 7 | 7 |
| s1 | 13 | 7 |
| s2 | 7 | 7 |
| s3 | 4 | 4 |
| s4 | 1 | 1 |
| s5 | 0 | 0 |

**Not done:** an in-kernel KUnit build and run (UML, or x86_64 with KASAN). The cover letter has a `[HUMAN: …]` block for those.

## 7. Mutation testing

The drivers run without arguments, as `mutate.py` expects:
- `work/v3/mut_diff_v3.c` (3000 cases);
- `work/v3/mut_new_v3.c` (200 cases);
- `work/v3/mut_props_v3.c` (150 cases).

Each `#include`s the corresponding v3 test unchanged. A 25 s watchdog (`mut_guard.h`) SIGKILLs the driver's process group, so a hanging mutant counts as killed instead of stalling the run.

```
python3 tools/mutate.py work/v3/mut_diff_v3.c work/v3/mut_new_v3.c work/v3/mut_props_v3.c \
    --src work/v3/lib/sg_split.c --func sg_calculate_split sg_split_phys sg_split_mapped sg_split
```
Log: `logs/mutate_all.log`. Report: `work/v3/mutants_report.txt`.

**33 mutants, 31 killed, 2 SURVIVED:**

```
SURVIVED  L73: if (!size && --nb_splits > 0) {   ->   if (!size && --nb_splits >= 0) {
SURVIVED  L88: if (nb_splits > 0) {              ->   if (nb_splits >= 0) {
```

(Line numbers refer to `work/v3/lib/sg_split.c`.) Per function: `sg_calculate_split` 19 mutants, 2 survived; `sg_split_phys` 3, `sg_split_mapped` 3 and `sg_split` 8, all killed (`mutants_report_sg_calculate_split.txt`, `mutants_report_phys_mapped_split.txt`).

**About the run itself:**
- `tools/mutate.py` writes `work/mutants/m*.c` and `work/mutants/report.txt`. The m*.c files from the round-1 run were overwritten.
- `report.txt` was backed up before the run (`work/v3/mutants_v2_report_backup.txt`) and restored afterwards.
- The first attempt, without the watchdog, ran past 9 minutes and was stopped by `timeout`. Its output is lost.

## 8. Other checks and notes

**Fixed scenarios.** `work/test_claims.c` (unchanged), built against v3 with ASan in both models (`logs/claims_v3.log`):
- every scenario either matches the kernel-doc or is rejected with `-EINVAL`;
- `p4_zero_then_nonzero_past_end` and `p4_ref_nonzero_past_end` give -22;
- `p4_t8b`, `p4_t8b_mapped` and `p4_skip_at_end_two_zero` match;
- `p1_coalesced_mapped_overshoot` (NEED) matches, with no ASan report;
- `cover_v1_arg_…` (NEED) gives -22, the same as v2.
- `nb_splits_zero` prints no rc line because its child `_exit()`s without flushing stdout. This is a test artifact: there is no CRASH line and no ASan report. P-ARGS and the probe cover that case.

**Argument probe.** `work/v3/probe_args.c`:

| Input | v2 | v3 |
|---|---|---|
| `nb=-1` | `-ENOMEM` | `-EINVAL` |
| `skip=-3` | rc 0, `out[0]` offset 97 for an entry at 100 | `-EINVAL` |
| `nb=0` | ASan heap-buffer-overflow READ in `sg_calculate_split` | `-EINVAL` |

**Patches apply.** `work/v3/series/v3-000[1-6]*.patch` apply in order with `patch -p1` onto `before/lib/sg_split.c`. After each of 1/6–5/6 the file is byte-identical to `steps/sN.c`, and after 6/6 the KUnit file is identical to `work/v3/lib/tests/sg_split_kunit.c`. The `Kconfig.debug` and `lib/tests/Makefile` hunks are v2's, unchanged; they were only checked against stub files holding their context lines.

**Last edit after the full runs: two comments reworded.**
- After the full runs above, I reworded the two comments that follow the loop in `sg_calculate_split()`. The code is unchanged and the line count is the same, so the mutation line numbers still hold.
- Re-run after that edit, all passing:
  - `test_propsv3copy 500`: 8/8;
  - `test_v3_new 500`: 3/3;
  - the KUnit replay (!NEED, plain): 22 ok + 3 skip, 0 failed;
  - the apply check.
- The `steps/revN.c` files were derived before that edit. They keep the old comment wording but have the same code.

**Runs that broke the rules.** Two long commands ran past the tool's 10-minute limit and were moved to the background automatically, against the "no background jobs" rule:
- the first ASan `test_props` run;
- the per-step props loop (`logs/props_per_step.log`).

Both finished. Nothing else heavy ran while they were in flight.
