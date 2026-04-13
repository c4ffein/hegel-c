<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Design decisions — Rust bridge build

This document explains the load-bearing design choices for the
current hegel-c build: a pure C public API (`hegel_c.h`,
`hegel_gen.h`) backed by a Rust FFI layer that talks to the Hegel
server.  It's the "why," not the "how" — for the API reference
see [`schema-api.md`](schema-api.md); for the architecture
sketch see the corresponding section of [`../CLAUDE.md`](../CLAUDE.md).

A companion `design_pure_c.md` will eventually cover the
alternative build (pure C reimplementation of the Hegel wire
protocol, no Rust bridge) — not yet written, tracked in
[`../TODO.md`](../TODO.md) under "Real C implementation."

## Why a Rust bridge in the first place

Hegel's shrinking logic lives in a Python/Rust server process
("the Hegel server" below).  Clients talk to it over a CBOR
protocol on stdin/stdout.  Writing that protocol from scratch in
C is feasible but non-trivial, and even a correct implementation
would need to be re-verified against the reference every time
Hegel makes a protocol change.

The [`hegeltest`](https://crates.io/crates/hegeltest) Rust crate
already implements the full protocol, manages the server
subprocess as a process-wide singleton, and exposes a clean Rust
API (`Hegel::new(...).run()`).  Building on it:

- we inherit integrated shrinking, database replay, health
  checks, and the full set of Hypothesis strategies for free;
- we track upstream protocol changes by bumping a single Cargo
  dependency;
- the work left to do on our side is the FFI boundary and the
  schema layer, both of which are well-scoped problems.

The cost: a Rust toolchain is required to build the library
(users just link `libhegel_c.a`, so the C side is untouched).
That's the trade we're making until the pure C build exists as a
second option.

## Fork-per-case + parent-proxied IPC

For each test case, `hegel_run_test` (and friends) creates two
pipes, `fork()`s, and runs the C test function in the child.
`hegel_draw_*` calls in the child write request messages to the
parent via one pipe; the parent forwards them to the Hegel
server and writes the response back over the other pipe.  If the
child crashes (SIGSEGV, SIGABRT, stack overflow, anything a
signal handler can catch), the parent sees it via `waitpid` and
reports the failure to the server, which can then shrink the
crashing case like any other failing case.

**The child never talks to the Hegel server directly.**  This is
the load-bearing decision.  It's worth unpacking why the "obvious"
alternative — child opens its own server connection, talks to it
directly, parent just `waitpid`s — doesn't work:

1. **The Hegel server is stateful and sequential.**  One test
   case at a time.  The shrinker lives server-side and needs an
   unbroken conversation to decide what to try next.
2. **Children die.**  That's the whole point of fork mode: we
   expect crashes.  A child holding the server connection would,
   on crash, leave the server's view of state corrupted —
   mid-protocol, mid-case.  Reconnecting afterward doesn't
   recover the shrinking context for the case that just crashed.
3. **Parent-owned means crash is just "close pipe, report FAIL,
   continue."**  The parent's server connection is untouched by
   the child's death.  It tells the server "this case failed
   with this message," the server decides whether to shrink, and
   the parent forks the next case.  Crash isolation becomes a
   local property of the parent-child pair, not a distributed
   concern.

So fork-per-case and parent-proxied IPC are two halves of the
same design: you can only have the crash-isolation guarantee if
the child does not own the stateful, sequential server
connection.

### Wire protocol summary

The in-process child↔parent protocol is a simple
length-prefixed binary format over pipes.  Source of truth:
`rust-version/src/lib.rs` lines 46–69 (the `MSG_*` constants).

| Message           | Payload                     | Response                   |
|-------------------|-----------------------------|----------------------------|
| `MSG_DRAW_INT`    | `min(4) max(4)`             | `val(4)`                   |
| `MSG_DRAW_I64`    | `min(8) max(8)`             | `val(8)`                   |
| `MSG_DRAW_U64`    | `min(8) max(8)`             | `val(8)`                   |
| `MSG_DRAW_USIZE`  | `min(8) max(8)`             | `val(8)`                   |
| `MSG_DRAW_FLOAT`  | `min(4) max(4)`             | `val(4)`                   |
| `MSG_DRAW_DOUBLE` | `min(8) max(8)`             | `val(8)`                   |
| `MSG_DRAW_TEXT`   | `min(4) max(4)`             | `len(4) bytes(len)`        |
| `MSG_DRAW_BYTES`  | `min(4) max(4)`             | `len(4) bytes(len)`        |
| `MSG_DRAW_REGEX`  | `plen(4) pat(plen)`         | `len(4) bytes(len)`        |
| `MSG_DRAW_FLOAT_EX`  | `min(4) max(4) flags(4)` | `val(4)`                   |
| `MSG_DRAW_DOUBLE_EX` | `min(8) max(8) flags(4)` | `val(8)`                   |
| `MSG_DRAW_TEXT_EX`   | `min(4) max(4) flags(4)` | `len(4) bytes(len)`        |
| `MSG_DRAW_EMAIL/URL/IPV4/IPV6` | —             | `len(4) bytes(len)`        |
| `MSG_DRAW_DOMAIN`    | `max_length(4)`          | `len(4) bytes(len)`        |
| `MSG_DRAW_DATE/DATETIME` | —                    | `len(4) bytes(len)` (ASCII)|
| `MSG_START_SPAN`  | `label(8)`                  | —                          |
| `MSG_STOP_SPAN`   | `discard(1)`                | —                          |
| `MSG_ASSUME`      | —                           | child `_exit(77)`          |
| `MSG_FAIL`        | `len(4) msg(len)`           | child `_exit(1)`           |
| `MSG_OK`          | —                           | child `_exit(0)`           |

The protocol is intentionally dumb.  There's no framing beyond
"read N bytes for this field, read M more for the next."  Every
field is little-endian.  The `_ex` variants carry flag bitfields
that are decoded on the Rust side into hegeltest generator
options (`allow_nan`, `exclude_min`, alphabet flags, etc.).
Spans are fire-and-forget — structural shrinking hints that the
parent forwards to hegeltest and that don't produce a child-side
value.

Terminal messages (`MSG_ASSUME` / `MSG_FAIL` / `MSG_OK`) end the
child's side of the conversation: after sending one, the child
calls `_exit` directly without waiting for a response.  The
parent uses the exit code together with any prior `MSG_FAIL`
payload to build the outcome it reports to hegeltest.

### The orphan-leak fix (`catch_unwind` + `resume_unwind`)

This is worth writing down because it's subtle and the failure
mode was invisible.

hegeltest's engine can decide to abort a test case mid-draw by
raising a `__HEGEL_STOP_TEST` panic inside `TestCase::draw`.
Before the fix, `run_forked` called `parent_serve` in a context
where that panic propagated up directly, never reaching the
`waitpid` line.  The forked child was still alive, blocked on a
pipe read that nobody would ever answer.  When the Rust process
eventually finished, the child's pipe hit EOF; the child-side
`pipe_read_exact` silently returned `false`; the `hegel_draw_*`
functions ignored that return value and yielded zero; and the
child — now reparented to init — ran the test body to
completion with garbage inputs as an orphan.  Assertion failures
in the orphan went to a closed stderr.  Roughly 10% of forks
were affected on the `HEGEL_ARRAY_INLINE` repro.

The fix has two parts:

1. **`run_forked` wraps `parent_serve` in `catch_unwind`.**
   Whether the serve loop exits normally, returns a failure, or
   panics with the `StopTest` sentinel, the fork handler now
   unconditionally closes its pipe ends, `waitpid`s the child,
   drops `tc`, and only then `resume_unwind`s the caught panic
   (if any).  The child is always reaped before a panic can
   escape back to hegeltest's engine.  `drop(tc)` happens in
   normal control flow between the catch and the resume, so the
   "drop `tc` before panicking" invariant from the original code
   is honored without risking a double-panic abort from inside
   an unwind.

2. **Child-side `hegel_draw_*` functions now check the pipe read
   result.**  A failed `pipe_read_exact` means "my parent is
   gone, I'm orphaned."  The child calls `child_abandoned()`
   (= `_exit(0)`) immediately instead of continuing with a
   zeroed response.  This is belt-and-braces: if fix #1 ever
   regresses, fix #2 still kills the orphan cleanly instead of
   letting it finish running the test body.

Verification: 1000/1000 cases across 20 runs of the repro,
zero orphans, zero out-of-range draws.  The four
`TESTS_HEALTH` selftests (`test_filter_too_much` and
`test_overflow_too_much`, single + suite variants) exercise
the `parent_serve` panic propagation paths as
regression coverage.  A reproducer test is kept at
`tests/irl/scotch/test_array_inline_orphan_repro.c` but is not
currently wired into any CI target — see `TODO.md`
"Direct CI coverage for the orphan-leak fix."

## Nofork mode

`hegel_run_test_nofork` / `_n` run the test body directly in the
current process, no fork, no IPC proxying.  It exists for two
reasons:

1. **Benchmarking.**  The whole point of
   [`benchmarking.md`](benchmarking.md) is comparing fork and
   nofork to measure the cost of crash isolation.  Without a
   nofork path there's no baseline.
2. **MPI_Comm_spawn.**  Spawned MPI workers inherit a Hegel
   connection that expects single-process-at-a-time access.
   Fork-within-MPI is a pile of platform-specific failure modes
   best avoided; nofork lets the worker run the test body
   directly.  See [`mpi-testing.md`](mpi-testing.md).

**Nofork gives up crash isolation.**  A SIGSEGV in the test body
kills the whole process — hegeltest can't even tell the case
failed, let alone shrink it.  This is acceptable for benchmarks
(we're deliberately running trivial tests that can't crash) and
for MPI workers (the MPI runtime catches crashes at a different
layer), but it's why fork mode is the default and nofork is the
escape hatch.

## The hegeltest process-wide singleton

`hegeltest` itself manages the Hegel server as a
`OnceLock<HegelSession>`.  Spawned once on first
`Hegel::new(...).run()` call, reused for every subsequent call in
the same Rust process, torn down when the process exits.

This matters for three things:

1. **The suite API** (`hegel_suite_*`).  A suite runs N tests in
   one binary; the singleton means all N share one server
   startup instead of paying the ~1s cost N times.  The suite
   API is just sugar on top of `hegel_run_test_result` in a
   loop — the amortization is free, it comes from the singleton.
2. **The nofork benchmark.**  Sequential calls to
   `hegel_run_test_nofork` in one binary also share the
   singleton.  That's why the suite benchmark can compare fork
   (via `hegel_suite_run`) and nofork (via a sequential loop) on
   equal amortization terms — see `benchmarking.md`.
3. **The "pool server across binaries" TODO.**  Cross-process
   pooling would require a long-lived daemon or shared socket,
   neither of which hegeltest provides today.  The singleton is
   the reason the problem is bounded to "one startup per
   binary" rather than "one startup per test function."

## Pure C alternative

The long-term plan is to reimplement the Hegel wire protocol in
pure C, so the library has no Rust build-time dependency.  That
build will need its own design doc — conservatively named
`design_pure_c.md` — covering at minimum:

- how the Hegel wire protocol (currently CBOR over stdio) maps
  to a pure C implementation
- whether the C implementation keeps the fork-per-case
  architecture or collapses to a single process with
  signal-based crash handling
- how the schema layer (which is already pure C in
  `hegel_gen.c`) stays unchanged across both builds
- how consistency between the two builds gets verified (the
  current plan is property-based tests comparing outputs)

Nothing in the current Rust-bridge build should lock the pure C
build out of these decisions — in particular, the schema layer
deliberately sits on top of the primitive draw API and knows
nothing about the Rust FFI.  The pure C build should be able to
replace `libhegel_c.a`'s Rust half without touching
`hegel_gen.c`.
