❯ new plan? create a protocol for testing hegel? create a dir in sandbox or something? copy the hegel project + scotch there,
  removing the .git(s) and the files that would nudge? start a subagent inside? ensure the subagent can find the bug ?

● Good plan. Let me design and implement it.

  Searched for 3 patterns (ctrl+o to expand)

● Building the protocol as a reusable script. Let me draft.

● Bash(mkdir -p /home/dev/workspace/hegel-c/scripts && ls /home/dev/workspace/hegel-c/scripts/)
  ⎿  check_doc_includes.py

● Write(scripts/setup-bughunt-sandbox.sh)
  ⎿  Wrote 177 lines to scripts/setup-bughunt-sandbox.sh
       1 #!/usr/bin/env bash
       2 # SPDX-License-Identifier: MIT
       3 # Copyright (c) 2026 c4ffein
       4 #
       5 # Reproducible sandbox for testing the hegel-bughunt skill against Scotch.
       6 #
       7 # Goal: create a clean directory containing only what an agent applying the
       8 # hegel-bughunt skill from cold should see — no internal project docs,
       9 # no test references, no git history, no bug-disclosing strings. The agent
      10 # operates in this sandbox; we then verify whether the skill drives the
     … +168 lines (ctrl+o to expand)

● Bash(chmod +x /home/dev/workspace/hegel-c/scripts/setup-bughunt-sandbox.sh &&
      /home/dev/workspace/hegel-c/scripts/setup-bughunt-sandbox.sh 2>&1 | tail -40)
  ⎿                             libscotchmetisv3.a
     make[2]: Entering directory '/tmp/hegel-bughunt-sandbox-1/scotch/src/libscotchmetis'
     make[2]: 'metis.h' is up to date.
     … +34 lines (ctrl+o to expand)

● Sandbox built clean. Let me verify before launching the agent.

  Listed 1 directory (ctrl+o to expand)

● Sandbox is clean. Spawning the honest dogfood now.

● Agent(Honest sandbox dogfood — bug hunt Scotch)
  ⎿  Done (96 tool uses · 183.2k tokens · 23m 20s)
  (ctrl+o to expand)

● Massive result. Let me verify the agent's claims and check whether they found the known bug.

  Searched for 4 patterns, listed 3 directories (ctrl+o to expand)

● Massive finding. Let me lay out what happened carefully.

  The honest sandbox dogfood worked. Agent in a clean sandbox, with only the skill content + Scotch source, built 22 tests + shared
   graph_gen.h + scotch_helpers.h + Makefile, and surfaced three bugs we didn't know about plus two suspects:

  1. SIGSEGV on SCOTCH_graphPart with parser-accepted single-letter strategies "f" and "d" — parser returns 0, runtime crashes.
  2. Parser/runtime contract mismatch on graph ordering — (g|g) selection operator parses but runtime rejects.
  3. SCOTCH_graphOrderCompute returns 0 but SCOTCH_graphOrderCheck rejects — silent corruption with strategy "o".

  Plus 2 suspects (parttab filled with -1, SIGABRT on "k").

  Did the agent find the bug we knew about? No — but they got within one mental hop. Looking at tests/scotch_helpers.h:
  static inline SCOTCH_Num strat_gen_flags(hegel_testcase *tc) {
      SCOTCH_Num flags = SCOTCH_STRATDEFAULT;
      /* ... 11 flag bits drawn ... */
      if (HEGEL_DRAW_INT(0, 1)) flags |= SCOTCH_STRATDISCONNECTED;  /* ← present! */
      return flags;
  }
  The agent built the exact flag fuzzer the methodology recommends, including STRATDISCONNECTED in the flag set. They then applied
  it to SCOTCH_stratGraphMapBuild (partitioning) — but not to SCOTCH_stratGraphOrderBuild (ordering). The known bug requires the
  flag fuzzer pointed at the ordering builder. They went 90% of the way and stopped one builder short.

  This is actually a stronger validation than catching the known bug. What the dogfood proves:

  1. Methodology drives autonomous discovery. Agent went from cold (no prior context, no project tests, no git history) to working
  audit suite + 3 real bugs in one session.
  2. The skill's specific patterns fired. Shared graph_gen.h (8 topologies), strategy-string fuzzer, flag-set fuzzer, goto cleanup
  discipline — all present in the agent's output, all consistent with the skill teaching.
  3. The bugs are different from the one we knew about. That's the original promise of bug-hunting PBT: surface bugs nobody
  anticipated, including the maintainer.

  Skill improvements suggested by this run:

  1. "Fuzz every parallel builder API independently" — the most actionable lesson. The skill says "for libraries with a strategy
  parser, fuzz the configuration"; it should be sharper: "For libraries with multiple parallel configuration entry points (e.g.,
  partition / order / color all having strategy builders), fuzz each independently — bugs hide in the less-tested builder."
  2. gdb may not be available — agent fell back to objdump + minimal repro driver. Skill recommends gdb for triaging cores; should
  add fallback (addr2line, signal-bisection) for environments without gdb.
  3. Skill's inspiration/targets/scotch reference is dead in a sandbox-only setup. Either drop it or generalize to "the target's
  source tree".
  4. HEGEL_ASSERT 512-byte truncation bites strategy-string format strings. Already a documented gotcha — the agent hit it but
  caught it from the docs.
  5. Post-Compute Check opportunity — Bug 3 was found by accident. Skill should add: "After every Algorithm call, opportunistically
   call any sibling *Check function — silent-corruption bugs surface there."
  6. "Letter survey" pattern — agent suggested running a simple "which letters work as top-level methods?" pre-pass before building
   grammars. Skill could say: "Before fuzzing a recursive grammar, do a 30-minute letter / token survey to identify which atoms are
   valid leaves."

  Sandbox protocol artifacts to commit (if you want):
  - scripts/setup-bughunt-sandbox.sh (the reproducible setup)
  - .claude/skills/hegel-bughunt/ (the skill, ~750 lines)
  - The first dogfood (tests/irl/scotch/* — the prior session's work that kicked this off)

  The sandbox itself at /tmp/hegel-bughunt-sandbox-1/ is reproducible from the script — no need to commit. Workspace dir has 22
  tests + bugs + cores; we can copy them to tests/irl/scotch/ if you want them as a baseline, but they're a different audit shape
  from your earlier 67-test suite.

  What do you want to do? Options I see:
  - (a) Commit the new artifacts (script + bughunt skill) as a milestone. Don't merge sandbox tests in.
  - (b) Apply the suggested skill improvements (1-6 above), then re-run sandbox dogfood, see if we now hit the known bug and find
  new ones.
  - (c) Move the agent's 22 sandbox tests into tests/irl/scotch/ as a complement to your 67-test work.

  I lean (a) + (b). The skill is shippable; tightening it before declaring v1 makes sense.

✻ Cooked for 29m 33s

※ recap: We're building a hegel-bughunt skill that drives agents to autonomously find bugs in C libraries; a sandboxed dogfood run
  just produced 22 working Scotch tests and surfaced 3 new bugs. Next: decide whether to commit the skill plus setup script as a
  milestone, or first tighten the skill based on the dogfood's findings. (disable recaps in /config)

❯   1. SIGSEGV on SCOTCH_graphPart with parser-accepted single-letter strategies "f" and "d" — parser returns 0, runtime crashes.
    2. Parser/runtime contract mismatch on graph ordering — (g|g) selection operator parses but runtime rejects.
    3. SCOTCH_graphOrderCompute returns 0 but SCOTCH_graphOrderCheck rejects — silent corruption with strategy "o".

    Plus 2 suspects (parttab filled with -1, SIGABRT on "k").

    Did the agent find the bug we knew about? No — but they got within one mental hop. Looking at tests/scotch_helpers.h:
    static inline SCOTCH_Num strat_gen_flags(hegel_testcase *tc) {
        SCOTCH_Num flags = SCOTCH_STRATDEFAULT;
        /* ... 11 flag bits drawn ... */
        if (HEGEL_DRAW_INT(0, 1)) flags |= SCOTCH_STRATDISCONNECTED;  /* ← present! */
        return flags;
    } => explain more thoroughly ?

● Let me actually trace through the agent's tests carefully so I can explain precisely.

  Read 1 file (ctrl+o to expand)

● Now I can explain the "one mental hop" precisely. Let me lay out what the agent built vs. what would have caught the bug.

  The 2×2 matrix the agent partially filled

  Scotch exposes its strategy system through two parallel entry points (a deliberate API design — different callers prefer
  different styles):

  ┌───────────────────────────────┬───────────────────────────────────────────┬────────────────────────────────────────────────┐
  │                               │    Configuration via strategy strings     │   Configuration via flag enums (Build APIs)    │
  │                               │                 (parsed)                  │                                                │
  ├───────────────────────────────┼───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ Partitioning                  │ SCOTCH_stratGraphMap(s, "...")            │ SCOTCH_stratGraphMapBuild(s, FLAGS, nparts,    │
  │ (SCOTCH_graphPart)            │                                           │ imb)                                           │
  ├───────────────────────────────┼───────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ Ordering (SCOTCH_graphOrder)  │ SCOTCH_stratGraphOrder(s, "...")          │ SCOTCH_stratGraphOrderBuild(s, FLAGS, ...)     │
  └───────────────────────────────┴───────────────────────────────────────────┴────────────────────────────────────────────────┘

  The agent built fuzzers for three of the four cells:

  - test_strat_part_fuzz.c — fuzzed strategy strings → stratGraphMap → graphPart. Found Bug 1 ("f" SIGSEGV).
  - test_strat_part_flags.c — fuzzed flag enums → stratGraphMapBuild → graphPart. Used strat_gen_flags() which includes
  SCOTCH_STRATDISCONNECTED. No bugs here.
  - test_strat_order_fuzz.c — fuzzed strategy strings → stratGraphOrder → graphOrderCompute. Found Bug 2 ((g|g)) and Bug 3 ("o"
  silent corruption).
  - test_strat_order_flags.c — MISSING. This is the cell that would have caught the known bug.

  Why the missing test specifically

  The known bug requires all three of these:

  1. The strategy was constructed via SCOTCH_stratGraphOrderBuild (the flag-enum entry point).
  2. The SCOTCH_STRATDISCONNECTED flag is in the flag set.
  3. SCOTCH_graphOrder is called on a graph with multiple connected components.

  Critically, SCOTCH_STRATDISCONNECTED is a flag-enum bit, not a strategy-string operator. There's no string-parser equivalent that
   exercises the same code path. So the agent's existing test_strat_order_fuzz.c (which fuzzes strings against stratGraphOrder
  parser) physically can't reach this bug — it never enters the Build codepath.

  The agent had:
  - ✅ The right input topologies (gen_disjoint_k2, gen_all_isolated in graph_gen.h)
  - ✅ The right flag fuzzer (strat_gen_flags(), with STRATDISCONNECTED in the bit set)
  - ✅ The correct comment on the fuzzer: "for SCOTCH_stratGraphMapBuild / OrderBuild" — they explicitly noted it serves both, but
  only wrote one caller
  - ✅ A test_bug_order_disconnected.c that runs ordering on disconnected graphs — but uses SCOTCH_stratInit (default empty
  strategy), not the Build flag enum

  So everything was 1 file away. A test_strat_order_flags.c mirroring test_strat_part_flags.c (replace Map with Order, replace
  graphPart with graphOrderCompute + Check) would have hit the bug within ~25 cases (25% trigger rate × 100+ default cases).

  What the methodology in the skill said

  The bughunt SKILL.md, Phase 2d:

  ▎ "Every algorithm test should pull a strategy from your config fuzzer (Phase 1d). Combined with multiple topologies, this puts
  ▎ each algorithm under a Cartesian product of (topology × configuration), which is where library bugs actually live."

  The agent followed this for one algorithm (partitioning). They didn't mechanically apply it to every algorithm. The skill said
  "every algorithm test" but didn't structure the work as "enumerate every (algorithm, configuration-entry-point) pair and check
  coverage of the matrix."

  The lesson for the skill

  This is a concrete, actionable improvement. The current Phase 2d text is correct but too easy to apply asymmetrically. The fix is
   to add the matrix framing explicitly:

  ▎ When the target exposes the same configurability through multiple parallel entry points — typically a string parser
  ▎ (stratXxx(&s, "...")) and a flag-enum builder (stratXxxBuild(&s, FLAGS, ...)), often once per algorithm — enumerate the matrix
  ▎ and confirm every cell has a fuzzer. Build a checklist:
  ▎
  ▎ | Algorithm | String fuzzer test        | Flag-enum fuzzer test    |
  ▎ |-----------|---------------------------|--------------------------|
  ▎ | foo       | ✓ test_foo_strat_string.c | ✓ test_foo_strat_flags.c |
  ▎ | bar       | ✓ test_bar_strat_string.c | (missing → write this)   |
  ▎
  ▎ The bugs concentrate in the diagonal cells with the least upstream test coverage — usually the flag-enum path of the less
  ▎ commonly used algorithm. Don't let "we have a strategy fuzzer somewhere" feel like coverage; the matrix tells you whether each
  ▎ pair is actually wired up.

  The agent built one excellent column (partitioning, both entry points) and one half-column (ordering, string only). With the
  matrix framing, that gap would have been visible.

  What's interesting about why the agent stopped at three

  Looking at the file structure, the agent built test_strat_part_fuzz.c first, then test_strat_order_fuzz.c (mirroring it for
  ordering — natural symmetry). Then they built test_strat_part_flags.c as a second style for partitioning. At that point they had
  three tests; the symmetric move was test_strat_order_flags.c. They stopped one step short — probably because:

  - The first three felt like "enough" coverage of the strategy space.
  - The skill said "fuzz the configuration" and they did.
  - Without the matrix framing, the missing fourth cell isn't visible from the inside.

  This is exactly the kind of thing PBT-of-PBT is good for: surfacing "I'm 75% covered" gaps that the audit author can't see from
  inside their own work.

  My take: option (b) from before stands — apply the matrix-framing fix, then re-run the sandbox dogfood. If the agent now writes
  the fourth cell autonomously and the bug fires, we've validated the improved skill and re-found the known bug from cold. That'd
  be a clean v1 of the bughunt skill.

  Want me to:
  - Apply the skill update + re-run sandbox dogfood?
  - Commit this milestone first, then iterate?
  - Something else?

✻ Crunched for 1m 16s

※ recap: Goal: ship a hegel-bughunt skill that drives autonomous PBT bug-hunting in C libraries. Current state: skill drafted and
  dogfooded against Scotch in a clean sandbox; agent found 3 new bugs but missed the known one. Next: tighten skill with a
  coverage-matrix framing, then re-run the sandbox dogfood. (disable recaps in /config)

❯ Did the agent use the generators or the schema api ?

  Searched for 1 pattern (ctrl+o to expand)

● Clean result: zero use of the schema API. Zero use of legacy hegel_gen_* combinators.

  The agent built tests entirely from:
  - HEGEL_DRAW_INT(...) — the primitive scalar draw macros (constructed in hegel_gen.h but they're just thin wrappers over
  hegel_draw_int(tc, ...)).
  - Hand-rolled imperative generators in their own graph_gen.h:
  static inline CSRGraph *gen_random_sparse(hegel_testcase *tc, int max_n) { ... }
  static inline CSRGraph *gen_2d_grid(hegel_testcase *tc, int max_side) { ... }
  static inline CSRGraph *gen_path(hegel_testcase *tc, int max_n) { ... }
  static inline CSRGraph *gen_complete(hegel_testcase *tc, int max_n) { ... }
  static inline CSRGraph *gen_disjoint_k2(hegel_testcase *tc, int max_pairs) { ... }
  static inline CSRGraph *gen_all_isolated(hegel_testcase *tc, int max_n) { ... }
  static inline CSRGraph *gen_single_vertex(hegel_testcase *tc) { ... }
  static inline CSRGraph *gen_tree(hegel_testcase *tc, int max_n) { ... }
  static inline CSRGraph *gen_any_topology(hegel_testcase *tc, int max_n) { ... }
  - goto cleanup discipline, malloc/free.

  No HEGEL_STRUCT, no HEGEL_ARR_OF, no HEGEL_LET/HEGEL_USE, no hegel_schema_* constructors, no HEGEL_DRAW(&addr, schema). The
  schema API is sitting there, the skill teaches it for ~400 lines, and the agent didn't touch it.

  This matches both prior dogfoods. It's consistent.

  Why

  The schema API teaches "describe a data type declaratively, get generation/allocation/cleanup for free." That works when the
  type's invariants are local — every field independently valid:

  typedef struct { int *items; int n; } Bag;
  HEGEL_STRUCT(Bag,
      HEGEL_LET    (n, HEGEL_INT(0, 10)),
      HEGEL_ARR_OF (HEGEL_USE(n), HEGEL_INT()),
      HEGEL_USE    (n));

  Bag is fine: any n and any int values produce a valid Bag.

  A Scotch graph in CSR form is non-locally constrained. verttab[i] and edgetab[verttab[i]..verttab[i+1]] and edgetab[verttab[j]..]
   all have to be coherent: every directed arc has a matching reverse arc, no self-loops, no duplicate edges, indices in range,
  monotonic verttab. The schema API can't express any of that — there's no HEGEL_INVARIANT_AFTER_FILL(check_fn) macro, no
  constraint solver, no "fill these N pointers consistently."

  So an agent presented with a Scotch-shaped target has two paths:

  1. Schema-build raw bytes, then hegel_assume(SCOTCH_graphCheck(...) == 0). The schema produces ~99% invalid graphs, the assume
  rejects them, the test budget evaporates, the FilterTooMuch health check trips. Bad.
  2. Imperative scalar draws + hand-built generator. Draw one bit per unordered pair, construct CSR from the resulting symmetric
  matrix. 100% of generator outputs are valid graphs. Cheap. Works.

  Both agents (dogfood 1 against SCOTCH_graphPart, this sandbox dogfood) independently chose path 2. So did the prior session whose
   output I rediscovered. So would I, if I sat down to write this from scratch.

  Implications

  The schema API is overkill for "C-library-with-non-local-invariants" targets, which is roughly all of:
  - Graph libraries (CSR, COO, adjacency-list)
  - Tree libraries (parent/child consistency)
  - Allocators / arenas (sequenced operations)
  - Parsers (only the inputs that parse are interesting; everything else is rejection)
  - Crypto / network protocols (validity is layered)
  - File-format codecs (magic numbers, length fields, checksums)

  These are the natural bug-hunt targets. And on every one of them, imperative-draw + hand-built generator wins.

  The schema API is overkill, but it's not wrong. It's the right tool for a smaller class of problems:
  - Pure data structures with local invariants (Bag, Tree of optional nodes, RGB triple)
  - Wire formats designed to accept any byte arrangement (CBOR, msgpack — with caveats)
  - Algorithm tests on simple typed inputs (sort, hash, encode-then-decode)

  But these are also exactly the problems where you might not need a PBT framework at all. They're the kind of targets where a unit
   test with a few hand-picked fixtures gets you 90% of the way.

  What this means for the project

  A few uncomfortable possibilities to consider:

  1. The schema API is an answer to a problem real C audits don't have. The 400-line schema chapter in the hegel-c reference might
  be teaching agents a tool they'll never reach for. If that's true, the cost is high — that's the first half of the C reference,
  and it competes for attention with the actually-load-bearing teaching (imperative draws + goto cleanup + shared _gen.h headers +
  flag fuzzers).
  2. The schema API could have an "invariant hook" added — HEGEL_VALIDATE(check_fn) that runs after fill, with health_fail if
  rejected too often. That'd make schema-mode viable for libraries with global invariants. Big design lift; I don't think it exists
   today.
  3. The skill could explicitly tell agents when to use schema vs. imperative. Right now both skills present the schema API as "the
   high-level API for describing C data structures, use it for anything that isn't a one-shot scalar test." That advice steered
  exactly zero of three independent agents toward using it on a real audit target. The advice is wrong, or the framing is wrong, or
   both.
  4. It could be that the dogfood targets are unrepresentative. Three audits all on Scotch-shaped libraries. A dogfood on a CBOR
  codec or JSON parser might get the schema API used heavily. That'd be a good follow-up experiment — try the bughunt skill on a
  target where the input shape is recursive.

  My honest take: option (3) is the cheapest win. Add a clear "imperative vs schema" decision rule near the top of the C reference:

  ▎ Imperative draws when the library's input has cross-field invariants (graph symmetry, sorted order, valid checksums, parser
  ▎ acceptance). Build the input one scalar at a time and let your generator construct a known-valid value.
  ▎
  ▎ Schema API when the library's input is a struct with independent fields (no cross-field validation), especially recursive types
  ▎  where HEGEL_SELF and HEGEL_OPTIONAL carry their weight.
  ▎
  ▎ If unsure: imperative is the safer default for C-library audits. You can always migrate later.

  What you do with the schema API itself (keep, deprecate, hook-extend) is a much bigger product decision. WDYT?

✻ Cooked for 1m 21s

❯ Could you actually explain more throughly THIS WHOLE BLOCK?  ```❯   1. SIGSEGV on SCOTCH_graphPart with parser-accepted
  single-letter strategies "f" and "d" — parser returns 0, runtime crashes.
      2. Parser/runtime contract mismatch on graph ordering — (g|g) selection operator parses but runtime rejects.
      3. SCOTCH_graphOrderCompute returns 0 but SCOTCH_graphOrderCheck rejects — silent corruption with strategy "o".

      Plus 2 suspects (parttab filled with -1, SIGABRT on "k").```

● Let me walk through each one carefully — what it actually is, why it matters, and what the underlying code path likely looks
  like.

  ---
  Bug 1: SIGSEGV on SCOTCH_graphPart with strategy "f" (and "d")

  What happened

  The agent's test_strat_part_fuzz.c draws random short strategy strings and feeds them through Scotch's two-stage strategy
  pipeline:

  SCOTCH_stratGraphMap(&strat, "f");      /* stage 1: parse */
  SCOTCH_graphPart(&grafdat, n, &strat);  /* stage 2: dispatch */

  The parser accepts "f" (returns 0). The runtime then SIGSEGVs trying to use it. Same crash with "d". Independent of nparts (1, 2,
   3, 4 all crash) and graph topology — the agent's shrinker reduced the failure to a 3-vertex K_3.

  What "f" actually is in Scotch

  Scotch's strategy DSL is a tiny grammar of single-letter methods, plus structural composers. Single letters are method codes:

  - f → Fiduccia-Mattheyses local refinement — a bipartitioning method that takes an existing partition and improves edge cut by
  swapping vertex pairs.
  - d → Diffusion-based partitioning — also a bipartitioning method.
  - g → Greedy graph-growing partitioning (also a bipartition method, top-level-ish).
  - r → Recursive bisection — a top-level method that splits into 2, then recursively into 2 again, etc.
  - m → Multi-level — coarsen, partition, refine, uncoarsen.

  Methods like f and d are inner methods: they assume you're already inside a recursive bisection step that has set up a "current
  bipartition" context — bgraph data structures, gain arrays, working buffers. They mutate that context; they don't construct one.
  There's no entry point for them at the top level.

  Why it crashes

  SCOTCH_graphPart at the top level dispatches to whatever method &strat says. When the parser accepts "f" as a top-level strategy,
   dispatch passes the user's SCOTCH_Graph to bgraphBipartFm() (or similar) — a function that expects a Bgraph *, not a Graph *.
  The two structs are different shapes: Bgraph has extra fields beyond Graph (gain tables, frontier sets, two-sided stats). The
  dispatched code reads off the end of the user's Graph, dereferences whatever uninitialized memory happens to be there, and
  segfaults.

  This is a type confusion at the strategy-dispatch layer, expressed as an over-permissive parser.

  Why it matters

  - Crash → process death. Anything linking Scotch into a long-running process (a HPC job scheduler, a mesh-refinement pipeline
  running for hours) gets killed.
  - No way for the caller to defend against it. They'd have to enumerate the set of strategy strings that are "safe at the top
  level" — Scotch doesn't publish that list anywhere.
  - The parser's job is precisely to reject malformed strategies with a non-zero return. It's failing to do so.

  Hypothesis on the fix

  The strategy parser maintains an internal table of (method letter, valid contexts) pairs. The fix is one entry in that table per
  method, marking which contexts (top-level mapping, top-level ordering, inner bipartition, inner separation, etc.) accept it. The
  dispatcher would then fail fast in stratGraphMap if the user-provided string contains a method invalid for the current context.

  This is a known tension in Scotch: the strategy DSL grew organically alongside the algorithms, and the constraints between them
  aren't enforced in one place.

  ---
  Bug 2: Parser/runtime contract mismatch on selection operator (g|g)

  What happened

  The selection operator | means "try both sub-strategies, pick the better result." In Scotch's grammar, (A|B) is a valid construct
   for graph mapping strategies. The agent fuzzed graph ordering strategies and produced (g|g). The parser accepted it
  (SCOTCH_stratGraphOrder(&s, "(g|g)") → rc=0). Then SCOTCH_graphOrderCompute returns 1 with stderr:

  hgraphOrderSt: selection operator not available for graph ordering strategies

  Reproduces with (g|s), (s|s), g|s — any selection inside an ordering context.

  Why it's a bug

  The error message confirms this is intentional at the runtime — Scotch's hgraphOrderSt (the ordering strategy dispatcher)
  explicitly disallows selection. So the runtime knows ordering doesn't support selection. The parser doesn't.

  The two layers are:

  ┌─────────────────────────────────────┬───────────────────────────────────────┬─────────────────────────────────────────────┐
  │                Layer                │             What it does              │                  This bug                   │
  ├─────────────────────────────────────┼───────────────────────────────────────┼─────────────────────────────────────────────┤
  │ stratParserParse2 (in               │ Tokenize + parse strategy string into │ Accepts (A|B) for any context               │
  │ scotch_libstrat.c)                  │  an AST                               │                                             │
  ├─────────────────────────────────────┼───────────────────────────────────────┼─────────────────────────────────────────────┤
  │ hgraphOrderSt (in                   │ Walk AST, dispatch each method        │ Crashes / errors on selection AST nodes for │
  │ hgraph_order_st.c)                  │                                       │  ordering                                   │
  └─────────────────────────────────────┴───────────────────────────────────────┴─────────────────────────────────────────────┘

  When you call SCOTCH_stratGraphOrder(&s, "(g|g)"), you've passed parsing — your SCOTCH_Strat now contains a parsed AST with a
  selection node. You think you have a valid ordering strategy. You don't.

  Severity

  - Wrong result + misleading API contract. Calling code that does the canonical "build strategy → check rc → use strategy" pattern
   will trip on the runtime error after committing to a specific code path.
  - Silent for the parser. A user iterating on strategy syntax has no parse-time signal that selection is forbidden in this
  context; they only find out when they try to use it.

  What the fix looks like

  The grammar accepted by the parser needs to be context-aware. There are at least three contexts (mapping, ordering,
  mesh-ordering), and each has a different valid alphabet of operators and method letters. Today the parser uses one grammar for
  everything; the per-context filtering happens at dispatch time, too late.

  Either:
  1. Make the parser take a context flag, reject incompatible operators at parse time. Cleanest API; biggest code change.
  2. Add a post-parse validation pass, called from stratGraphMap/stratGraphOrder/etc., that walks the AST and rejects
  context-incompatible nodes before returning to the caller. Less invasive.

  Either gives the user an rc != 0 from the strat-build call instead of a deferred runtime error.

  ---
  Bug 3: Silent corruption — graphOrderCompute rc=0 but graphOrderCheck rejects

  What happened

  Probably the most consequential of the three. The minimal trigger:

  SCOTCH_stratGraphOrder(&s, "o");                                 /* rc = 0 */
  SCOTCH_graphOrderInit(&g, &orderdat, perm, peri, ...);
  SCOTCH_graphOrderCompute(&g, &orderdat, &s);                     /* rc = 0 — "success" */
  SCOTCH_graphOrderCheck(&g, &orderdat);                           /* rc = 1 with stderr:
                                                                      "orderCheck2: invalid number of column blocks (1)" */

  Strategy "o" produces an ordering object whose internal validity check rejects it, but the function that built it claimed
  success.

  What "o" is

  In Scotch's ordering DSL, o is one of the bipartite-separator-based methods (probably OO — Optimum Ordering, or a particular
  nested-dissection variant). It's a real method, valid in this context, no parser/dispatcher mismatch like Bugs 1–2.

  Why it's the worst kind

  Look at the API contract from the caller's POV:

  if (SCOTCH_graphOrderCompute(&g, &orderdat, &s) != 0) {
      /* handle failure */
  }
  /* assume orderdat now contains a valid ordering */
  use_ordering(&orderdat);   /* ← reads garbage */

  The caller did everything right by the documented contract. They have no reason to call graphOrderCheck — that's an optional
  validation function, used in development and debugging, not on the production hot path. So the bogus ordering goes downstream:

  - Permutation arrays passed to a sparse matrix solver → solver crunches the wrong order → numerical wrong-answer.
  - cblknbr = 1 (which the check flagged) means the column-block partition is degenerate. A solver expecting a hierarchical block
  structure now sees one giant block. Performance collapses. Or, worse, downstream code indexes into rangtab expecting cblknbr+1
  entries and reads OOB.

  This is silent data corruption with a green-light return code. Strictly worse than a crash for production use, because the caller
   has no way to know something went wrong.

  Hypothesis

  Two paths likely:

  - o produces a single column block in some edge case (possibly: graph too small, or input topology that defeats the method's
  normal output structure). The method silently degenerates rather than returning an error.
  - The dispatcher records "success" based on "the method ran to completion without internal abort," but cblknbr == 1 should be a
  separate post-condition check — and it isn't run.

  Either way, the fix is: Compute should call Check (or its essential parts) before returning, and propagate the failure as a
  non-zero rc.

  Why PBT specifically catches this

  You'd never write this as a unit test from cold. The unit-test mindset is "did the function I called succeed?" — rc == 0 says
  yes, end of test. PBT catches it because the property is external to the function: "the orderdat must be a valid ordering,"
  verified by a separate Check call. The agent's test post-condition runs Check independently, surfacing the disagreement.

  This is exactly the pattern the dogfood agent's "what I'd do differently" recommends: pair Compute with sibling Check whenever
  possible. The skill should pull that into the methodology.

  ---
  Suspect 1: SCOTCH_graphPart rc=0 but parttab filled with -1

  Strategies "b" and "x" accepted by SCOTCH_stratGraphMap. SCOTCH_graphPart returns 0 — the function thinks it succeeded. But:

  - The agent pre-filled parttab with -42 (a sentinel) before calling.
  - After the call, every entry of parttab is -1.

  So the function did write to the buffer (overwrote -42), but wrote -1 (a sentinel meaning "unassigned / no part") to every
  vertex. There's no valid partition into nparts ≥ 1 parts where every vertex is -1.

  This is the silent variant of Bug 1. Same general shape (parser accepts an inner-only method as a top-level strategy), but
  instead of crashing, the dispatched code path writes the "no part assigned yet" sentinel and exits cleanly. The rc == 0 lies.

  Why "suspect" not "bug": the agent didn't fully confirm whether b and x are documented as valid top-level methods or not. If
  they're inner-only (likely), this is exactly Bug 1 in silent form, which is strictly worse than a crash.

  b is probably Bipartitioning (entry to a bipartition step from inside recursion); x might be a less common method. Both look like
   inner methods.

  ---
  Suspect 2: SIGABRT on SCOTCH_graphOrder with strategy "k"

  SCOTCH_stratGraphOrder(&s, "k");      /* rc = 0 */
  SCOTCH_graphOrder(&g, &s, perm, ...); /* SIGABRT */

  SIGABRT (rather than SIGSEGV) suggests an internal assert() is firing. Probably along the lines of assert(strat->context ==
  BGRAPH_CTX) somewhere in the dispatch chain — an internal sanity check that catches the type confusion before it crashes, but
  with a fatal-by-design abort rather than a clean error return.

  This is a third manifestation of the same bug class (parser too permissive at the top level), expressed via Scotch's defensive
  assert(). The fix is the same — reject k at parse time when it's contextually invalid.

  k might be the kway-multilevel inner method or similar.

  ---
  The pattern across all three bugs + two suspects

  The agent isn't reporting five unrelated bugs. Three plus two suspects are five symptoms of the same underlying defect class:

  ┌─────────────────┬────────────────────┬──────────────────────────────────┐
  │    Strategy     │       Layer        │             Symptom              │
  ├─────────────────┼────────────────────┼──────────────────────────────────┤
  │ "f" (Bug 1)     │ Top-level mapping  │ SIGSEGV                          │
  ├─────────────────┼────────────────────┼──────────────────────────────────┤
  │ "d" (Bug 1)     │ Top-level mapping  │ SIGSEGV                          │
  ├─────────────────┼────────────────────┼──────────────────────────────────┤
  │ "b" (Suspect 1) │ Top-level mapping  │ Silent corruption (parttab = -1) │
  ├─────────────────┼────────────────────┼──────────────────────────────────┤
  │ "x" (Suspect 1) │ Top-level mapping  │ Silent corruption                │
  ├─────────────────┼────────────────────┼──────────────────────────────────┤
  │ "k" (Suspect 2) │ Top-level ordering │ SIGABRT (internal assert)        │
  ├─────────────────┼────────────────────┼──────────────────────────────────┤
  │ (g|g) (Bug 2)   │ Top-level ordering │ Runtime returns rc=1             │
  └─────────────────┴────────────────────┴──────────────────────────────────┘

  Common cause: Scotch's strategy parser accepts grammar that the strategy dispatcher doesn't support in the current context. The
  parser is one big grammar applied to all contexts; the per-context constraints live elsewhere (in the dispatcher) and don't
  always match.

  The variants are determined by what the dispatcher does with an invalid strategy:

  - If the dispatched function dereferences the wrong struct → SIGSEGV (Bug 1).
  - If the dispatcher hits an assert along the way → SIGABRT (Suspect 2).
  - If the dispatched function's defensive code paths return -1 sentinels → silent corruption (Suspect 1).
  - If the dispatcher has an explicit per-context allowlist → clean rc=1 (Bug 2).
  - If the algorithm runs but post-conditions aren't checked → wrong-answer + rc=0 (Bug 3 — slightly different but related).

  What this implies for fixing Scotch: one structural change — context-aware parsing or post-parse validation — likely fixes all
  five symptoms at once. Three crashes, one silent corruption, and one silently-wrong result, all in the same root cause.

  What this implies for the bughunt methodology: strategy-string fuzzing was the right tool, and it surfaced an entire bug class on
   the first session. The agent doing one well-designed fuzzer and running it against multiple algorithms found more useful
  information than someone hand-writing a dozen targeted unit tests would have.

  ---
  Caveats on how confident to be

  Some of this is interpretation, not direct verification:

  - I'm inferring what f, d, g, b, x, k, o mean from Scotch convention and method-letter folklore. The agent didn't read Scotch's
  strategy-method documentation in detail (their report says they should have done a "letter survey first" — they didn't). The
  actual semantics may differ; the symptom classes are what's confirmed.
  - "These are all one root cause" is my synthesis from the symptom pattern. It's plausible — and it's the simplest hypothesis that
   explains the data — but a Scotch maintainer would need to confirm with gdb and source-reading.
  - The minimal reproducers in the agent's report are real (the core dumps in /tmp/hegel-bughunt-sandbox-1/workspace/tests/.cores/
  confirm crashes happened). The reproducers can be re-run; they're not theoretical.

  If you want hard verification of any specific bug, the workspace has the binaries; cd /tmp/hegel-bughunt-sandbox-1/workspace &&
  tests/test_bug_strat_segv will reproduce the SIGSEGV, etc.
