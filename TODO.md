<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# TODO

## Next up (2026-10-21)

- **Imperative helpers (2026-10-30).** Main is plain generators now (the
  schema layer moved to `experiments/`, see its README). What the
  harnesses read while deciding would have used:
  - *Per-case allocation:* `hegel_alloc (tc, size)`, freed at the end of
    the case and on any jump out of it — every Scotch prop has a
    `goto cleanup` chain. `core/`'s `hegel__draws_abandoned` hook already
    runs at those points (end of a nofork case, a stateful rule that
    assumed); fork children just exit.
  - *A recursion helper* for hand-written generators: runs the branch /
    leaf / retry / finish loop around a user's node function, unwinding
    and freeing an over-budget attempt — the tricky, repetitive part of
    `tests/selftest/test_recursion_{tree,nary_tree}.c`. The schema layer's
    `hegel__rec` (allocation tracking + longjmp unwind,
    `experiments/schema-with-callbacks/hegel_gen.c`) is the starting point,
    including its fix for engine calls that jump out mid-unwind.
  - *An average-size knob for collections:* the engine aims for ~5
    elements whatever the max (`length_p_continue` in libhegel's
    `native/core/state.rs` derives the average from min/max only), and
    `hegel_new_collection` takes no average. Ask upstream for one; until
    then a drawn length is the way to get large lists.
- **Remote-board transport.** The wire protocol in `core/hegel_internal.h`
  is transport-shaped on purpose: run it over serial/TCP with a board in
  the child's role and a watchdog reset in fork's.
- **hegel-skill C reference.** Add C to the canonical Agent Skill
  (`inspiration/hegel/hegel-skill`, see its CLAUDE.md), local first,
  upstream PR once settled.
- **Linux harnesses don't rebuild `libhegel_c.a`.** Add the FORCE
  delegation the other test Makefiles use, once, in
  `tests/irl/linux/pin.mk`.
- **ASan/UBSan CI job.** A selftest job with `libhegel_c.a` and the
  tests built with `-fsanitize=address,undefined`.
- **Case teardown.** `hegel_set_case_setup` has no partner; add
  `hegel_set_case_teardown` (from NEXT.md item D, deepstate's `TEST_F`).
- **Skill update for the missed bug classes** (2026-05-25 rediscovery,
  targets in `docs/corpus.toml`, pins in `tests/irl/linux/pins/`). Done
  2026-10-26 in `.claude/skills/hegel/SKILL.md`, not yet validated:
  - diagnostic content (bootconfig `39ebc8d7f561`): the skill no longer says
    not to check what errors say; "Diagnostic content" property added.
  - reader across teardown (maple_destroy_walk `ea9b77f98d94`, RCU race):
    not a concurrency limit after all, the July run hit it single-threaded by
    holding `rcu_read_lock` across `mtree_destroy`; added as a bug pattern.
  - maple_prealloc (`fba46a5d83ca`): no lesson yet. Its harness is green on
    buggy (never reaches the bug), so "draw bookkeeping-state mutators" is a
    guess; fix the harness first, then see what the miss teaches.
  The skill text stays generic (no kernel examples): hunters read it, so
  naming a target there would void its blind re-run.
  Validate: re-run bootconfig and maple_destroy_walk blind (fresh agents,
  harnesses scrubbed of hints) against the updated skill, record in
  `docs/runs.toml`.
- **Required reachability triage in `hegel-bughunt`.** Every finding gets
  a caller survey ("can any in-tree caller trigger it?") and a "harness
  artifact?" check (re-run against the reference semantics, not only our
  stubs). Every July 2026 finding was real but latent; the prealloc one
  was a stub artifact.
- **Aim hunts at untrusted-input code** (on-disk formats, network buffers,
  decompressors on adversarial data) rather than internal helpers — that's
  where reachable bugs are. Say so in the skill's target-selection step.
- **Research bets, in order:** (1) a prototype auto-harness generator,
  scoped to self-contained code (codecs, parsers, pure helpers); (2) several
  models as decorrelated hunters over one shared executable judge — only
  after the benchmark is rebuilt leak-free, or it measures noise.
- **Reach beyond leaf code: the forked child as any runtime.** Our Linux
  harnesses all run kernel code natively against hand-written shims, so they
  only reach leaf data structures and algorithms. The fork protocol already
  says "the board takes the child's role"; the same holds for a kernel:
  - `uml`: an `ARCH=um` kernel as a userspace process, a KUnit module fed
    draws over the pipe, KASAN; a crash is a SIGSEGV `waitpid` sees. Reaches
    page cache, mm, fs, core net. First target: lib/buildid.c (sashiko
    `29fb415a6a72`, needs `filemap_read_folio`); then ext4 `do_split`
    (`94824ac9a8aa`) and minix statfs (`8c97a6ddc956`).
  - `qemu`: the `.qemu` VM (validated with libnftables 2026-04-30), a guest
    agent over serial, VM reset as crash isolation. Reaches the syscall and
    netlink surface (nft, net/sched).
  Prove one bug end to end on `uml` before wiring more. (From the July 2026
  `tests/irl/repro-driver` draft; its shim example became the `group_cpus`
  pin, its MPI example duplicated `tests/mpi`.)
- **Grow the rediscovery corpus** with the deferred filesystem picks: ext4
  do_split, minix superblock, ADFS nzones, squashfs negative offset, UDF
  lengthAllocDescs, HFS+ catalog record, lib/crypto/mpi underflow (2–6 h
  of stubs each, as pins).
- **Patch pipeline: a guardrailed path from finding to mailing list — human
  review is the point.** The goal is better patches, not more of them: the
  pipeline raises the quality bar and NEVER sends anything itself. It refuses
  to produce a series without (1) a red/green pin (buggy red, fixed green),
  (2) a human `Signed-off-by` (an AI never adds one, per the kernel's
  coding-assistants rules) and (3) `Assisted-by:` disclosure. It runs the
  checks we did by hand — lore search for prior reports, `checkpatch.pl
  --strict`, `get_maintainer.pl` (needs `scripts/`, which our sparse checkouts
  lack), `Fixes:` sha exists — builds the cover letter from the ledger
  entries, and writes `.eml` files for the human to read and send (`sm
  send-patches` or `b4`/`git send-email`). Says all of this up front. Build on
  `b4` rather than beside it; keep the submission entries in
  `docs/findings.toml` in step.
- **Sashiko integration in that pipeline.** Run a Sashiko review of our own
  series *before* sending (akpm pointed us at Sashiko's review of sg_split v1
  and v2: it found the zero-nents OOB and bug 4 — both pre-existing — that our
  testing hadn't), fetch Sashiko's review of what we sent
  (`sashiko.dev/api/patchset?id=<msgid>`), and turn its concerns into
  properties: a Sashiko finding we can't make a pin go red on is either wrong
  or a gap in our generators (bug 4 was the latter: zeros excluded by
  construction). Sashiko is pinned under `inspiration/kernel-review/`
  (`docs/sashiko-gap-analysis.md`); the pipeline should fetch it on demand at
  that pin rather than as a submodule. Gate = every Sashiko finding triaged
  (fixed / refuted with a reason / named out of scope), not "zero findings".
- **Gates keyed to the patch content.** Each gate result (pin red/green,
  property runs with seed and case count, Sashiko, checkpatch) records the hash
  of the patch it checked; amending a patch turns its greens stale. Every
  red-only check carries one must-pass anchor (the July ladder printed six
  green "OK (red)" on builds that never compiled).
- **Green gates aren't enough: measure whether the properties are adequate.**
  The real failure mode: the model writes some properties, they pass, it calls
  the patch done, and a missing property hides the next bug. Mutation testing
  of the patched function is the mechanical signal. Measured 2026-10-23 on
  sg_split with a 60-line script (relational / `&&`↔`||` / ±1 mutants):
  v3 against its v3-era contract test → 8 of 24 mutants survive, one of them
  `skip > sglen` → `skip >= sglen`, the exact line of bug 5, which a
  cold-context session only found later by hand. v4a against today's full
  suite → 1 of 25 survives (an off-by-one writing past `out[]`), which
  points at the setup gap: property runs aren't built with ASan. Gate: every
  surviving mutant is killed by a new property, or marked equivalent with a
  reason, or names a setup gap (sanitizers) that gets fixed. Survivors are
  the to-do list of missing properties, so they go to a fresh, cold-context
  session that never saw the patch's tests, not back to the author. Other
  sources of properties to fold in: a differential property against the
  unpatched function (outside the bug's precondition, old == new: the patch
  changes only what it claims), a property-family checklist per function
  (contract boundaries, output well-formedness, conservation, error path
  leaves outputs untouched; bug 4 and bug 5 were each a missing family), and
  each claim in the commit message mapped to the property that backs it.
- **Private ledger overlay: undisclosed bugs and drafts, public once sent.**
  This repo is public, and `submissions/` and `docs/findings.toml` disclose
  every bug they name. Wanted: a second ledger for what isn't reported yet,
  with the same tools, that moves entries to the public one when they go out.
  Shape (2026-10-25 discussion): a *private git repo checked out at a
  gitignored path* (`private/`, or `$HEGEL_FINDINGS_PRIVATE`), holding its own
  `findings.toml` and `submissions/<id>/`. Not a submodule: a submodule records
  its URL, name and every bump in the public history (that leaks that
  undisclosed work exists, and when it moved) and public CI can't clone it.
  Pieces: `findings.py` overlays the private ledger when present (one id
  namespace, lint across both, a public entry may not point at a private id);
  the page badges private entries and its title says "includes private";
  `findings.py promote <id>` moves an entry and its folder to the public side
  (the commit is the disclosure); `serve` warns louder when private entries are
  loaded and bound to 0.0.0.0; blind bug-hunt sandboxes get neither ledger.
  Open: finding ids leaking through `related` links or notes; whether pins for
  undisclosed bugs (`tests/irl/*/pins/*.conf`) go private too (they name the
  bug as much as the ledger does).
- **Make the sg_split v3-agent evidence rerunnable.** The tests behind
  `submissions/linux-sg-split-series/v3-agent/REPORT.md` are archived in its
  `evidence/` as they ran in a throwaway sandbox (comments say `work/v3x/`).
  Wire them into `tests/irl/linux/sg_split/` as a target that applies a
  version folder's patches to the pin worktree and runs the extended suite,
  the fake-page / i386 properties (needs an i386 sysroot: fetch the Debian
  debs like `build32.sh` says) and the mutation run. Then any version folder
  can be re-checked after a container loss.
- **`make check-findings`: re-run each finding's pin against upstream** (mainline +
  linux-next for Linux, master for Scotch) and report what changed ("no longer
  reproduces upstream: probably fixed, go look"). Needs a shared, refreshed
  checkout at upstream's latest commit (the ~15 s per new kernel commit adds up),
  and pins for the findings that have none (maple rewalk NULL deref, most Scotch
  entries). Today's checks of `docs/findings.toml` were by hand (2026-10-22).
- **Re-verify the Scotch entries marked `memory` / `transcript`** in
  `docs/findings.toml` against current Scotch (line numbers moved since April),
  and give the real ones pins.
- **Real harness for `mab_calc_split`** (`4f6a6bed0bfe`, corpus candidate):
  a buggy snapshot at the fix's parent (the fix spans several functions, so
  no single-line revert) and a stateful property that drives node splits and
  calls `mt_validate()` after every op. `tests/irl/linux/maple_tree` is not
  it — that pin runs on v7.1 (already fixed) and never calls the validator.

## From FP

- lance combien de fois? parametrisable ou?
- HEGEL_SELF => prevenir des le debut, parametriser proba
- link hegel cpp now its out
- HEGEL protocol?
- dependency typo?
- bien preciser version de scotch
- verifier que les liens sont bons dans le README
--- => schema_api
- pas d espace avant les parentheses ouvrantes? un petit linter?
- THREE LAYERS => expliciter que HEGEL_ => shapes
  - preciser DRAW => shape ou val
- free pas en macro? bizarre... free schema vs shape => il a raison, j ai toujours pas clean mdr
- all public api vs all the public api(s) ???
- hegel_schema_t => expliquer que structures opaques, un peu moins expliciter mais linker vers code
  - preciser que runtime fait des checks, preciser lesquels?
- voir les FAST INT, mecanisme de promotion / acceleration => par le choix du compilo?.. comment gerer?
- dans schema api, facet introduites avant d etres definies?
- hegel union, untagged... => expliquer au bon moment?...
  - degager ce dont j ai plus besoin avec les facettes?...
- scotch src libscotch arch.h => pointer vers classe
  - classe a des pointeurs de fonction qui permettent de lire l union
  - arch_mesh_blabla pour voir comment c est instancier
- But ? => However
- rappeler les types dans la description des union?
- C++ => check si moyen de forcer pas de reour a la ligne entre + et +
- directement lier vers les array, int etc plus tot dans le doc? si besoin?
- lo et hi? comment gerer le min/max? documenter? je pense que bien gerer mais verifier code + expliciter
  - en particulier, comment je gere le u64? => expliciter que bien fait
- Text => review de mon cote
- metre des regles locales pour une struct? via un callbak? regenerer tant que pas A et B ou blabla?
- on a skip aux arrays => FACET pas clair! mieux expliquer :)

## High-leverage next work

### Review-readiness packet

| Item | Status |
|---|---|
| Embedded-systems demo | **TODO** — pick a target (Modbus / ring buffer / MQTT-SN / reviewer's choice), write 2–3 generators, host-compiled |
| `docs/review-framing.md` (one-page packet) | **TODO** — per-reviewer specific questions |

### Real-world README / quickstart

`docs/shrinking.md` walks through one real harness and CLAUDE.md's
"Writing generators" lists the conventions, but there's no "here's
your first 5-minute test" walkthrough.
For a library that might get adopted by strangers, a focused
getting-started doc is worth 30 minutes of writing time.

### Missing from Go

1. Named "batteries-included" generators. Go ships Binary, Dates, Datetimes, Emails, URLs, Domains, IPAddresses().IPv4()/IPv6().
C has none. Binary(min, max) is the biggest one for a C library — people test serializers, binary formats, wire protocols, MPI
buffers. Binary is covered now (`hegel_draw_bytes`); the rest are still missing.

2. Float control. Go has .Min().Max().ExcludeMin().ExcludeMax().AllowNaN(false).AllowInfinity(false). C has
hegel_draw_float / _double (lo, hi) — inclusive only, no NaN/Inf opt-in (bounded draws never give NaN). For numerical code (Scotch-adjacent, scientific C), NaN
handling is where bugs live. This is a real gap, not an aesthetic one.

(Run options and regex fullmatch, items 3 and 4 here before, are done: `hegel_run_options`,
and regex draws are full-match.)

## API polish

### Friction backlog from real tests (2026-05-01 burst)

Five subagents wrote tests against diverse kernel targets (BQL, kfifo,
bitmap, maple tree, ts_kmp) to collect API friction, not bugs. Votes =
how many of them hit it independently. `hegel_draw_bytes` (2 votes) shipped
then; the rest is open. Gen-layer items from that list are gone with the
old gen layer.

- **`HEGEL_ASSERT_EQ` / `_NE` / `_LT` / `_LE` / `_GT` / `_GE`** (+ `_PTR`,
  `_BYTES(a, b, n)`) — 2 votes: ~70% of invariant code was format strings.
  Print both sides automatically (`_Generic` for the integer types).
- **Weighted rules:** `hegel_state_machine_add_rule_weighted(sm, name, fn, w)`
  — 2 votes: tests registered one rule under several names to bias the
  pick, which pollutes traces.
- **Invariant context:** let an invariant know which rule just ran (name,
  index) for its message — 2 votes.
- **Doc: dynamic bounds** — 2 votes: `hegel_draw_int(tc, 0, state->n - 1)`
  works directly; testers reached for draw + clamp + assume first.
- 1 vote each: pick the k-th present item (`hegel_draw_index_of`);
  `hegel_draw_one_of(tc, n)` as a self-documenting int + switch; K-of-N
  subsets; byte draws from a non-contiguous alphabet; a precondition shared
  by several rules; per-test case count in suites (`hegel_suite_add_n`); a
  `_ctype` table and `scnprintf` stub for the radix-tree harness (parsers
  fail to link without them).

## Cleanup

### Extract or generalize `graph_gen.h` / `scotch_helpers.h`

Currently these live in `tests/irl/scotch/` as test-harness
helpers — CSR graph builders, strategy string generators, Scotch
error-code decoders.  They're Scotch-specific in their current
form, which is why they're sitting in the test harness rather
than in hegel-c proper.

But the *patterns* they encode are general:

- CSR construction from a logical `(nvert, edges[])` list is a
  reusable pattern anyone wiring a graph-library test would
  need — a library helper that takes a drawn logical graph and
  produces a canonical CSR.
- The strategy string generator is a focused example of
  building a valid DSL string from draws — useful as a
  pattern reference even if the specific DSL is Scotch's.
- Symmetry / dedup / self-loop-free post-processing is a
  recurring "structural invariant across array elements"
  pattern.

Decision to make: extract into hegel-c as library helpers, keep
them Scotch-specific but document the patterns in `docs/`, or
leave alone entirely.  Leaning toward
documenting the patterns first (cheap) and only extracting once
a second IRL target hits the same needs and we can see the real
shape of the abstraction.

Was previously listed as a "design decision" in the README; it's
really an open cleanup task, not a settled choice.

## Infrastructure

### Parallel test execution

Currently tests run sequentially. For suites with dozens of tests,
parallelization (across binaries or across cases within a binary)
would meaningfully cut CI time.

### Port more hegel-rust tests

See `tests/from-hegel-rust/manifest.md`. Remaining ones need
features we don't have (`exclude_min`, NaN/inf) or are Rust-specific.

## TODO
- [ ] Port more hegel-rust tests — see `tests/from-hegel-rust/manifest.md`. Remaining need features (exclude_min, NaN/inf) or are Rust-specific.
- [ ] Parallel test execution
- [ ] Test `HEGEL_DATABASE` replay in fork mode (`test_run_options` covers nofork only)
- [ ] More Scotch IRL tests — strategy string fuzzing against real parser, mesh partitioning
- [ ] More IRL targets beyond Scotch in `tests/irl/`
- [ ] Clean up `graph_gen.h` / `scotch_helpers.h` — currently in the Scotch test harness, contain general patterns (CSR builders, strategy generators) worth extracting or generalizing. See `TODO.md`.
