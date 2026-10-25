<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# sg_split v2 — handoff #2 (2026-06-10)

Task: ship a v2 patch series fixing latent bugs in `lib/sg_split.c` and
replying to Andrew Morton (akpm). This is **defensive kernel maintenance**:
fixing pre-existing bugs and submitting them upstream the normal way. All
drafts are written and approved by c4ffein; the remaining work is
mechanical (git re-sequence + format-patch + send).

Supersedes the **patch-count plan** in `V2_HANDOFF.md` (that file said 6
patches; the plan is now **5** — see below). Everything else in
`V2_HANDOFF.md` is still valid reference (env gotchas, validation, lore
msgids).

## Where we are — drafts DONE and approved

All three live at repo root and were reviewed line-by-line:

- **`mail_v2_0.md`** — the in-thread plain-text reply to akpm. SEND-READY.
  Answers his two questions (not seen in real life; both Sashiko findings
  fixed + a third bug found) and previews the 5-patch v2. Threads via
  `In-Reply-To:` akpm's msgid. One optional nit left: line ~40 "Second
  comment" → "The second comment" for parallelism with "The first comment".
- **`mail_v2_cover.md`** — the `[PATCH v2 0/5]` cover letter. Approved.
  Leads with the EXPORT_SYMBOL framing; has the "Changes since v1"
  changelog; threads `In-Reply-To:` the v1 `[0/2]` cover.
- **`mail_v2_commits.md`** — all 5 commit messages. Approved. Patch 2's
  message is framed by the **config symbol** `CONFIG_NEED_SG_DMA_LENGTH`,
  NOT an arch enumeration (the alias is config-dependent: `drivers/iommu`
  selects NEED; enumerating !NEED arches is the "strike sparc32" trap).

Test file already edited + verified this session:
- **`inspiration/targets/linux/lib/tests/sg_split_kunit.c`** — added an
  end-of-list-marker invariant (`sg_is_last(entry) == (entry is last)`,
  guarded `out_nents>0`) in the shared `sg_split_run_mode` loop, so it
  runs across all 16 base cases ×2 modes + t8b. **checkpatch --strict -f:
  0/0/0. UML run: 17 passed / 3 skipped (divergent skipped on !NEED).**

## The 5-patch plan (was 6; divergent folded into patch 3)

Bisect-safe order:

```
1/5  fix sg_calculate_split() nb_splits overshoot   (sg_split.c) — UNCHANGED code from v1
2/5  fix sg_split_phys() ->length clobber           (sg_split.c) — bug 2; lands before the test that exposes it
3/5  add KUnit tests                                (lib/tests/sg_split_kunit.c + Kconfig.debug + tests/Makefile)
                                                     — T1..T16, each run unmapped AND identity-mapped, DMA-addr +
                                                       end-marker asserts, NEED-gated divergent D1..D3. NO t8b yet.
4/5  guard sg_split_phys/mapped vs zero-nents        (sg_split.c) — bug 3 (the OOB)
5/5  add zero-nents regression test (t8b)            (sg_split_kunit.c) — guarded by 4/5
```

Patches 4 + 5 are an isolable droppable tail (akpm may prefer to treat a
malformed zero-size split as the caller's problem). Bug-3 fix shipped as
tolerate-and-skip (`if (!split->nents) continue;`); offered to respin to
`-EINVAL` if he prefers (cheap).

## Exact git state (branch `scratch-v2` in inspiration/targets/linux)

Base `08d0d3466664` (Merge tag net-7.1-rc2), confirmed ancestor of HEAD.

Committed (WRONG order for v2 — must re-sequence):
- `048a75c05e50` overshoot fix → becomes 1/5, code unchanged.
- `5156904a9669` zero-nents guard in BOTH copiers → becomes 4/5 (must MOVE
  down; its t8b test splits out to 5/5).

Uncommitted working tree (ALL fixes are present in the tree):
- `M lib/sg_split.c` — **bug-2 (->length clobber) fix applied** (the `len`
  temporary + `out_sg->length = len` after the `sg_dma_len=0` scrub, at
  ~lines 89/98). Not yet committed → becomes 2/5.
- `?? lib/tests/sg_split_kunit.c` — full test file (T1-16 + identity +
  end-marker + divergent D1-3 + t8b). Splits across 3/5 and 5/5.
- `M lib/Kconfig.debug` — adds `config SG_SPLIT_KUNIT_TEST` (goes with 3/5).
- `M lib/tests/Makefile` — adds the obj line (3/5).

IGNORE unrelated untracked `tools/testing/radix-tree/_empty.c`, `_stubs.c`.

## NEXT ACTION — re-sequence into the 5 commits

1. **Backup first:** `git -C inspiration/targets/linux branch scratch-v2-bak`.
2. `git reset 08d0d3466664` (mixed) so the two commits' changes join the
   working tree. Now everything is uncommitted on top of base.
3. Re-commit in 5 pieces. `sg_split.c` carries 3 separate logical edits
   (overshoot / clobber / guard) and the test file 2 (suite / t8b), so you
   need partial staging. **`git add -p` is interactive and may be blocked
   in the harness** — the robust fallback is to reconstruct each
   intermediate file state with Write and commit between writes:
   - 1/5: sg_split.c = base + only `if (nb_splits <= 0)`.  Commit (msg from
     mail_v2_commits.md 1/5).
   - 2/5: + the bug-2 `len`/reorder hunk.  Commit (2/5).
   - 3/5: write sg_split_kunit.c WITHOUT t8b (drop the t8b function +
     its KUNIT_CASE line); add Kconfig.debug + tests/Makefile.  Commit (3/5).
   - 4/5: + `if (!split->nents) continue;` in both copiers.  Commit (4/5).
   - 5/5: add the t8b function + its KUNIT_CASE back.  Commit (5/5).
4. Verify (TMPDIR workaround, /dev/shm noexec — see V2_HANDOFF.md):
   - after 3/5: UML green (16 ×2 modes, 3 divergent skipped) — no t8b yet.
   - after 5/5: UML 17/3; x86_64+KASAN 20/20 (divergent runs, t8b OOB
     caught). Commands in V2_HANDOFF.md "Validation".
5. `checkpatch.pl --strict` on all 5 patches (expect clean; benign
   orphan-file MAINTAINERS warning on the test patch).
6. `git format-patch -v2 --cover-letter -5 --in-reply-to=<v1 0/2 msgid>`
   and paste the approved cover prose into the 0/5.

## Open items to confirm before send

- **DTHEv2 "immune" claim** (load-bearing in patches 1 & 2): the only
  in-tree caller passing an *unmapped* multi-entry partial split builds for
  arm64 (= NEED, immune). Re-confirm the caller survey still holds.
- **Name the review or not:** patch 4 prose says "an automated review";
  `Link:` points to akpm's lore msg (`20260601175549...@linux-foundation.org`).
  Decide whether to name Sashiko/gemini explicitly. No `Reported-by:` (tool,
  not a human) unless someone claims it.
- **Cc on test patches (3/5, 5/5):** get_maintainer routes `lib/tests/` to
  the KUnit maintainers (David Gow et al.) — add them to those patches; the
  discussion reply Cc stays as-is.

## Send mechanics (order matters)

1. **Reply to akpm FIRST** (mail_v2_0.md), in-thread plain text.
2. THEN send v2 threaded `--in-reply-to` the v1 `[0/2]` cover msgid
   (`178027099087.72481.1976843064458686851@gmail.com`).
3. Tool: `sm send-patches` (c4ffein's, at `/home/dev/workspace/sm`) OR
   `git send-email`. Identity `Charles Pellegrini <c4ffein.work@gmail.com>`.
   To akpm; Cc egorenar-dev, robert.jarzmik, t-pratham, lkml (+ KUnit
   maintainers on test patches). Jens Axboe stays dropped (get_maintainer
   does NOT route lib/sg_split.c to him — only akpm + lkml).
4. ALWAYS download the raw .eml/mbox.gz from lore and verify byte-exact;
   NEVER copy-paste patches (tabs die). **c4ffein presses send, not the
   model** — irreversible public post under his identity.

## Key facts verified THIS session (so you don't re-derive)

- `sg_dma_len(sg)` = `((sg)->dma_length)` on NEED, `((sg)->length)` on
  !NEED (include/linux/scatterlist.h:34/36). `sg_dma_address` is ALWAYS its
  own field; only `dma_length` is elided on !NEED. So bug 2 is a *length*
  bug, not an address bug.
- `NEED_SG_DMA_LENGTH` is a `kernel/dma/Kconfig` symbol selected by
  `drivers/iommu/Kconfig` + arch Kconfigs (x86, arm64, ppc, s390, sparc…).
  Config-dependent, not a clean arch list. Coalescing (IOMMU) is the only
  thing that makes a DMA segment's length differ from its CPU length.
- Bug 2 is latent because: mapped callers self-heal on !NEED (the mapped
  pass rewrites `sg_dma_len` == `->length`), so only an *unmapped*
  multi-entry split on a !NEED config is exposed; the sole unmapped caller
  is on NEED. Needs all three gates at once.
- Bug 3 (OOB) repro: `in=[{10}]`, `sizes=[10,0]`, `nb_splits=2` → trailing
  zero-size split gets 0 input entries → `out_sg=ZERO_SIZE_PTR` →
  `out_sg[-1]` fixup OOB. t8 (with a trailing input entry) does NOT trigger
  it (nents=1). Allocation is `kmalloc_objs(struct scatterlist, 0, …)` →
  ZERO_SIZE_PTR. -EINVAL backstop (sg_split.c:74) catches under-coverage,
  so only the phantom-zero-size + exact-fit combo reaches the OOB.

## Pointers

- Drafts: `mail_v2_0.md`, `mail_v2_cover.md`, `mail_v2_commits.md` (repo root).
- Prior handoff (env/validation reference): `mail_for_maintainers/V2_HANDOFF.md`.
- Userspace PBT harness + targets: `tests/irl/sg_split/`.
- Memory: `project_sg_split_submission`.
- Watch thread: https://lore.kernel.org/all/?q=sg_split&o=-1
