<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# sg_split v2 — handoff (refreshed 2026-06-09)

Supersedes the 2026-06-07 draft. This version reflects work done since:
the KUnit test is **ported, relocated, enhanced, and validated in-kernel**;
the patch plan grew from 4 to **6 patches**. Read top-to-bottom before
touching anything. Remaining work is mechanical: re-sequence the branch,
checkpatch, format-patch, reply to akpm, send.

## TL;DR — where we are

- **v1 (2-patch series) is SENT & LIVE ON LORE** since 2026-05-31, threaded,
  byte-exact. Cover (0/2) Message-ID:
  `178027099087.72481.1976843064458686851@gmail.com`
- **akpm replied** (within a day), two asks:
  1. "Real life or code review?" → answer plainly: **code review + property-
     based testing, no real-world report** (this is why it's not -stable).
  2. He ran an **automated review (Sashiko, gemini-3.1-pro)**:
     `https://sashiko.dev/#/patchset/178027099087.72481.1976843064458686851@gmail.com`
     (data API: `https://sashiko.dev/api/patchset?id=<urlencoded-msgid>`).
     It flagged a Critical pre-existing OOB + a test-file issue.
- Investigation cascaded into **three kernel bugs + one test bug**, all fixed
  and **empirically verified** (KUnit in real UML + x86_64/KASAN, plus a
  userspace ASAN harness). Next session = re-sequence into 6 clean patches and
  send.

## The three kernel bugs (all `Fixes: f8bcbe62acd0`, all latent — no in-tree caller hits any)

1. **nb_splits overshoot** (`sg_calculate_split`) — the v1 bug. `!nb_splits`
   exit check misses the overshoot 1→0→-1, folding a trailing entry into the
   last split. Fix: `if (nb_splits <= 0) break;`. Credit
   `Reported-by: Alexander Egorenkov <egorenar-dev@posteo.net>` + `Closes:` his
   2021 lore link (we rediscovered independently; credit is his).
   Repro: `in=[2,1], skip=0, nb_splits=1, sizes=[1]` → split[0] length 2 not 1.

2. **bug C: `sg_dma_len`-clobbers-`->length`** (`sg_split_phys`) — found
   chasing Sashiko's test-arg finding. On `!NEED_SG_DMA_LENGTH` arches (riscv,
   m68k, openrisc, nios2, microblaze … — **NOT sparc, NOT sparc32**, both are
   NEED) `sg_dma_len(sg)` aliases `((sg)->length)`, so `sg_dma_len(out_sg)=0`
   wipes the length just computed. Only the last entry is restored
   (`out_sg[-1].length`), so non-last entries of a **multi-entry** split keep
   length 0 → silent data loss (NOT an OOB). Fix: write `->length` AFTER the
   `sg_dma_len=0` line.
   Latent because: mainstream arches (x86/arm64/ppc/s390/…) have a separate
   `dma_length` field (immune); 5/6 callers map (the mapped pass re-copies
   ->length via the alias and repairs it); the lone unmapped caller (DTHEv2)
   runs on ARCH_K3=arm64 → NEED → immune.
   Repro (unmapped, !NEED): `in=[10,10], sizes=[3,17]` → split[1]=[len 0,len 10],
   sum 10 not 17. VERIFIED in-kernel: see "bug-C-revert" below.

3. **zero-nents ZERO_SIZE_PTR OOB** (`sg_split_phys` + `sg_split_mapped`) —
   Sashiko-found, confirmed with ASAN + KASAN. A split that gets no input entry
   (`nents==0`) leaves `out_sg` = `ZERO_SIZE_PTR (==16)`; `out_sg[-1].length=…`
   is an OOB write → KASAN splat / oops. Only memory-safety bug of the three;
   needs a caller to pass a trailing 0-size split with no input left (none
   in-tree). Fix: `if (!split->nents) continue;` in BOTH copiers.
   Repro: `in=[{10}], sizes=[10,0], nb_splits=2`.
   OPEN QUESTION for akpm: tolerate (our fix) vs reject `-EINVAL`. Decided:
   ship tolerate-and-skip, say so in the commit, offer to respin to `-EINVAL`
   if he prefers (cheap because it's the droppable tail).

### The test bug (folded into the test patches, not a kernel fix)
v1 KUnit passed `n_in` as sg_split's `in_mapped_nents`; must be `0` for an
unmapped list. On NEED arches (x86/arm64 CI) that drives the mapped pass to
read `dma_length` (deterministically 0 after sg_init_table's memset) →
`-EINVAL` → the test FAILS on CI. Passed for us only because UML is !NEED
(alias → valid length). Fix: pass `0`. That fix is what *exposes* bug C on
!NEED, which is why bug C (patch 2) must land before the corrected test.

## DECIDED: v2 patch order (6 patches, bisect-safe)

```
[v2 1/6] lib: scatterlist: fix sg_calculate_split() nb_splits overshoot   (sg_split.c) — unchanged from v1
[v2 2/6] lib: scatterlist: fix sg_split_phys() ->length clobber (bug C)    (sg_split.c)
[v2 3/6] lib: scatterlist: add KUnit tests for sg_split()                  (lib/tests/sg_split_kunit.c + Kconfig.debug + tests/Makefile)
                                                                            — 16 cases, each run unmapped AND identity-mapped
[v2 4/6] lib: scatterlist: guard sg_split_phys/mapped against zero-nents   (sg_split.c)
[v2 5/6] lib: scatterlist: add zero-nents OOB regression test (t8b)        (sg_split_kunit.c)
[v2 6/6] lib: scatterlist: add mapped/divergent-geometry KUnit tests       (sg_split_kunit.c) — NEED-gated, droppable tail
```

Rationale:
- Fixes 1+C are silent-corruption bugs; their suite (3/6) sits right after.
- The contentious OOB hardening (4/6) + its t8b regression test (5/6) are an
  isolable tail akpm can drop with no impact on 1–3.
- The mapped/divergent KUnit tests (6/6) are the most experimental (NEED-gated,
  pure regression-guard, no in-tree trigger) → very last, independently
  droppable. Promoting divergent to its own patch (vs deferring to a vague
  follow-up) was c4ffein's call and is correct: a clearly-scoped droppable
  patch can't "balloon review" the way folding it into 3/6 would.

## Current git state (branch `scratch-v2` in inspiration/targets/linux)

Base: `08d0d3466664` (Merge tag net-7.1-rc2). **Branch is NOT yet in the v2
order — must be re-sequenced.**

Committed:
- `048a75c05e50` = patch 1 (overshoot). → becomes v2 1/6, unchanged.
- `5156904a9669` = OOB guard in BOTH copiers. → becomes v2 4/6 (must MOVE down,
  and the t8b test split into a separate 5/6).

Uncommitted (working tree):
- `M lib/sg_split.c` — **bug-C fix applied** (the `len` temporary + `->length`
  after `sg_dma_len=0`). → becomes v2 2/6. NOT yet committed.
- `?? lib/tests/sg_split_kunit.c` — the full enhanced test (everything: 16
  cases + identity-mapped + t8b + divergent D1–D3). Splits across 3/6, 5/6, 6/6.
- `M lib/Kconfig.debug` — adds `config SG_SPLIT_KUNIT_TEST` (goes with 3/6).
- `M lib/tests/Makefile` — adds `obj-$(CONFIG_SG_SPLIT_KUNIT_TEST) += sg_split_kunit.o` (3/6).

NOTE: v1's wrong-location wiring was reverted (lib/Kconfig + lib/Makefile clean,
old lib/test_sg_split_kunit.c removed). Ignore unrelated untracked
`tools/testing/radix-tree/_*.c`.

## The exact verified fixes (already applied to the tree)

### Bug C (patch 2/6) — `sg_split_phys` per-entry loop (KEEP the post-loop lines):
```c
		for (j = 0; j < split->nents; j++, out_sg++) {
			unsigned int len = in_sg->length;

			*out_sg = *in_sg;
			sg_dma_address(out_sg) = 0;
			sg_dma_len(out_sg) = 0;
			if (!j) {
				out_sg->offset += split->skip_sg0;
				len -= split->skip_sg0;
			}
			out_sg->length = len;
			in_sg = sg_next(in_sg);
		}
		out_sg[-1].length = split->length_last_sg;   /* MUST stay */
		sg_mark_end(out_sg - 1);                      /* MUST stay */
```
Micro-decision still open: the `len`-temporary reads slightly awkward; a tidier
shape is fine, it's correctness-verified as-is.

### OOB guard (patch 4/6) — top of the loop in BOTH sg_split_phys and sg_split_mapped:
```c
	for (i = 0, split = splitters; i < nb_splits; i++, split++) {
		if (!split->nents)
			continue;
		...
```

## The test file: structure and patch-split point

`lib/tests/sg_split_kunit.c` (modern location; config in `lib/Kconfig.debug`,
Makefile line in `lib/tests/Makefile`). checkpatch `--strict -f`: **0/0/0**.

Structure top-to-bottom:
- 4 defines (`MAX_IN_ENTRIES`, `MAX_SPLITS`, `PAGE_BYTES`, `DMA_BASE=0x40000000`).
- `sg_split_pattern` — position-dependent backing byte (page*31 + off*17 + 0x5a).
- `sg_split_check_dma` / `sg_split_check_cpu` — per-split DMA / CPU oracles.
- `sg_split_build_input` — backing + input SG + linear oracle.
- `sg_split_run_mode(…, bool dma_mapped)` — one case, unmapped or identity-mapped
  (sets dma_address/dma_len, asserts the DMA view via sg_split_check_dma).
- `sg_split_run` — calls run_mode twice (unmapped, mapped).
- T1..T16 cases (each calls sg_split_run).            ← end of **patch 3/6**
- `sg_split_t8b_trailing_zero_no_input_left`.         ← **patch 5/6**
- `sg_split_run_divergent` + D1/D2/D3 (kunit_skip on !NEED). ← **patch 6/6**
- `sg_split_test_cases[]` + suite + `kunit_test_suite` + MODULE_*.
  (the KUNIT_CASE lines for t8b and D1–D3 move with their respective patches)

When splitting commits: patch 3 creates the file through T16 + `sg_split_run`
and the T1–T16 KUNIT_CASE entries; patch 5 adds the t8b function + its
KUNIT_CASE; patch 6 adds `sg_split_run_divergent` + D1–D3 + their KUNIT_CASE
entries. Clean cuts, no overlap.

## Validation — all reproducible

KUnit (in inspiration/targets/linux; needs `TMPDIR=/tmp/uml_tmp`, /dev/shm noexec):
```
mkdir -p /tmp/uml_tmp
# UML (!NEED) — the bug-C arch. Expect 17 passed, 3 divergent SKIPPED.
TMPDIR=/tmp/uml_tmp python3 tools/testing/kunit/kunit.py run sg_split \
  --kunitconfig=/dev/stdin <<'EOF'
CONFIG_KUNIT=y
CONFIG_SG_SPLIT=y
CONFIG_SG_SPLIT_KUNIT_TEST=y
EOF
# x86_64 (NEED) + KASAN — divergent runs, t8b memory-checked. Expect 20/20.
TMPDIR=/tmp/uml_tmp python3 tools/testing/kunit/kunit.py run sg_split \
  --arch=x86_64 --kconfig_add=CONFIG_KASAN=y --kunitconfig=/dev/stdin <<'EOF'
CONFIG_KUNIT=y
CONFIG_SG_SPLIT=y
CONFIG_SG_SPLIT_KUNIT_TEST=y
EOF
```
RESULTS (2026-06-09): UML 17 passed / 3 skipped; x86_64+KASAN **20/20**.
bug-C-revert proof: temporarily restore the original phys order, re-run UML →
6 unmapped multi-entry cases FAIL (T2/T3/T11/T12/T15/T16, all `[unmapped]`);
their `/mapped` variants pass (mapped pass repairs) → the suite catches bug C.

Userspace harness (fast, ASAN/UBSAN, dual-arch in one build) in
`tests/irl/sg_split/`:
```
make cases          # v2 candidate (sg_split_v2.c): ALL PASS both arch models
make cases-bugc     # v2 minus bug-C fix: !NEED 6 fail (unmapped) / mapped pass; NEED all pass
make cases-contrast # unguarded current: ASAN heap-overflow / SEGV at sg_split_phys t8b
```
Files there: `sg_split_v2.c` (all fixes), `sg_split_bugc.c` (minus bug C),
`sg_split_current.c` (upstream), `test_sg_split_cases.c` (the MIT userspace
suite — NOT shippable; the GPL KUnit file is the deliverable).

## Next-session checklist

1. Re-sequence `scratch-v2` to the 6-patch order above. Commit the bug-C fix as
   2/6 (before the OOB guard). Split the test file across 3/6, 5/6, 6/6.
2. Build/run KUnit at the relevant positions (TMPDIR workaround):
   - after 3 (no OOB guard yet, no t8b): UML green (16 cases × 2 modes).
   - after 5: t8b present + guarded → green; as a control, with 4–5 reverted t8b
     OOBs (proved via ASAN/KASAN).
   - after 6: x86_64+KASAN 20/20 (divergent runs).
3. `checkpatch.pl --strict` on ALL SIX patches (not just `-f` on files). Expect
   clean; the test patch emits the benign orphan-file MAINTAINERS warning
   (no specific F: entry for lib/sg_split.c — get_maintainer routes to akpm).
4. `git format-patch -v2 --cover-letter -6`. Cover (0/6) MUST contain:
   - "Changes since v1:" changelog (corrected in_mapped_nents; added bug C +
     OOB+t8b; added mapped/identity + divergent coverage; link to v1 lore).
   - **EXPORT_SYMBOL framing** (lib/sg_split.c:203): sg_split is exported, so the
     contract is "any caller, any sizes[]", not the 6 in-tree call sites — this
     is the strongest justification for landing latent fixes; lead with it.
   - the "found via the automated review feedback" provenance for 2 & 4.
   - the tolerate-vs-EINVAL question for 4/6, with "trivial to respin to -EINVAL".
   - mapped coverage is now TESTED (identity on all arches, divergent on NEED via
     KASAN x86_64), not just argued — say so. Real dma_map_sg rig still deferred.
   - **strike sparc32** from any vulnerable-arch list (it IS NEED).
5. **Reply to akpm IN-THREAD FIRST** (plain text, interleaved quoting): answer
   his two questions + "the test-arg note led me to a third pre-existing bug;
   v2 incoming." THEN send v2 threaded `--in-reply-to` the v1 cover msgid.
6. Send via `sm send-patches` (c4ffein's tool) OR git send-email. Identity
   `Charles Pellegrini <c4ffein.work@gmail.com>`. To akpm; Cc egorenar-dev,
   robert.jarzmik, t-pratham, lkml (Jens Axboe dropped in v1 — keep dropped).
   ALWAYS download the raw .eml / mbox.gz from lore to verify byte-exactness;
   NEVER copy-paste patches (tabs die).

## Attribution (public — get it right)

- Bug 1: `Reported-by: Alexander Egorenkov <egorenar-dev@posteo.net>` + `Closes:`
  his 2021 link (`20210418143425.22944-1-egorenar-dev@posteo.net`).
- Bug 3 (OOB) & the test-arg finding: surfaced by the **Sashiko** automated
  review akpm ran (gemini-3.1-pro; prompts repo masoncl/review-prompts = Chris
  Mason). Credit the review in commit-body prose; confirm whether a specific
  human wants Reported-by. We confirmed with ASAN + KASAN.
- Bug C: ours (no external reporter); say honestly it was "found while
  addressing the review feedback on the test."
- All patches: `Assisted-by: Claude:claude-opus-4-8 hegel-c`
  (Documentation/process/coding-assistants.rst) + `Signed-off-by: Charles
  Pellegrini <c4ffein.work@gmail.com>`.

## Pointers

- v1 package + INSTRUCTIONS + files.md: this directory (`mail_for_maintainers/`).
- Userspace PBT harness + targets: `tests/irl/sg_split/`.
- Memory: `project_sg_split_submission` (has the current state + env gotchas).
- sm patch-send tool: `/home/dev/workspace/sm` (the `send-patches` subcommand).
- Watch thread: https://lore.kernel.org/all/?q=sg_split&o=-1
