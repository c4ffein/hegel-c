<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# TODO

## Next up (2026-10-21)

- **First tag (v0.1.0), which pyscotch waits for** (`PYSCOTCH_HANDOFF.md`,
  phase 2): once the pending work is committed and `make test` plus every
  Scotch pin pass. The libhegel bump is done (2026-11-06: 0.50.0, the
  state-machine creation in `core/hegel_server.c` ported to its
  declare-then-start ABI). Then say so in the README, so
  pyscotch can point its hegel-c witnesses at the tag.
- **`make test` is red about one run in thirty, from an engine bug** —
  `bugs:8c229d24-4b71-4e69-b782-ddcf94bbf1fd` (libhegel-float-simplest-nonzero,
  in c4ffein/bugs, with its draft report): a float drawn between two drawn
  floats hits a libhegel internal error (0.48.0 to 0.50.0, `HEGEL_SEED=40`).
  `tests/from-hegel-rust/test_floats_f64_bounds` is its witness: keep it as is.
- **libhegel coverage (2026-11-08).** hegel-c binds 111 of libhegel 0.50.0's
  157 functions; the 46 others are listed in `tools/libhegel-coverage.toml`,
  18 as deliberate (most getters, our own context / label / note / replay,
  the Unicode version, grammar placeholder markers), 28 as gaps.
  `make check-libhegel-coverage` fails on a function in neither list, so a
  bump can't add an unclassified one (`LIBHEGEL_VERSION=<new>` runs it
  against the new header before bumping). Bound on 2026-11-08: value draws,
  repeat loops, settings and profiles, events, grammars, `label_combine`.
  Left:
  - *Multi-threaded generation* (`test_case_clone` / `set_worker` / `block`):
    needs a design for fork mode, where the engine lives in the parent and
    a clone can't be handed to another process. Nofork could use it as is.
  - *Pretty printer* (25 functions): the engine's counterexample printer;
    hegel-c prints its own report, so low priority.
  Follow-ups from that work:
  - `HEGEL_STATISTICS=1` prints the engine's statistics although the runner
    sets verbosity QUIET, which libhegel 0.50.0's header says prints nothing:
    an upstream question (header or behaviour?). Its "no events were
    recorded" message names the Rust API (`tc.event(..)`).
  - `derandomize` with the database off seeds every run of a binary from the
    same empty key, so they all see the same input sequence (the `ci`
    profile too). Setting the `<binary>#<run>` key even with the database
    off would give each run its own stable seed; not done, since it changes
    default CI behaviour.
  - A passing repeat loop runs until the choice budget overruns (often
    ~400k iterations), which trips TooSlow in fork mode. A built-in cap would
    depart from the engine and hegel-rust; decide whether to offer one.
  - URLs from `hegel_draw_url` have no length bound (631 characters seen in
    2000 draws), and the caller-buffer contract truncates silently: an
    allocating variant (case-owned, like `hegel_draw_grammar`) would be safer.
- **Imperative helpers (2026-10-30).** Main is plain generators now (the
  schema layer moved to `experiments/`, see its README). What the
  harnesses read while deciding would have used:
  - ~~*Per-case allocation*~~ — done 2026-10-30: `hegel_alloc` /
    `hegel_realloc` / `hegel_free` (`core/hegel_alloc.c`), released by the
    case drivers on every exit path, in the body's process (no wire op);
    `tests/selftest/test_alloc_lifetime.c`. Left: port the Scotch props'
    `goto cleanup` chains where the memory isn't handed to Scotch.
  - ~~*A recursion helper*~~ — done 2026-10-30: `hegel_draw_recursive` /
    `hegel_recursive_child`, on `hegel_alloc` (an over-budget attempt
    longjmps back and its blocks are released by serial; the engine closes
    its spans). `tests/selftest/test_recursive_{tree,shrinks,misuse}.c`;
    the raw-protocol tests stay as they are.
  - *An average-size knob for collections:* the engine aims for ~5
    elements whatever the max (`length_p_continue` in libhegel's
    `native/core/state.rs` derives the average from min/max only), and
    `hegel_new_collection` takes no average. Issue drafted in
    `docs/upstream/collection-average-size.md` (not posted); until then a
    drawn length is the way to get large lists.
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
  `b4` rather than beside it; keep the submission entries in c4ffein/bugs
  (`data/submissions/`) in step.
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
- **Make the sg_split v3-agent evidence rerunnable.** The tests behind
  `data/submissions/linux-sg-split-series/v3-agent/REPORT.md` in c4ffein/bugs
  are archived in its `evidence/` as they ran in a throwaway sandbox (comments say `work/v3x/`).
  Wire them into `tests/irl/linux/sg_split/` as a target that applies a
  version folder's patches to the pin worktree and runs the extended suite,
  the fake-page / i386 properties (needs an i386 sysroot: fetch the Debian
  debs like `build32.sh` says) and the mutation run. Then any version folder
  can be re-checked after a container loss.
- **`make check-pins-upstream`: re-run each bug's pin against upstream** (mainline +
  linux-next for Linux, master for Scotch) and report what changed ("no longer
  reproduces upstream: probably fixed, go look"). Needs a shared, refreshed
  checkout at upstream's latest commit (the ~15 s per new kernel commit adds up),
  and pins for the bugs that have none (maple rewalk NULL deref, most Scotch
  entries). The checks recorded in the bugs' files were by hand (2026-10-22);
  c4ffein/bugs plans the same from its side (PLAN Phase 5, witnesses run).
- **Re-verify the Scotch bugs imported from `memory` / `transcript` sources**
  (in c4ffein/bugs) against current Scotch (line numbers moved since April),
  and give the real ones pins here.
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
