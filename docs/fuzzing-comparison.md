<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Property-based testing vs. coverage-guided fuzzing

A common question when someone first encounters hegel-c: *"don't
we already have AFL and libFuzzer? Why does C need property-based
testing on top of coverage-guided fuzzing?"*

The honest answer is that they solve overlapping problems with
very different models, and each has classes of bugs the other
struggles to find. This doc walks through the difference, where
each tool is genuinely strong, where each hits a wall, and how to
decide which one fits a given target.

## The two models

**AFL / libFuzzer (coverage-guided fuzzing).** Mutate raw bytes.
Run the target with the input. Observe a coverage bitmap. Keep
inputs that hit new edges; throw away the rest. Detect failures
via signals (SIGSEGV, SIGABRT) and sanitizer instrumentation
(ASan, UBSan, MSan). The unit of input is `(uint8_t *data, size_t
size)`. Throughput is enormous — millions of cases per second
with persistent mode and a fork server.

**Hegel-c (property-based testing).** Generate typed C values
from a schema (struct, array, union, …). Pass the value to a
regular C function. Check user-defined assertions. When something
fails, shrink by manipulating the byte stream that *drives*
generation, replaying generation each time to produce a smaller
structurally-valid input. Throughput is bounded by IPC with the
Hegel server — orders of magnitude slower per case than AFL, but
each case is structurally meaningful.

The shared part: both can find crashes, both run with sanitizers,
both have shrinking. The differences are about what kind of bugs
are reachable in practice.

## Where coverage-guided fuzzing hits walls

### 1. Producing structurally valid input is the hard part of the job

AFL mutates bytes. For an input format like JSON, a SQL query, a
valid graph, or a C program, most mutated bytes get rejected at
the parser stage. The fuzzer wastes cycles on "input that doesn't
even parse" before it can probe deeper logic. Dictionaries,
grammar-aware mutators, and custom libFuzzer mutators help — but
you have to write them, and they're often a parser-shaped chunk
of code in their own right.

Hegel-c: every generated input is structurally valid *by
construction*. The schema enumerates what valid means; the
generator produces nothing else. For graph algorithms, ASTs,
schemas, valid SQL, valid programs — this is the biggest single
win.

### 2. Cross-field invariants are not natively expressible

"This length field equals this array's length." "This offset
points inside this buffer." "This enum tag matches the active
variant of this union." AFL has no language for any of this —
coherence has to emerge from coverage feedback, slowly and
unreliably.

Hegel-c's `HEGEL_LET` / `HEGEL_USE` is *literally* this. Bind
once, reference multiple times, coherence by construction. Same
for `HEGEL_LEN_PREFIXED_ARRAY` and `HEGEL_TERMINATED_ARRAY`. The
whole binding system exists because cross-field invariants are
the dominant case in C.

### 3. Non-crash properties are not expressible at all

This is the big one. AFL/libFuzzer find things that crash, abort,
or trip a sanitizer. They cannot find:

- `decode(encode(x)) ≠ x` — roundtrip failure that doesn't crash
- `merge(a, b) ≠ merge(b, a)` — CRDT commutativity violation
- `sort(sort(x)) ≠ sort(x)` — idempotency violation
- `partition_count(g) = sum(partition_sizes(g))` — set-theoretic
  invariant
- `BFS_distance ≤ DFS_distance` — algorithmic invariant
- `(A*B)*C ≈ A*(B*C)` within numerical tolerance —
  associativity
- `tree_balanced_after_insert(t, x)` — structural invariant

This is the wedge that justifies hegel-c's existence. The whole
class of "logic bugs that don't crash" is invisible to
coverage-guided fuzzing. PBT with user-defined oracles is the
natural tool. Hypothesis (which hegel-c inherits its engine from)
built its reputation finding exactly these in numpy / pandas /
scipy — code that doesn't crash but produces subtly wrong
answers.

You can bolt on differential fuzzing (run two implementations,
compare outputs) in libFuzzer, but it's awkward and limited to
oracle-by-comparison checks. Anything you'd express as "for all x,
property P holds" is not its native form.

### 4. Stateful sequences of operations are awkward

"Create object → add 5 elements → remove 2 → check size == 3." In
AFL you encode the operation sequence as bytes, write a dispatcher
that interprets opcodes, and effectively build a stateful fuzzer
harness yourself. In hegel-c you just write the sequence as C
code. Shrinking matters here too — an integrated shrinker can
shrink "300 random ops, fails" down to "create + delete + access,
fails." That's surgical.

### 5. Shrinking quality on structured input

AFL shrinks bytes via deletion / replacement and re-tests. For
unstructured input that's fine; for structured input it often
produces inputs that no longer parse, and you stop shrinking
before you reach the minimal failing case.

Hegel-c shrinks the *byte stream that drives generation*, then
replays generation. A 200-node graph that triggers a bug shrinks
to a 4-node graph that still triggers it, and the 4-node graph is
*still a valid graph*. This is the integrated-shrinking property
and it's genuinely rare outside the Hypothesis lineage. See
[shrinking.md](shrinking.md) for a worked example.

### 6. The harness itself is a bug source in AFL

Your `LLVMFuzzerTestOneInput(uint8_t *data, size_t size)` has to
convert raw bytes into the typed input the function under test
wants. That conversion is a parser; it has its own bugs; it
rejects valid-but-unusual structures it forgot about. Crashes in
the harness look like crashes in the target. Hegel-c skips this
entire layer — the schema produces typed values directly into the
function under test.

## Where coverage-guided fuzzing genuinely wins

Don't pretend otherwise.

### Throughput

AFL persistent mode + fork server runs millions of cases per
second. Hegel-c's IPC with the Hegel server caps it at orders of
magnitude less. For "find a crash by trying lots of inputs," AFL
crushes hegel-c on raw rate.

### Coverage guidance

For code with deep branching (parsers, decoders, state machines),
AFL's coverage-guided exploration reaches paths that random
structured generation will miss. PBT is "good random"; coverage-
guided fuzzing is "smart random." Hegel-c has no coverage
feedback today; the underlying Hypothesis engine has `target()`
for property-directed exploration but `hegeltest` 0.4.3 doesn't
expose it (see TODO.md).

### Ecosystem maturity

OSS-Fuzz, established corpora, sanitizer integration, CI tooling,
decades of CVE harvest. Hegel-c is months old and solo-maintained.

### No modeling cost

Point AFL at a binary, give it seed inputs, walk away. Hegel-c
demands you describe the input space first; for an unfamiliar
library that's days of work before the first test runs.

### Pure byte-input targets

Image, audio, video, compression, network parsers — AFL is
purpose-built for these. Hegel-c has no advantage here unless
there's a non-crash property to check. Don't point it at
libpng or zlib expecting to beat AFL.

## How to choose

It's not AFL **vs.** hegel-c. It's: **what kind of bug are you
hunting?**

| Bug type                                                     | Right tool        |
| ------------------------------------------------------------ | ----------------- |
| Crashes in byte-input parsers (image, audio, protocol)       | AFL/libFuzzer     |
| Memory safety in deeply branchy state machines               | AFL/libFuzzer     |
| Logic bugs in algorithms with structural input               | PBT (hegel-c)     |
| Roundtrip / commutativity / idempotency / algebraic          | PBT (hegel-c)     |
| Stateful API correctness over operation sequences            | PBT (hegel-c)     |
| Compiler / interpreter bugs requiring valid programs as input| PBT (CSmith-style)|
| Distributed systems consensus correctness                    | PBT (Hypothesis-style stateful) |

The two are complementary, not competitive. Real-world projects
often want both: AFL on the byte-eating frontends, PBT on the
algorithmic core.

## Where hegel-c's wedge is sharpest

Targets where structured generation pays off heavily and
coverage-guided fuzzing has known limitations:

- **Graph algorithms.** Partitioning, ordering, clustering,
  flow. Input is a valid graph; valid graphs are hard to mutate
  from random bytes. Invariants (vertex preservation, edge cut
  bounds, balance) are non-crash properties. The Scotch
  integration tests in this repo (`tests/irl/scotch/`) are an
  example.
- **CRDTs and other algebraic structures.** Commutativity,
  associativity, idempotency are exactly the laws PBT was made
  for.
- **Numeric / scientific code.** Matrix operations, solvers,
  signal processing. Byte fuzzing produces NaN soup; PBT
  produces structured inputs against tolerance-bounded
  invariants.
- **Compilers and interpreters.** Random valid programs (CSmith
  showed this works); coverage-guided fuzzers struggle to keep
  output well-formed.
- **Database query optimizers.** Random valid queries; check
  result equivalence under rewrite rules.
- **Encoding / decoding library pairs.** `decode(encode(x)) ==
  x` is the canonical PBT property and applies to
  protobuf / CBOR / msgpack / custom binary formats.
- **State machines with rich APIs.** Sequences of typed calls;
  invariants between them.

If your target falls in this list, hegel-c is doing something
AFL can't easily do. If it doesn't, AFL is probably the right
first stop.

## Practical implication for adopting hegel-c

If you point hegel-c at a JSON parser or a PNG decoder, AFL has
been there, will still beat you on throughput and infrastructure,
and the comparison won't flatter you. If you point it at a graph
library, a CRDT, a query optimizer, a numerical solver, a
compression lib's roundtrip property, or a CBOR encoder/decoder
pair — somewhere with a real invariant AFL can't articulate —
you're on home turf.

The Scotch IRL suite (`tests/irl/scotch/`,
[shrinking.md](shrinking.md) walkthrough) is the worked example
in this repo: a real graph-partitioning library, structured
generation of valid graphs, an algorithmic invariant, and a real
bug found and shrunk to a 3-vertex minimum. That's the shape of
target where hegel-c earns its keep.
