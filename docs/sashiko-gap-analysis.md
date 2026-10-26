<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Sashiko gap analysis — static LLM review vs. dynamic PBT

**Pinned sashiko SHA:** `a434adcd821bbc3767657f8c41e979082acf127a` (2026-07-12).
This doc is written against that commit. `make check-sashiko` tells you when
upstream moved; when it does, re-read `src/worker/prompts.rs` in the clone at
`inspiration/kernel-review/sashiko/`, refresh the table below, then bump
`SASHIKO_PIN` in the top-level Makefile. Do not bump the pin without
refreshing this doc — the pin's whole job is to keep the two in sync.

## What sashiko is

[Sashiko](https://github.com/sashiko-dev/sashiko) (Linux Foundation,
Apache-2.0, Rust) is an **agentic static reviewer of Linux kernel patches**.
It ingests a diff (LKML via NNTP, GitHub PR, GitLab MR, or local git), runs
it through an 11-stage LLM pipeline — seven specialist "lenses" (stages 1–7)
followed by a refinement pipeline (dedup, conflict resolution, adversarial
verification, LKML report; stages 8–11) — and emits an inline-commented
review email. Reported recall: 53.6% of bugs from the last 1000
`Fixes:`-tagged upstream commits (Gemini 3.1 Pro).

It is the methodological complement of `hegel-bughunt`: same target domain
(Linux kernel), opposite method. Sashiko *reads* a diff and reasons about it;
we *execute* code with generated inputs and watch it fail under ASAN/UBSAN.
Sashiko reviews *changes* (a patch, with commit-message intent as an oracle);
we audit *whole libraries* (no intent available, properties as oracles).

The full stage prompts live inline in `src/worker/prompts.rs` (not in
`skills/` — that directory holds Gemini CLI dev-workflow automations for
building sashiko itself, and is irrelevant to us).

## Stage-by-stage: can dynamic PBT catch this bug class?

| Sashiko stage (lens) | PBT-reachable? | hegel-bughunt coverage today |
|---|---|---|
| 1 · Conceptual: architectural flaws, UAPI breakage, backwards compat | **No** | Needs the patch + intent. Static-review territory. |
| 2 · Implementation vs. commit-message claims; missing callers/initializers; off-by-one in bounds; bad bitwise ops | **Partly** | The claims/completeness half needs the commit message. The math half (off-by-one, bitwise, size args) is squarely ours — round-trip properties caught exactly this in `lib/packing.c` (LE + non-mult-of-4 buffer OOB). |
| 3 · Execution flow: logic errors, loop conditions, unhandled error paths, missing return checks, NULL derefs | **Yes** | Core territory: invariants + crash detection in fork mode. Error-*path* coverage needs fault injection (see gaps below). |
| 4 · Resource mgmt: leaks, UAF, double free, uninit vars, refcount, async-handoff teardown | **Yes (userspace)** | alloc/free sequence generators + ASAN/LSAN — this found the kzalloc/`__GFP_ZERO` stub bug and the `maple_prealloc` leak. Kernel-side kref/workqueue/timer teardown is out of reach in userspace shims. |
| 5 · Locking: sleep-in-atomic, AB-BA deadlock, RCU violations, IRQ context | **Mostly no** | Our fork-mode runner is single-threaded per case. Stateful PBT can drive *interleavings* of a lock-free API, but lockdep-class kernel bugs are sashiko's domain. |
| 6 · Security: buffer overflow, OOB r/w, integer overflow, TOCTOU, infoleak via copy_to_user | **Partly** | OOB/overflow: yes — ASAN + adversarial length generators (the packing.c find *is* stage 6 material). TOCTOU and privilege escalation: no. Infoleak: partially (MSAN-style uninit-read checks, not yet in our recipes). |
| 7 · Hardware: register access, DMA barriers, endianness, suspend/resume state machines | **No** | Needs the device. Endianness conversion bugs are the one sliver PBT reaches (byte-exact round-trips on both-endian codecs). |
| 8 · Dedup/consolidation of findings | n/a (process) | We do this by hand in `REPORT.md`; the rediscovery re-runs used judge subagents. |
| 9 · Concern vs. dismissal conflict resolution — "both sides are untrusted claims, verify against the code" | n/a (process) | Matches our hunter/judge split from the 2026-07-12 blind-rediscovery re-run. |
| 10 · Verification + severity: "to discard a concern you MUST find concrete proof it's invalid; unsure → report" | n/a (process) | **Worth stealing.** Our judges default the other way (unsure → dismiss). See takeaways. |
| 11 · LKML-format report generation | n/a (output) | Relevant to `mail_for_maintainers/` — their formatting rules (pre-existing-issue phrasing, no markdown, no shouting) are a good checklist for our patch letters. |

**Summary:** stages 3, 4, and the memory-safety half of 6 are our home turf —
and notably they're where sashiko's *static* reasoning is weakest (it can
suspect an off-by-one; we can prove it with a shrunk counterexample). Stages
1, 5, 7 are structurally out of PBT's reach. That's the honest boundary: the
two methods compose, they don't compete.

## Concrete takeaways for hegel-bughunt

1. **Adopt stage 10's asymmetric burden of proof for judge passes.** Sashiko:
   a finding may only be discarded with concrete disproof; uncertainty means
   report it. Our 2026-07-12 judge subagents leaned the opposite way. For
   *rediscovery* scoring the strict direction is right — false negatives cost
   more than false positives when a human reviews the output anyway.
2. **Error-path coverage is our biggest stage-3 gap.** Sashiko explicitly
   traces `goto cleanup;` paths. Our userspace shims could fail allocations
   deterministically (a drawn "fail the Nth malloc" counter in the stub
   layer) to force those paths. Candidate for the next skill revision.
3. **Uninit-read detection (stage 6 infoleak analog).** We run ASAN+UBSAN;
   MSAN (or poisoned-alloc stubs) would catch the userspace analog of
   copy-uninitialized-to-user. Not yet in the recipes.
4. **Their stage 11 formatting rules** are a checklist for
   `mail_for_maintainers/` letters: state pre-existing-ness explicitly, no
   markdown, constructive inline tone.
5. **Their benchmark corpus is now our rediscovery ground truth.**
   `benchmarks/benchmark.json` (999 unfiltered `Fixes:`-tagged bugs with
   buggy/fix SHAs) subsumes hand-curated shortlists — it contained
   our hand-picked #1 target, the idr bug, verbatim (`c6e8e595a079`, now in
   `docs/corpus.toml`).
   `tools/filter-corpus.py` extracts the PBT-eligible slice (~236 of 999 are
   dynamic-class; ~53 in harnessable subsystems) and splits output for blind
   hygiene: `hunt_targets.json` (SHAs + subsystem only, hunter-visible) vs.
   `judge_key.json` (descriptions, judge-only). Protocol details:
   `hegel-bughunt` skill § "Corpus-calibrated hunting". Outputs land in the
   gitignored `.corpus/` — the description text is Apache-2.0.

## License note

Sashiko is Apache-2.0; hegel-c is MIT. Reading their prompts as a checklist
is fine. Do **not** copy prompt text or code into this tree without carrying
attribution and the Apache-2.0 notice.
